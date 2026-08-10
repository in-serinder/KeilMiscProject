const mqtt = require('mqtt');
const sqlite3 = require('sqlite3').verbose();
const express = require('express');
const cors = require('cors');
const path = require('path');
const http = require('http');
const {
    Server
} = require('socket.io');
const os = require('os');

// ========== 配置 ==========
const MQTT_BROKER = 'mqtt://8.130.191.142:1883';
const MQTT_TOPIC = 'IOTGP/WMN';
const DB_PATH = path.join(__dirname, 'weather.db');
const HTTP_PORT = parseInt(process.env.PORT, 10) || 3000;
const CLEANUP_INTERVAL_HOURS = 72;
const PRESSURE_HISTORY_SIZE = 30; // 用于大气压预测的历史数据点数

// ===== 数据主动拉取 =====
const DATA_PULL_ENABLED = true; // 启用主动拉取
const DATA_PULL_INTERVAL_MS = 5000; // 超过该间隔(最大5s)未收到数据则主动下发指令
const DATA_PULL_WATCHDOG_MS = 1000; // 看门狗检查周期
const DATA_PULL_CMD = JSON.stringify({
    cmd: 'get'
}); // 主动拉取指令

// 获取本机局域网 IPv4 地址 (供前端配置连接用)
function getLanIPs() {
    const list = [];
    const nets = os.networkInterfaces();
    for (const name of Object.keys(nets)) {
        for (const net of nets[name] || []) {
            if (net.family === 'IPv4' && !net.internal) {
                list.push(net.address);
            }
        }
    }
    return list;
}

// ========== 全局变量 ==========
let pressureHistory = []; // 近期大气压历史 { time, pressure }
let lastHWPacket = null;
let lastDataReceivedAt = Date.now(); // 最近一次收到有效数据的时间 (ms)
let lastPullRequestAt = 0; // 最近一次主动下发拉取指令的时间
let pullRequestCount = 0; // 主动拉取累计下发次数

// ========== 数据库初始化 ==========
const db = new sqlite3.Database(DB_PATH, (err) => {
    if (err) {
        console.error('[DB] 打开数据库失败', err.message);
        process.exit(1);
    }
    console.log('[DB] 已连接到 SQLite 数据库');
});

db.serialize(() => {
    db.run(`CREATE TABLE IF NOT EXISTS weather_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    clientid TEXT,
    uptime INTEGER,
    time TEXT,
    dht11_temp REAL,
    dht11_humidity REAL,
    bmp280_pressure REAL,
    bmp280_temp REAL,
    is_rain INTEGER,
    is_night INTEGER,
    weather_prediction INTEGER,
    oled_power INTEGER,
    pt_voltage REAL,
    battery_voltage REAL,
    light_mv REAL,
    sw_pressure_prediction INTEGER,
    created_at INTEGER
  )`, (err) => {
        if (err) console.error('[DB] 创建表失败', err.message);
    });

    // 创建索引
    db.run(`CREATE INDEX IF NOT EXISTS idx_time ON weather_data(time)`);
    db.run(`CREATE INDEX IF NOT EXISTS idx_created_at ON weather_data(created_at)`);
});

// ========== 大气压预测算法 ==========
// 0=转晴天 1=转降雨刮风 2=转阴天 3=转雷暴强对流 4=无效预测
// 自动适配气压单位: 如果是 ~1000 范围视为 hPa; 如果是 ~100000 范围视为 Pa
function detectPressureUnit(p) {
    if (typeof p !== 'number' || p <= 0) return 'unknown';
    if (p > 10000 && p < 150000) return 'Pa';
    if (p > 800 && p < 1200) return 'hPa';
    return 'unknown';
}

function predictByPressure(currentPressure, bmp280Temp) {
    if (pressureHistory.length < 5) return 4; // 数据不足

    // 自动判定单位并统一换算到 hPa 进行判断和阈值比较
    const unit = detectPressureUnit(currentPressure);
    // 阈值基准 (使用 hPa)
    const STD_P_hPa = 1013.25; // 标准大气压
    let curr_hPa;
    if (unit === 'Pa') curr_hPa = currentPressure / 100;
    else if (unit === 'hPa') curr_hPa = currentPressure;
    else return 4; // 单位未知，无法判断

    // 计算压力变化趋势 (按原始单位)
    const pressures = pressureHistory.map(p => p.pressure);
    const n = pressures.length;
    let sumX = 0,
        sumY = 0,
        sumXY = 0,
        sumXX = 0;
    for (let i = 0; i < n; i++) {
        sumX += i;
        sumY += pressures[i];
        sumXY += i * pressures[i];
        sumXX += i * i;
    }
    const slope = (n * sumXY - sumX * sumY) / (n * sumXX - sumX * sumX);
    // 把斜率转换成 hPa/样本 方便阈值比较
    const slope_hPa = (unit === 'Pa') ? slope / 100 : slope;

    const avgP_raw = pressures.reduce((a, b) => a + b, 0) / n;
    const totalChange = currentPressure - pressures[0];
    const changePercent = avgP_raw > 0 ? (totalChange / avgP_raw) * 100 : 0;

    const recentN = Math.min(3, n);
    const recentPressures = pressures.slice(-recentN);
    const recentAvg = recentPressures.reduce((a, b) => a + b, 0) / recentN;
    const recentChange = currentPressure - recentPressures[0];
    const recentChangePercent = recentAvg > 0 ? (recentChange / recentAvg) * 100 : 0;

    let variance = 0;
    for (const p of pressures) variance += (p - avgP_raw) ** 2;
    variance /= n;
    const stdDev = Math.sqrt(variance);
    const volatility = avgP_raw > 0 ? (stdDev / avgP_raw) * 1000 : 0; // 千分比

    const pressureLevel = curr_hPa / STD_P_hPa; // 相对标准大气压水平

    // ========= 预测阈值 (以 hPa 斜率和百分比变化为基准) =========
    // 快速大幅下降 + 明显偏低气压 => 雷暴强对流
    if (slope_hPa < -0.8 && recentChangePercent < -0.08 && pressureLevel < 0.997) return 3;
    // 中等下降 => 转降雨刮风
    if (slope_hPa < -0.4 && recentChangePercent < -0.04) return 1;
    // 缓慢下降或气压略低 => 转阴天 (温度<25视作条件辅助)
    if (slope_hPa < -0.15 || (pressureLevel < 0.999 && bmp280Temp < 25)) return 2;
    // 稳定上升或气压偏高 => 转晴天
    if (slope_hPa > 0.3 || (pressureLevel > 1.002 && recentChangePercent > 0.02)) return 0;
    // 大幅波动且下跌 => 雷暴可能
    if (volatility > 0.8 && recentChangePercent < -0.03) return 3;
    // 稳定 => 按当前气压水平判断晴/阴
    if (Math.abs(slope_hPa) < 0.1 && volatility < 0.3) {
        return pressureLevel >= 1.0 ? 0 : 2;
    }
    return 4;
}

// ========== MQTT 连接 ==========
const mqttClient = mqtt.connect(MQTT_BROKER, {
    reconnectPeriod: 5000,
    connectTimeout: 10000,
    clientId: 'monitoring_platform_' + Math.random().toString(16).substr(2, 8)
});

mqttClient.on('connect', () => {
    console.log('[MQTT] 已连接到 Broker:', MQTT_BROKER);
    mqttClient.subscribe(MQTT_TOPIC, (err) => {
        if (err) {
            console.error('[MQTT] 订阅失败:', err);
        } else {
            console.log('[MQTT] 已订阅主题', MQTT_TOPIC);
        }
    });
});

mqttClient.on('reconnect', () => {
    console.log('[MQTT] 正在重连...');
});

mqttClient.on('error', (err) => {
    console.error('[MQTT] 错误:', err.message);
});

mqttClient.on('message', (topic, message) => {
    try {
        const payload = message.toString();
        const data = JSON.parse(payload);
        // ---- 合法性校验: 过滤空/测试消息 ----
        if (!data || typeof data !== 'object') return;
        // 命令消息(如 cmd=get 指令)：自回环/调试指令，静默忽略，不计入数据
        if (typeof data.cmd === 'string') return;
        if (typeof data.dht11_temp !== 'number' || typeof data.bmp280_pressure !== 'number') {
            console.log('[MQTT] 跳过非数据消息 (缺字段)', payload.substr(0, 120));
            return;
        }
        if (!isFinite(data.dht11_temp) || !isFinite(data.bmp280_pressure)) {
            console.log('[MQTT] 跳过非法数值消息', payload.substr(0, 120));
            return;
        }
        handleWeatherData(data);
    } catch (e) {
        // 解析失败, 只在 payload 非空时提示, 避免刷屏
        if (message.length > 0) {
            console.error('[MQTT] 解析消息失败:', e.message, message.toString().substr(0, 160));
        }
    }
});

function handleWeatherData(data) {
    // 记录收到数据时间，供看门狗判断是否需要主动拉取
    lastDataReceivedAt = Date.now();
    // 更新大气压历史
    if (typeof data.bmp280_pressure === 'number' && data.bmp280_pressure > 0) {
        pressureHistory.push({
            time: data.time || new Date().toISOString(),
            pressure: data.bmp280_pressure
        });
        if (pressureHistory.length > PRESSURE_HISTORY_SIZE) {
            pressureHistory.shift();
        }
    }

    // 计算软件预测
    const swPressurePrediction = predictByPressure(
        data.bmp280_pressure || 0,
        data.bmp280_temp || 0
    );

    const now = Date.now();
    const stmt = db.prepare(`INSERT INTO weather_data (
    clientid, uptime, time, dht11_temp, dht11_humidity,
    bmp280_pressure, bmp280_temp, is_rain, is_night,
    weather_prediction, oled_power, pt_voltage, battery_voltage,
    light_mv, sw_pressure_prediction, created_at
  ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`);

    stmt.run(
        data.clientid || null,
        data.uptime || 0,
        data.time || new Date().toISOString(),
        data.dht11_temp || 0,
        data.dht11_humidity || 0,
        data.bmp280_pressure || 0,
        data.bmp280_temp || 0,
        data.is_rain ? 1 : 0,
        data.is_night ? 1 : 0,
        data.weather_prediction ? 1 : 4,
        data.oled_power ? 1 : 0,
        data.pt_voltage || 0,
        data.battery_voltage || 0,
        data.light_mv || 0,
        swPressurePrediction,
        now,
        function (err) {
            if (err) {
                console.error('[DB] 插入数据失败:', err.message);
            }
        }
    );
    stmt.finalize();

    // 保存最新数据包用于广播
    lastHWPacket = {
        ...data,
        sw_pressure_prediction: swPressurePrediction,
        _created_at: now
    };

    // 通过 Socket.IO 广播
    if (io) {
        io.emit('weather_data', lastHWPacket);
    }

    console.log('[DATA]', data.time, 'DHT:', data.dht11_temp + '°C/' + data.dht11_humidity + '%',
        'BMP:', data.bmp280_pressure + 'Pa', 'SW预测:', swPressurePrediction);
}

// ========== 数据主动拉取 ==========
// 向主题下发 cmd=get 指令，设备收到后会直接回传最新数据
function publishPullRequest(reason) {
    if (!mqttClient || !mqttClient.connected) {
        console.log('[PULL] 跳过下发，MQTT 未连接');
        return false;
    }
    mqttClient.publish(MQTT_TOPIC, DATA_PULL_CMD, {
        qos: 0,
        retain: false
    }, (err) => {
        if (err) {
            console.error('[PULL] 下发指令失败:', err.message);
        } else {
            console.log('[PULL] 已下发', DATA_PULL_CMD, reason ? '(' + reason + ')' : '');
        }
    });
    lastPullRequestAt = Date.now();
    pullRequestCount++;
    return true;
}

// 数据新鲜度看门狗: 超过 DATA_PULL_INTERVAL_MS 未收到有效数据 -> 主动拉取
function checkDataFreshness() {
    if (!DATA_PULL_ENABLED) return;
    const elapsed = Date.now() - lastDataReceivedAt;
    if (elapsed >= DATA_PULL_INTERVAL_MS) {
        publishPullRequest('数据超时 ' + Math.round(elapsed / 1000) + 's');
        // 重置计时（无论是否发送成功），避免看门狗在短周期内重复触发刷屏
        lastDataReceivedAt = Date.now();
    }
}

// 启动看门狗
setInterval(checkDataFreshness, DATA_PULL_WATCHDOG_MS);

// ========== 72小时数据库清理 ==========
function runCleanup() {
    const cutoff = Date.now() - CLEANUP_INTERVAL_HOURS * 60 * 60 * 1000;
    db.run(`DELETE FROM weather_data WHERE created_at < ?`, [cutoff], function (err) {
        if (err) {
            console.error('[DB] 清理失败:', err.message);
        } else {
            console.log(`[DB] 已清理${this.changes} 条旧记录 (保留最近${CLEANUP_INTERVAL_HOURS}h)`);
        }
    });
    // 同时清理过期的大气压历史
    const pressureCutoff = Date.now() - 2 * 60 * 60 * 1000; // 只保留2小时的压力历史
    pressureHistory = pressureHistory.filter(p => {
        const t = new Date(p.time).getTime();
        return isNaN(t) || t > pressureCutoff;
    });
}

// 启动时立即清理一次
runCleanup();
// 每小时检查一次
setInterval(runCleanup, 60 * 60 * 1000);

// ========== HTTP 服务器 + Socket.IO ==========
const app = express();
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, '..', 'web')));

const server = http.createServer(app);
const io = new Server(server, {
    cors: {
        origin: '*'
    }
});

// ---- API 路由 ----

// 获取最新一条数据
app.get('/api/latest', (req, res) => {
    db.get(`SELECT * FROM weather_data ORDER BY id DESC LIMIT 1`, (err, row) => {
        if (err) {
            res.status(500).json({
                error: err.message
            });
        } else {
            res.json(row || {
                message: 'no data'
            });
        }
    });
});

// 获取历史数据 (默认最近1小时，最大24小时)
app.get('/api/history', (req, res) => {
    const hours = Math.min(parseFloat(req.query.hours) || 1, 24);
    const limit = parseInt(req.query.limit) || 500;
    const since = Date.now() - hours * 60 * 60 * 1000;

    db.all(
        `SELECT * FROM weather_data WHERE created_at >= ? ORDER BY id ASC LIMIT ?`,
        [since, limit],
        (err, rows) => {
            if (err) {
                res.status(500).json({
                    error: err.message
                });
            } else {
                res.json(rows);
            }
        }
    );
});

// 获取数据统计
app.get('/api/stats', (req, res) => {
    const hours = Math.min(parseFloat(req.query.hours) || 1, 24);
    const since = Date.now() - hours * 60 * 60 * 1000;

    db.get(
        `SELECT
      COUNT(*) as count,
      MIN(dht11_temp) as temp_min, MAX(dht11_temp) as temp_max, AVG(dht11_temp) as temp_avg,
      MIN(dht11_humidity) as hum_min, MAX(dht11_humidity) as hum_max, AVG(dht11_humidity) as hum_avg,
      MIN(bmp280_pressure) as pres_min, MAX(bmp280_pressure) as pres_max, AVG(bmp280_pressure) as pres_avg,
      MIN(battery_voltage) as batt_min, MAX(battery_voltage) as batt_max, AVG(battery_voltage) as batt_avg,
      MIN(light_mv) as light_min, MAX(light_mv) as light_max, AVG(light_mv) as light_avg,
      SUM(is_rain) as rain_count
     FROM weather_data WHERE created_at >= ?`,
        [since],
        (err, row) => {
            if (err) {
                res.status(500).json({
                    error: err.message
                });
            } else {
                res.json(row);
            }
        }
    );
});

// 获取MQTT连接状态
app.get('/api/status', (req, res) => {
    res.json({
        mqtt_connected: mqttClient.connected,
        mqtt_broker: MQTT_BROKER,
        mqtt_topic: MQTT_TOPIC,
        records_in_history: pressureHistory.length,
        last_packet_time: lastHWPacket ? lastHWPacket.time : null,
        db_path: DB_PATH,
        cleanup_interval_hours: CLEANUP_INTERVAL_HOURS,
        data_pull_enabled: DATA_PULL_ENABLED,
        data_pull_interval_ms: DATA_PULL_INTERVAL_MS,
        data_pull_cmd: DATA_PULL_CMD,
        last_data_received_at: lastDataReceivedAt ? new Date(lastDataReceivedAt).toISOString() : null,
        last_pull_request_at: lastPullRequestAt ? new Date(lastPullRequestAt).toISOString() : null,
        pull_request_count: pullRequestCount,
        data_stale: lastDataReceivedAt ? (Date.now() - lastDataReceivedAt) >= DATA_PULL_INTERVAL_MS : true,
        server_time: new Date().toISOString()
    });
});

// 手动触发主动拉取 (调试/运维用)
app.post('/api/pull', (req, res) => {
    const ok = publishPullRequest('手动触发');
    res.json({
        ok: ok,
        cmd: DATA_PULL_CMD,
        topic: MQTT_TOPIC,
        sent_at: new Date().toISOString()
    });
});

// ---- Socket.IO ----
io.on('connection', (socket) => {
    const clientAddr = socket.handshake.address;
    console.log('[WS] 客户端已连接:', clientAddr, '总连接数:', io.engine.clientsCount);

    // 立即推送最新数据
    if (lastHWPacket) {
        socket.emit('weather_data', lastHWPacket);
    }

    socket.on('disconnect', () => {
        console.log('[WS] 客户端已断开:', clientAddr, '剩余连接数', io.engine.clientsCount);
    });
});

// ========== 启动 ==========
server.listen(HTTP_PORT, () => {
    console.log('========================================');
    // console.log('   天气监测平台后端已启动');
    console.log('========================================');
    console.log('  HTTP/Web 服务:   http://localhost:' + HTTP_PORT);
    console.log('  局域网访问:      ' + (getLanIPs().map(ip => 'http://' + ip + ':' + HTTP_PORT).join('  ') || '(未检测到)'));
    console.log('  Socket.IO:       ws://localhost:' + HTTP_PORT);
    console.log('  MQTT Broker:     ' + MQTT_BROKER);
    console.log('  MQTT 主题:       ' + MQTT_TOPIC);
    console.log('  数据库文件:      ' + DB_PATH);
    console.log('  数据保留周期:    ' + CLEANUP_INTERVAL_HOURS + ' 小时');
    console.log('  数据拉取:      ' + (DATA_PULL_ENABLED ? '启用 (超过 ' + DATA_PULL_INTERVAL_MS + 'ms 无数据则下发 ' + DATA_PULL_CMD + ')' : '禁用'));
    console.log('========================================');
});

// ========== 优雅关闭 ==========
let shuttingDown = false;

function shutdown(signal) {
    if (shuttingDown) return;
    shuttingDown = true;
    console.log('\n[系统] 正在关闭... (信号: ' + signal + ')');
    try {
        mqttClient.end(true);
    } catch (e) {}
    db.close(() => {
        server.close(() => {
            console.log('[系统] 已退出');
            try {
                if (process.connected) process.disconnect();
            } catch (e) {}
            process.exit(0);
        });
        // 兜底 3 秒强制退出
        setTimeout(() => {
            try {
                if (process.connected) process.disconnect();
            } catch (e) {}
            process.exit(0);
        }, 3000);
    });
}

process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
// Electron 子进程断开 IPC 时退出
process.on('disconnect', () => shutdown('DISCONNECT'));
// 父进程 IPC 通道关闭时也退出
process.on('message', (msg) => {
    if (msg === 'shutdown') shutdown('IPC_SHUTDOWN');
});