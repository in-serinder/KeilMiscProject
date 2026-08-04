#include "stm32f10x.h"
#include "jumper.h"
#include "adc.h"
#include "dht11_s.h"
#include "bmp280_s.h"
#include "rain_s.h"
#include "light_s.h"
#include "light_ctl.h"
#include "UART.h"
#include "buzzer.h"
#include "key.h"
#include "oled_lightpower.h"
#include "Delay.h"
#include <stdbool.h>

/*
 * 串口心跳通讯格式 以>开头 以<结尾 以-分隔
 * 发送
  * >dht11温度-dht11湿度-bmp280压力-bmp280温度-是否雨-是否夜晚-pt光伏板电压-电池电压-光线值-oled供电状态<
 * 例如
 * >13.00-50.00-1013.2-25.00-true-true-44.553v-3.92v-1000mv-true
 * oled供电状态: true=正在供电 false=已断电
 * ESP-01收到心跳后检测该字段 false->true 上升沿, 重新初始化SSD1306
 *
 * 调试信息格式 全英文
 *   // 传感器类型
 *   [Debug][Sensor] 传感器类型-传感器值
 *   // 配置类型 开机对阈值等宏变量配置 跳线等
 *   [Debug][Config] 配置类型-配置值
 *   // 用户操作类型 按键操作
 *   [Debug][Option] 操作类型-操作值
*/


/*
 * 使用TIM2定时器中断作为系统心跳: 一个tick = 10ms
 * 系统时钟72MHz, APB1=36MHz, TIM2挂APB1上(倍频后=72MHz)
 * 预分频 72-1 -> 1MHz(1个计数=1us), 自动重载 10000-1 -> 10ms 一次更新中断
 */
#define TICK_PRESCALER   72u        // 72MHz/72 = 1MHz
#define TICK_PERIOD      9999u      // 10000计数 = 10ms

#define TASK_FAST_TICKS      1u     // 快速传感器(PT/电池/雨/光): 每个tick读取
#define TASK_DHT11_TICKS     200u   // DHT11:  每200tick = 2s
#define TASK_BMP280_TICKS    100u   // BMP280: 每100tick = 1s
#define TASK_HEARTBEAT_TICKS 200u   // 串口心跳+调试: 每200tick = 2s

#define OLED_ON_TICKS  (5u * 60u * 100u)  // OLED手动供电时长 5分钟 = 30000tick

static volatile uint32_t tickCounter = 0;  // 系统tick计数, 每10ms在中断中+1

static uint8_t  dht11Temp   = 0;      
static uint8_t  dht11Humi   = 0;      
static float    bmpTemp     = 0.0f;   
static float    bmpPress    = 0.0f;   
static bool     isRain      = false;  
static bool     isNight     = false;  
static uint16_t lightMv     = 0;      
static float    ptVoltage   = 0.0f;   
static float    batteryVoltage = 0.0f;

static bool     lightOn     = false;  // 照明LED当前开关状态
static bool     lastNight   = false;  // 上一次白/夜状态
static bool     oledPowered = false;  // OLED当前供电状态
static uint32_t oledOffTick = 0;      // OLED自动关闭的时刻(tick)
static bool     dht11Ok     = true;   // DHT11最近一次读取是否成功(用于失败节流)


void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        tickCounter++;
    }
}


static void Tick_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseStructure.TIM_Period       = TICK_PERIOD;
    TIM_TimeBaseStructure.TIM_Prescaler    = TICK_PRESCALER - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode  = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);
}


static void Set_Light(bool on)
{
    lightOn = on;
    if (on)
        Light_On();
    else
        Light_Off();
}


static void Key_Process(uint32_t now)
{
    enum KeyState key = Key_Read();
    if (key == Key_None)
        return;

    /* 调试: 先打印检测到的按键(与跳线逻辑无关), 用于确认GPIO检测是否成功 */
    UART_Printf("[Debug][Option] Key-Detected-%s\r\n",
                (key == Key_LIGHT_Power) ? "LightPower" : "OLEDPower");

            if (key == Key_LIGHT_Power)
    {
        /* 手动翻转照明LED */
        Set_Light(!lightOn);
        UART_Printf("[Debug][Option] Key-LightPower-%s\r\n", lightOn ? "On" : "Off");
    }
        else if (key == Key_OLED_Power)
    {
        if (!Jumper_IsOLEDPowerOn())
        {
            /* 只更新供电截止时刻, 由Oled_Manage统一控制上电/断电,
               避免与Oled_Manage状态机冲突, 确保供电状态(心跳帧)一致 */
            oledOffTick = now + OLED_ON_TICKS;          /* 维持5分钟供电 */
            UART_Printf("[Debug][Option] Key-OLEDPower-On-5min\r\n");
        }
    }
}

static void Oled_Manage(uint32_t now)
{
    bool wantOn;

    if (Jumper_IsOLEDPowerOn())
    {
        wantOn = true;                                   /* 常亮 */
    }
    else
    {
        /* 距离上次按键(或0)未满5分钟则保持供电 */
        wantOn = ((int32_t)(now - oledOffTick) < 0);
    }

                if (wantOn != oledPowered)  /* 只在状态变化时操作GPIO */
    {
        oledPowered = wantOn;
        if (wantOn)
        {
            Oled_LightPower_On();
            /* 通知ESP-01: OLED重新上电, 需重新初始化SSD1306(断电后内部寄存器丢失) */
            UART_SendString("[Cmd]OLED-Reinit\r\n");
        }
        else
        {
            Oled_LightPower_Off();
        }
    }
}


static void Light_Auto_Control(void)
{
    if (!Jumper_IsNightAutoTurnOnLight())
        return;

    if (isNight != lastNight)   /* 白夜切换时刻 */
    {
        lastNight = isNight;
        Set_Light(isNight);     /* 夜晚开灯 白天关灯 */
        UART_Printf("[Debug][Sensor] Light-DayNight-%s\r\n", isNight ? "Night" : "Day");
    }
}


static void Fast_Sensors_Read(void)
{
    ptVoltage      = Adc_ReadVoltage(Adc_Channel_PT);
    batteryVoltage = Adc_ReadVoltage(Adc_Channel_BAT);
    isRain         = Rain_Sensor_IsRain();
    lightMv        = Light_Sensor_Read();
    isNight        = Light_Sensor_IsNight();
}


static void Dht11_Read(void)
{
    uint8_t t = 0, h = 0;
    static bool lastOk = true;

        if (DHT11_S_Read(&t, &h) == 0)
    {
        dht11Temp = t;
        dht11Humi = h;
        dht11Ok = true;
        lastOk = true;
    }
    else
    {
        dht11Ok = false;
        if (lastOk)   /* 只在首次失败时提示, 避免刷屏 */
        {
            lastOk = false;
            UART_Printf("[Debug][Sensor] DHT11-ReadFailed\r\n");
        }
    }
}


static void Bmp280_Read(void)
{
    BMP280_Sensor_Read();
    bmpTemp  = BMP280_Sensor_ReadTemperature();
    bmpPress = BMP280_Sensor_ReadPressure();
}


static void Data_Acquire(uint32_t now)
{
    static uint32_t lastDht11  = 0;
    static uint32_t lastBmp280 = 0;

    Fast_Sensors_Read();   /* 每个tick */

        /* DHT11失败后按5倍周期(10s)重试, 避免每2s白白阻塞约20ms拖慢主循环 */
    if ((int32_t)(now - lastDht11) >=
        (dht11Ok ? TASK_DHT11_TICKS : (TASK_DHT11_TICKS * 5UL)))
    {
        lastDht11 = now;
        Dht11_Read();
    }
    if ((int32_t)(now - lastBmp280) >= TASK_BMP280_TICKS)
    {
        lastBmp280 = now;
        Bmp280_Read();
    }
}


static void UART_Heartbeat(void)
{
        UART_Printf(">%.2f-%.2f-%.1f-%.2f-%s-%s-%.3fv-%.2fv-%umv-%s<\r\n",
                (float)dht11Temp, (float)dht11Humi,
                bmpPress, bmpTemp,
                isRain  ? "true" : "false",
                isNight ? "true" : "false",
                ptVoltage, batteryVoltage, lightMv,
                oledPowered ? "true" : "false");

    UART_Printf("[Debug][Sensor] DHT11-Temperature-%.2f\r\n", (float)dht11Temp);
    UART_Printf("[Debug][Sensor] DHT11-Humidity-%.2f\r\n",    (float)dht11Humi);
    UART_Printf("[Debug][Sensor] BMP280-Pressure-%.1f\r\n",   bmpPress);
    UART_Printf("[Debug][Sensor] BMP280-Temperature-%.2f\r\n", bmpTemp);
    UART_Printf("[Debug][Sensor] Rain-IsRain-%s\r\n",         isRain ? "true" : "false");
    UART_Printf("[Debug][Sensor] Light-IsNight-%s\r\n",       isNight ? "true" : "false");
    UART_Printf("[Debug][Sensor] PT-Voltage-%.3fv\r\n",       ptVoltage);
    UART_Printf("[Debug][Sensor] Battery-Voltage-%.2fv\r\n",  batteryVoltage);
    UART_Printf("[Debug][Sensor] Light-Value-%umv\r\n",       lightMv);
}


static void Tick_Process(uint32_t now)
{
    static uint32_t lastHeartbeat = 0;

    Key_Process(now);       /* 按键扫描+业务逻辑 */
    Data_Acquire(now);      /* 数据获取 */
    Oled_Manage(now);       /* OLED供电管理 */
    Light_Auto_Control();   /* 照明自动控制 */

    /* 心跳用增量式判断 + now快照:
       DHT11阻塞约20ms会让tickCounter漂移, 若用 tickCounter%200==0 会漏掉心跳;
       增量式按绝对tick间隔触发, 无论主循环是否追赶都保证2s一次 */
    if ((int32_t)(now - lastHeartbeat) >= TASK_HEARTBEAT_TICKS)
    {
        lastHeartbeat = now;
        UART_Heartbeat();   /* 串口心跳+调试信息 */
    }
}

int main(void)
{
    uint32_t lastTick = 0;

    UART_Init();
    UART_SendString("Start..\r\n");

    Delay_ms(500);

    Adc_Init();
    init_DHT11_S();
    BMP280_Sensor_Init();
    Rain_Sensor_Init();
    Light_Init();

    // Light_On();
    // Delay_ms(2000);
    // Light_Off();
    // Delay_ms(2000);


    Light_Sensor_Init();
    Buzzer_Init();
    Key_Init();
    Jumper_Init();
    Oled_LightPower_Init();


    Buzzer_Config(1);

    UART_Printf("[Debug][Config] Jumper-IsNightAutoTurnOnLight-%s\r\n",
                Jumper_IsNightAutoTurnOnLight() ? "True" : "False");
    UART_Printf("[Debug][Config] Jumper-IsOLEDPowerOn-%s\r\n",
                Jumper_IsOLEDPowerOn() ? "True" : "False");
    UART_Printf("[Debug][Config] RainSensor-RainThreshold-%umv\r\n", RAIN_SENSOR_RAIN_THRESHOLD);
    UART_Printf("[Debug][Config] OLED-OnDuration-5min\r\n");

    Tick_Init();

    Delay_ms(500);
    UART_SendString("Ready..\r\n");

    // 开机先读一轮数据, 使首次心跳即有有效值
    Fast_Sensors_Read();
    Dht11_Read();
    Bmp280_Read();
  

    while (1)
    {
        while (tickCounter == lastTick)
        {
        }
                lastTick = tickCounter;
        Tick_Process(lastTick);  /* 传入tick快照, 防止阻塞期间tickCounter漂移 */
    }
}

