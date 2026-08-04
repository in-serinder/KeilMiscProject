/*
 * esp01.ino - STM32 气象监测节点 · ESP-01 无线网关
 *
 * 1) 串口(115200) 接收 STM32(User/main.c) 心跳数据, 格式:
 *      >dht11温度-dht11湿度-bmp280压力-bmp280温度-是否雨-是否夜晚-pt电压-bat电压-光线值-oled供电状态<
 *      例: >13.00-50.00-1013.2-25.00-true-true-44.553v-3.92v-1000mv-true<
 *      当oled供电状态 false->true 上升沿(STM32重新给OLED上电)时,
 *      重新初始化SSD1306(断电后内部寄存器丢失)
 *      STM32还会发送指令帧 [Cmd]OLED-Reinit 主动通知重初始化,
 *      两者互为备份, 覆盖开机时序丢指令的情况
 *
 * 2) OLED(128x64) 显示顺序(从上到下):
 *      PT/BAT电压 | DHT11/BMP280温度 | 气压/湿度 | 是否有雨 | 光线值 |
 * 运行时长Uptime | 上次上传时间
 *
 * 3) MQTT 上报: 所有数据打包为 JSON, 携带 uptime 与当前时间(NTP同步)
 *
 * 注: NTP 时间源为网络, 未使用 DS1302(ESP-01 引脚 GPIO0/GPIO2 已被 OLED 占用,
 *     TX/RX 用于与 STM32 通信, 无空闲引脚接 DS1302 三线RTC)。
 */
/* MQTT报文缓冲必须 >= 最大topic+payload长度, 必须在include PubSubClient前定义
   默认256字节, 本工程JSON payload约230~260字节(含time时间串),
   超长导致publish失败 */
#define MQTT_MAX_PACKET_SIZE 512
#define MQTT_MAX_TOPIC_LENGTH 128
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <Wire.h>

#define SDA 2
#define SCL 0

#define OLED_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

const char *WIFI_SSID = "2.4GHZ";
const char *WIFI_PASSWORD = "320724fuck";

const char *MQTT_SERVER_IP = "8.130.191.142";
const int MQTT_PORT = 1883;
const char *MQTT_USER = "Relay";
const char *CLIENT_ID = "mqttx_1e157";
const char *MQTT_PASSWORD = "password";
const char *MQTT_TOPIC = "IOTGP/WMN";

const long UTC_OFFSET_SEC = 28800; // 时区偏移: UTC+8
const char *NTP_SERVERS[] = {"ntp.pool.org", "cn.pool.ntp.org",
                             "time.windows.com", "ntp.ntsc.ac.cn",
                             "203.107.6.88"};
const unsigned long NTP_UPDATE_MS = 3600000UL; // 每1小时重新同步一次

const unsigned long UPLOAD_INTERVAL_SEC = 30; // 每30s向MQTT上报一次

WiFiClient wc;
PubSubClient pc(wc);
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVERS[4], UTC_OFFSET_SEC, 60000);
JsonDocument json;

String rawDht11Temp, rawDht11Humi, rawBmpPress, rawBmpTemp;
String rawIsRain, rawIsNight;
String rawPtVoltage, rawBatVoltage, rawLightMv;
// 用于JSON
float fDht11Temp = 0, fDht11Humi = 0, fBmpPress = 0, fBmpTemp = 0;
float fPtVoltage = 0, fBatVoltage = 0;
int iLightMv = 0;
bool bIsRain = false;
bool bIsNight = false;
bool bOledPower = false;        // OLED当前供电状态(STM32心跳第10字段)
bool oledReinitPending = false; // 检测到OLED重新上电, 待重新初始化

String lastUploadTime = "--:--:--"; // 上次上报成功时间 HH:MM:SS
String dat = "";                    // 串口行缓冲
unsigned long bootMs = 0;
unsigned long lastPublish = 0; // 上次上报时刻(秒)
unsigned long lastNtpSync = 0;
unsigned long lastMqttAttempt = 0; // MQTT重连节流时刻(ms)
uint8_t mqttFailCount = 0;         // MQTT连续失败次数(用于重连退避)
unsigned long lastWifiAttempt = 0; // WiFi重连节流时刻(ms)
bool ntpSynced = false;

String getDateTimeString() {
  time_t t = timeClient.getEpochTime();
  setTime(t);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", year(), month(),
           day(), hour(), minute(), second());
  return String(buf);
}

String getTimeString() {
  return timeClient.getFormattedTime(); // "HH:MM:SS"
}

/* 运行秒数 -> "HH:MM:SS" / 超过24小时 -> "XdHH:MM:SS" */
String formatUptime(unsigned long sec) {
  unsigned long d = sec / 86400UL;
  unsigned long h = (sec % 86400UL) / 3600UL;
  unsigned long m = (sec % 3600UL) / 60UL;
  unsigned long s = sec % 60UL;
  char buf[16];
  if (d > 0)
    snprintf(buf, sizeof(buf), "%lud%02lu:%02lu:%02lu", d, h, m, s);
  else
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
}

/* 格式: >f-f-f-f-flag-flag-fv-fv-imv-flag<  共10个字段, 分隔符'-'
   最后一个字段: OLED供电状态(true=供电中) */
void parserSTM32Serial(String data) {
  /* 指令帧: [Cmd]OLED-Reinit -> OLED重新上电, 需重新初始化SSD1306 */
  if (data.startsWith("[Cmd]OLED-Reinit")) {
    oledReinitPending = true;
    return;
  }

  if (!data.startsWith(">") || !data.endsWith("<"))
    return;
  data = data.substring(1, data.length() - 1); // 去掉首尾 > >

  String fields[10];
  int count = 0, pos = 0;
  while (count < 9) { // 找9个'-' => 10个字段
    int idx = data.indexOf('-', pos);
    if (idx == -1)
      return;
    fields[count++] = data.substring(pos, idx);
    pos = idx + 1;
  }
  fields[9] = data.substring(pos);

  rawDht11Temp = fields[0];
  rawDht11Humi = fields[1];
  rawBmpPress = fields[2];
  rawBmpTemp = fields[3];
  rawIsRain = fields[4];
  rawIsNight = fields[5];
  rawPtVoltage = fields[6];
  rawBatVoltage = fields[7];
  rawLightMv = fields[8];

  // 去掉单位后缀 v / mv
  rawPtVoltage.replace("v", "");
  rawBatVoltage.replace("v", "");
  rawLightMv.replace("mv", "");

  fDht11Temp = rawDht11Temp.toFloat();
  fDht11Humi = rawDht11Humi.toFloat();
  fBmpPress = rawBmpPress.toFloat();
  fBmpTemp = rawBmpTemp.toFloat();
  fPtVoltage = rawPtVoltage.toFloat();
  fBatVoltage = rawBatVoltage.toFloat();
  iLightMv = rawLightMv.toInt();
  bIsRain = (rawIsRain == "true");
  bIsNight = (rawIsNight == "true");

  // OLED供电状态: 检测 关闭->打开 上升沿, 需重新初始化SSD1306
  bool newPower = (fields[9] == "true");
  if (newPower && !bOledPower)
    oledReinitPending = true;
  bOledPower = newPower;
}

/* STM32通过PB3控制OLED供电, 断电再上电后SSD1306内部寄存器全部丢失,
   必须重新发送初始化序列(相当于重新begin) */
void oledReinit() {
  delay(150); // 等待OLED上电稳定
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.display();
  Serial.println("[OLED] Re-init OK");
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("PT:");
  display.print(rawPtVoltage);
  display.print("v ");
  display.print("BAT:");
  display.print(rawBatVoltage);
  display.print("v");

  display.setCursor(0, 9);
  display.print("T1:");
  display.print(rawDht11Temp);
  display.print("C ");
  display.print("T2:");
  display.print(rawBmpTemp);
  display.print("C");

  display.setCursor(0, 18);
  display.print("P:");
  display.print(rawBmpPress);
  display.print("hPa ");
  display.print("H:");
  display.print(rawDht11Humi);
  display.print("%");

  display.setCursor(0, 27);
  display.print("Rain:");
  display.print(bIsRain ? "True" : "False");
  display.print(" Nig:");
  display.print(bIsNight ? "True" : "False");

  display.setCursor(0, 36);
  display.print("Light:");
  display.print(rawLightMv);
  display.print("mv");

  // 运行时长 Uptime (格式化为 HH:MM:SS / XdHH:MM:SS)
  unsigned long uptime = (millis() - bootMs) / 1000UL;
  display.setCursor(0, 45);
  display.print("UP:");
  display.print(formatUptime(uptime));

  // 上次上报时间
  display.setCursor(0, 55);
  display.print("Report:");
  display.print(lastUploadTime);

  display.display();
}

void publishData() {
  unsigned long uptime = (millis() - bootMs) / 1000UL;

  json["clientid"] = CLIENT_ID;
  json["uptime"] = uptime; /* 运行秒数 */
  json["time"] =
      ntpSynced ? getDateTimeString() : "1970-01-01 00:00:00"; /* 当前时间 */
  json["dht11_temp"] = fDht11Temp;
  json["dht11_humidity"] = fDht11Humi;
  json["bmp280_pressure"] = fBmpPress;
  json["bmp280_temp"] = fBmpTemp;
  json["is_rain"] = bIsRain;
  json["is_night"] = bIsNight;
  json["oled_power"] = bOledPower;
  json["pt_voltage"] = fPtVoltage;
  json["battery_voltage"] = fBatVoltage;
  json["light_mv"] = iLightMv;

  String payload;
  serializeJson(json, payload);

  if (pc.publish(MQTT_TOPIC, payload.c_str())) {
    lastUploadTime =
        ntpSynced ? getTimeString() : "--:--:--"; /* 记录本次上报时间 */
    Serial.println("[MQTT] OK -> " + payload);
  } else {
    /* state=0 且 len<512 时, 是ESP8266典型的TCP半开连接:
       下行还能读到残留数据, 上行write()已失败, 只能靠重连恢复 */
    Serial.println("[MQTT] Publish FAILED len=" + String(payload.length()) +
                   " state=" + String(pc.state()) +
                   " fail=" + String(mqttFailCount));
    mqttFailCount++; /* 计入退避 */
    pc.disconnect(); /* 半开连接无法自行恢复, 断开后loop按退避节流重连 */
  }
}

/* 收到 MQTT 指令 JSON, 目前支持:
 *   {"cmd":"get"} -> 立即上报最近一次数据(不等30s自动周期) */
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++)
    msg += (char)payload[i];
  Serial.println("[MQTT] CMD: " + msg);

  // 快速过滤: 订阅topic含数据回声/非指令消息, 避免无谓解析
  if (msg.indexOf("\"cmd\"") == -1)
    return;

  JsonDocument doc; // 独立文档, 避免与全局json(上报)冲突
  if (deserializeJson(doc, msg)) {
    Serial.println("[MQTT] CMD JSON parse FAILED");
    return;
  }

  if (String(doc["cmd"] | "") == "get") {
    Serial.println("[MQTT] CMD get -> publish now");
    publishData(); // 立即上报最近一次数据
  }
}

void Wifi_connect() {
  int count = 0;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connect to: \n\tSSID:" + String(WIFI_SSID) +
                 "\n\tPassWord:" + String(WIFI_PASSWORD));
  while (WiFi.status() != WL_CONNECTED && count < 30) { /* 最多等30s */
    Serial.print('.');
    delay(1000);
    count++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnect to " + String(WIFI_SSID) + " Successful\n");
    Serial.println("\nMAC: " + WiFi.macAddress() + "\nIP: " +
                   WiFi.localIP().toString() + "\nUsing Time:" + count + "s");
  } else {
    Serial.println("\nWiFi connect timeout");
  }
}

bool MQTT_connect() {
  if (WiFi.status() != WL_CONNECTED)
    return false;
  if (pc.connect(CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
    pc.subscribe(MQTT_TOPIC);
    Serial.println("Connect MQTT Server " + String(MQTT_SERVER_IP) +
                   " Success");
    return true;
  }
  Serial.println("MQTT connect failed, retry later");
  return false;
}

void setup() {
  Serial.begin(115200); // 与STM32 USART1(115200)一致
  delay(3000);
  Wire.begin(SDA, SCL);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(5, 20);
  display.print("Init...");
  display.display();

  bootMs = millis();
  pc.setServer(MQTT_SERVER_IP, MQTT_PORT);
  pc.setCallback(mqttCallback); /* 注册MQTT指令回调: {"cmd":"get"}立即上报 */
  /* 关键! 顶部#define MQTT_MAX_PACKET_SIZE对库的.cpp不生效(分开编译),
     buffer实际仍是默认256字节. 而publish报文需5+2+10(头+topic)+payload≈263字节,
     263>256时publish直接返回false(state=0, 连接正常). 必须运行时扩容: */
  pc.setBufferSize(512);
  Serial.println("[MQTT] bufferSize=" + String(pc.getBufferSize()));
  pc.setKeepAlive(15);    /* MQTT心跳15s, 服务器断连可被检测 */
  pc.setSocketTimeout(8); /* socket读超时8s: 防止pc.loop()阻塞卡死,
                            又避免服务器消息间隔>3s时被误断连接 */

  Wifi_connect();
  MQTT_connect();

  /* NTP 首次同步 */
  timeClient.begin();
  ntpSynced = timeClient.forceUpdate();
  if (ntpSynced) {
    setTime(timeClient.getEpochTime());
    Serial.println("[NTP] Sync OK: " + getDateTimeString());
  } else {
    Serial.println("[NTP] Sync FAILED");
  }
  lastNtpSync = millis();

  delay(500);
}

void loop() {
  unsigned long now = millis();
  unsigned long uptime = (now - bootMs) / 1000UL;

  // NTP 周期同步(每1小时), 失败则1分钟后再试(避免每次loop阻塞1.5s)
  if (WiFi.status() == WL_CONNECTED &&
      now - lastNtpSync >= (ntpSynced ? NTP_UPDATE_MS : 60000UL)) {
    if (timeClient.forceUpdate()) {
      ntpSynced = true;
      setTime(timeClient.getEpochTime());
      Serial.println("[NTP] Re-Sync OK: " + getDateTimeString());
    }
    lastNtpSync = now;
  }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (dat.length() > 0) {
        parserSTM32Serial(dat);
        dat = "";
      }
    } else {
      dat += c;
    }
  }

  if (oledReinitPending) {
    oledReinitPending = false;
    oledReinit();
  }

  if (uptime - lastPublish >= UPLOAD_INTERVAL_SEC) {
    publishData();
    lastPublish = uptime;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiAttempt >= 30000UL) {
      lastWifiAttempt = now;
      Wifi_connect();
    }
  } else if (!pc.connected()) {
    /* 线性退避: 首次失败3s后重连, 之后每次+3s, 上限21s
       既保证半开连接快速恢复, 又避免连续失败时打满重连日志 */
    unsigned long waitMs = 3000UL * (mqttFailCount + 1UL);
    if (waitMs > 21000UL)
      waitMs = 21000UL;
    if (now - lastMqttAttempt >= waitMs) {
      lastMqttAttempt = now;
      if (MQTT_connect())
        mqttFailCount = 0; /* 连接成功, 计数清零 */
      else
        mqttFailCount++;
    }
  } else {
    pc.loop();
  }

  updateDisplay();

  static unsigned long lastDiag = 0;
  if (now - lastDiag >= 60000UL) {
    lastDiag = now;
    Serial.println("[Diag] Alive heap=" + String(ESP.getFreeHeap()) +
                   " WiFi=" + String(WiFi.status()) +
                   " MQTT=" + String(pc.connected() ? "OK" : "DOWN"));
  }

  delay(10);
}
