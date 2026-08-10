/* =========================================================
 *  天气监测平台前端脚本
 * ========================================================= */

// ========== 配置 ==========
const HISTORY_HOURS_DEFAULT = 1;
const CFG_KEY = 'wmn_config';

let config = loadConfig();
let API_BASE = getApiBase();
let socket = null;

// 默认配置: 浏览器模式取当前页面地址，否则用 localhost:3000
function defaultConfig() {
    const o = window.location.origin;
    if (o && /^https?:/.test(o)) {
        try {
            const u = new URL(o);
            return {
                host: u.hostname,
                port: u.port || (u.protocol === 'https:' ? '443' : '80')
            };
        } catch (e) {
            /* ignore */
        }
    }
    return {
        host: 'localhost',
        port: '3000'
    };
}

function loadConfig() {
    try {
        const raw = localStorage.getItem(CFG_KEY);
        if (raw) {
            const c = JSON.parse(raw);
            if (c && c.host && c.port) return {
                host: String(c.host).trim(),
                port: String(c.port).trim()
            };
        }
    } catch (e) {
        /* ignore */
    }
    return defaultConfig();
}

function saveConfig(host, port) {
    config = {
        host: String(host).trim(),
        port: String(port).trim()
    };
    try {
        localStorage.setItem(CFG_KEY, JSON.stringify(config));
    } catch (e) {
        /* ignore */
    }
    API_BASE = getApiBase();
}

function getApiBase() {
    return `http://${config.host}:${config.port}`;
}

// ========== 预测映射 ==========
const PRED_MAP = {
    0: {
        text: '转晴天',
        sub: 'Sunny Transition',
        icon: '☀️',
        cls: 'w0'
    },
    1: {
        text: '转降雨刮风',
        sub: 'Rain & Wind',
        icon: '🌧️',
        cls: 'w1'
    },
    2: {
        text: '转阴天',
        sub: 'Cloudy',
        icon: '☁️',
        cls: 'w2'
    },
    3: {
        text: '转雷暴强对流',
        sub: 'Thunderstorm',
        icon: '⛈️',
        cls: 'w3'
    },
    4: {
        text: '无效预测',
        sub: 'No Prediction',
        icon: '—',
        cls: 'w4'
    },
};

// ========== 全局状态 ==========
let charts = {};
let currentHours = HISTORY_HOURS_DEFAULT;
let latestData = null;

// ========== DOM 引用 ==========
const $ = (id) => document.getElementById(id);
const ids = {
    temp: 'dht11-temp',
    hum: 'dht11-hum',
    pres: 'bmp-pres',
    btemp: 'bmp-temp',
    light: 'light-mv',
    batt: 'batt-mv',
    ptVolt: 'pt-mv', // 太阳能板电压大字
    feelTemp: 'feel-temp', // 体感温度大字
    soc: 'batt-soc', // 锂电电量%大字
    temps: 'dht11-temp-sub',
    hums: 'dht11-hum-sub',
    press: 'bmp-pres-sub',
    btemps: 'bmp-temp-sub',
    lights: 'light-sub',
    batts: 'batt-sub',
    ptSub: 'pt-sub',
    feelSub: 'feel-sub',
    socSub: 'soc-sub',
    hwIcon: 'hw-icon',
    hwText: 'hw-text',
    swIcon: 'sw-icon',
    swText: 'sw-text',
    rain: 'is-rain',
    night: 'is-night',
    oled: 'oled-pwr',
    chargeState: 'charge-state',
    cid: 'client-id',
    up: 'uptime',
    mqttInd: 'mqtt-ind',
    wsInd: 'ws-ind',
    clock: 'clock',
    lastTime: 'last-time',
    presUpdate: 'pres-update',
    voltUpdate: 'volt-update',
    lightUpdate: 'light-update',
};

// ========== 物理计算工具 ==========
/**
 * 体感温度 (Steadman 简化版，适用于 -10~45°C)
 * 公式: HI = c1 + c2*T + c3*R + c4*T*R + c5*T^2 + c6*R^2 + c7*T^2*R + c8*T*R^2 + c9*T^2*R^2
 * 当 T < 27°C 采用线性混合 (温度+风速简化无风速测量时近似)
 */
function calcFeelTemp(tempC, rhPct) {
    if (typeof tempC !== 'number' || typeof rhPct !== 'number') return null;
    if (!isFinite(tempC) || !isFinite(rhPct)) return null;
    const T = tempC;
    const R = Math.max(0, Math.min(100, rhPct));
    if (T < 26) {
        // 低温段：无风速时，用简单湿度修正 - 每高20%湿度 体感+0.5度(冷天反之)
        const corr = T < 10 ? -((100 - R) / 20) * 0.5 : ((R - 50) / 50) * 1.0;
        return T + corr;
    }
    // Steadman NOAA 热指数 (°C版本，输入华氏转换)
    const TF = T * 9 / 5 + 32;
    const c1 = -42.379,
        c2 = 2.04901523,
        c3 = 10.14333127,
        c4 = -0.22475541,
        c5 = -6.83783e-3,
        c6 = -5.481717e-2,
        c7 = 1.22874e-3,
        c8 = 8.5282e-4,
        c9 = -1.99e-6;
    let HI_F = c1 + c2 * TF + c3 * R + c4 * TF * R + c5 * TF * TF + c6 * R * R +
        c7 * TF * TF * R + c8 * TF * R * R + c9 * TF * TF * R * R;
    const HI_C = (HI_F - 32) * 5 / 9;
    return HI_C;
}

/**
 * 单节锂电池 (LiPo / Li-Ion 3.7V 标称, 4.2V 满, 3.0V 截止) 电量估算 (SoC%)
 * 近似经验映射 (18650 / LiPo 通用保守曲线)
 */
function calcLipoSoc(voltage) {
    if (typeof voltage !== 'number' || !isFinite(voltage)) return null;
    const V = voltage;
    if (V >= 4.19) return 100;
    if (V >= 4.12) return 90 + (V - 4.12) * (10 / 0.07);
    if (V >= 3.98) return 70 + (V - 3.98) * (20 / 0.14);
    if (V >= 3.87) return 50 + (V - 3.87) * (20 / 0.11);
    if (V >= 3.78) return 35 + (V - 3.78) * (15 / 0.09);
    if (V >= 3.70) return 20 + (V - 3.70) * (15 / 0.08);
    if (V >= 3.55) return 10 + (V - 3.55) * (10 / 0.15);
    if (V >= 3.30) return 3 + (V - 3.30) * (7 / 0.25);
    if (V >= 3.00) return Math.max(0, (V - 3.00) * (3 / 0.30));
    return 0;
}

function socClass(soc) {
    if (soc === null || isNaN(soc)) return '';
    if (soc >= 60) return 'soc-high';
    if (soc >= 30) return 'soc-mid';
    if (soc >= 15) return 'soc-low';
    return 'soc-crit';
}

function feelClass(feel, real) {
    if (feel === null || !isFinite(feel)) return '';
    const diff = feel - (real ?? feel);
    if (diff > 2) return 'feel-hot';
    if (diff < -2) return 'feel-cool';
    return '';
}

// ========== 格式化工具 ==========
const fmt = (n, d = 1) => (n === null || n === undefined || isNaN(n)) ? '--' : Number(n).toFixed(d);
const fmtInt = (n) => (n === null || n === undefined || isNaN(n)) ? '--' : Math.round(n).toString();

function fmtUptime(s) {
    if (!s || isNaN(s)) return '--';
    s = parseInt(s);
    if (s < 60) return s + 's';
    if (s < 3600) return Math.floor(s / 60) + 'm ' + (s % 60) + 's';
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    if (h < 24) return h + 'h ' + m + 'm';
    const d = Math.floor(h / 24);
    return d + 'd ' + (h % 24) + 'h';
}

function setFlash(el) {
    el.classList.add('flash');
    setTimeout(() => el.classList.remove('flash'), 400);
}

// ========== 时钟 ==========
function updateClock() {
    const d = new Date();
    const pad = (n) => String(n).padStart(2, '0');
    $(ids.clock).textContent = `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}
setInterval(updateClock, 1000);
updateClock();

// ========== 大字数据更新 ==========
function updateBigNumbers(d, stats) {
    const setVal = (idKey, val, dKey, extraClass) => {
        const el = $(ids[idKey]);
        if (!el) return;
        const old = el.textContent;
        el.textContent = val;
        // 先清除可能的状态类
        el.classList.remove('soc-high', 'soc-mid', 'soc-low', 'soc-crit', 'feel-hot', 'feel-cool', 'pt-charge', 'pt-idle');
        if (extraClass) el.classList.add(extraClass);
        if (old !== val && old !== '--') setFlash(el);
        if (dKey && stats) {
            const sub = $(ids[dKey]);
            if (sub) {
                const k = idKey.includes('dht11') ? 'temp' : idKey.includes('bmp') ?
                    (idKey.includes('pres') ? 'pres' : 'temp') :
                    idKey.includes('hum') ? 'hum' :
                    idKey.includes('light') ? 'light' :
                    idKey.includes('batt') ? 'batt' :
                    null;
                if (k && stats[`${k}_min`] !== undefined) {
                    sub.textContent = `min ${fmt(stats[`${k}_min`])} · max ${fmt(stats[`${k}_max`])} · avg ${fmt(stats[`${k}_avg`])}`;
                }
            }
        }
    };

    setVal('temp', fmt(d.dht11_temp, 1), 'temps');
    setVal('hum', fmt(d.dht11_humidity, 0), 'hums');
    setVal('pres', fmt(d.bmp280_pressure, 1), 'press');
    setVal('btemp', fmt(d.bmp280_temp, 1), 'btemps');
    setVal('light', fmtInt(d.light_mv), 'lights');
    setVal('batt', fmt(d.battery_voltage, 2), 'batts');

    // 太阳能板电压 (PT)
    const ptV = d.pt_voltage;
    setVal('ptVolt', fmt(ptV, 2), 'ptSub',
        (typeof ptV === 'number' && ptV > 3.5) ? 'pt-charge' : 'pt-idle');
    if ($(ids.ptSub)) {
        if (typeof ptV === 'number') {
            const charging = ptV > d.battery_voltage + 0.10;
            $(ids.ptSub).textContent = charging ? '光伏充电中' : (ptV > 4.5 ? '光伏板在线' : '弱光/夜间');
        } else {
            $(ids.ptSub).textContent = '等待数据...';
        }
    }

    // 体感温度
    const feel = calcFeelTemp(d.dht11_temp, d.dht11_humidity);
    setVal('feelTemp', feel === null ? '--' : fmt(feel, 1), 'feelSub',
        feelClass(feel, d.dht11_temp));
    if ($(ids.feelSub) && feel !== null) {
        const diff = feel - d.dht11_temp;
        const mark = diff > 0.5 ? '↑ 闷热' : (diff < -0.5 ? '↓ 干爽' : '≈ 接近');
        $(ids.feelSub).textContent = `实测 ${fmt(d.dht11_temp,1)}°C · 差异 ${diff>=0?'+':''}${fmt(diff,1)}°C (${mark})`;
    }

    // 锂电电量估算
    const soc = calcLipoSoc(d.battery_voltage);
    setVal('soc', soc === null ? '--' : Math.round(soc).toString(), 'socSub', socClass(soc));
    if ($(ids.socSub) && soc !== null) {
        let level = '充足';
        if (soc < 15) level = '告急';
        else if (soc < 30) level = '偏低';
        else if (soc < 60) level = '中等';
        $(ids.socSub).textContent = `电压 ${fmt(d.battery_voltage,2)}V · ${level}`;
    }
}

// ========== 预测更新 ==========
function updatePrediction(hw, sw) {
    const h = PRED_MAP[hw] || PRED_MAP[4];
    const s = PRED_MAP[sw] || PRED_MAP[4];
    $(ids.hwIcon).textContent = h.icon;
    $(ids.hwIcon).className = 'pred-icon ' + h.cls;
    $(ids.hwText).innerHTML = `<div class="pt-main">${h.text}</div><div class="pt-sub">${h.sub} · code=${hw}</div>`;

    $(ids.swIcon).textContent = s.icon;
    $(ids.swIcon).className = 'pred-icon ' + s.cls;
    $(ids.swText).innerHTML = `<div class="pt-main">${s.text}</div><div class="pt-sub">${s.sub} · code=${sw}</div>`;
}

// ========== 状态更新 ==========
function updateStates(d) {
    const setBool = (idKey, val) => {
        const el = $(ids[idKey]);
        if (!el) return;
        el.textContent = val ? '是' : '否';
        el.className = 'state-value ' + (val ? 'yes' : 'no');
    };
    setBool('rain', !!d.is_rain);
    setBool('night', !!d.is_night);
    setBool('oled', !!d.oled_power);

    // 充电状态 (根据 PT 电压 > 锂电电压 + 0.1V 判断光伏充电中)
    const csEl = $(ids.chargeState);
    if (csEl) {
        const pt = typeof d.pt_voltage === 'number' ? d.pt_voltage : NaN;
        const bt = typeof d.battery_voltage === 'number' ? d.battery_voltage : NaN;
        const charging = isFinite(pt) && isFinite(bt) && pt > bt + 0.10;
        const weakLight = isFinite(pt) && pt < 1.0;
        csEl.classList.remove('yes', 'no');
        if (weakLight) {
            csEl.textContent = '夜间';
            csEl.className = 'state-value no';
        } else if (charging) {
            csEl.textContent = '充电中';
            csEl.className = 'state-value yes';
        } else {
            csEl.textContent = '待机';
            csEl.className = 'state-value';
        }
    }

    $(ids.cid).textContent = d.clientid || '--';
    $(ids.up).textContent = fmtUptime(d.uptime);
    $(ids.lastTime).textContent = d.time || '--';
    const now = new Date().toLocaleTimeString();
    $(ids.presUpdate).textContent = '最后更新: ' + now;
    $(ids.voltUpdate).textContent = '最后更新: ' + now;
    $(ids.lightUpdate).textContent = '最后更新: ' + now;
}

// ========== Chart.js 通用配置 ==========
Chart.defaults.color = '#8f95a0';
Chart.defaults.borderColor = '#2e323a';
Chart.defaults.font.family = '"Consolas", "Segoe UI", monospace';

function createLineChart(ctx, datasets, yAxisConfig) {
    return new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: datasets.map(ds => ({
                label: ds.label,
                data: [],
                borderColor: ds.color,
                backgroundColor: ds.bgColor || (ds.color + '1a'),
                borderWidth: 2,
                pointRadius: 0,
                pointHoverRadius: 4,
                pointHoverBorderWidth: 2,
                tension: 0.25,
                fill: !!ds.fill,
                yAxisID: ds.yAxisID || 'y',
                borderDash: ds.borderDash || undefined,
            }))
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            interaction: {
                mode: 'index',
                intersect: false
            },
            plugins: {
                legend: {
                    display: true,
                    position: 'top',
                    align: 'end',
                    labels: {
                        boxWidth: 12,
                        boxHeight: 3,
                        padding: 16,
                        font: {
                            size: 11
                        }
                    }
                },
                tooltip: {
                    backgroundColor: '#13151a',
                    borderColor: '#2e323a',
                    borderWidth: 1,
                    titleColor: '#e8eaee',
                    bodyColor: '#d7d9de',
                    padding: 10,
                    cornerRadius: 0,
                }
            },
            scales: {
                x: {
                    grid: {
                        display: false
                    },
                    ticks: {
                        maxTicksLimit: 8,
                        font: {
                            size: 10
                        }
                    }
                },
                ...yAxisConfig
            }
        }
    });
}

// ========== 图表初始化 ==========
function initCharts() {
    charts.th = createLineChart($('chart-th'), [{
            label: '温度 °C',
            color: '#ff7b7b',
            fill: true,
            yAxisID: 'y'
        },
        {
            label: '湿度 %',
            color: '#7cd1f9',
            fill: false,
            yAxisID: 'y1'
        },
    ], {
        y: {
            type: 'linear',
            position: 'left',
            grid: {
                color: '#2e323a'
            },
            ticks: {
                font: {
                    size: 10
                }
            }
        },
        y1: {
            type: 'linear',
            position: 'right',
            grid: {
                display: false
            },
            ticks: {
                font: {
                    size: 10
                }
            }
        },
    });

    charts.pres = createLineChart($('chart-pres'), [{
            label: '压强 hPa',
            color: '#b980f0',
            fill: true,
            yAxisID: 'y'
        },
        {
            label: '芯片温度 °C',
            color: '#4fb3ff',
            fill: false,
            yAxisID: 'y1'
        },
    ], {
        y: {
            type: 'linear',
            position: 'left',
            grid: {
                color: '#2e323a'
            },
            ticks: {
                font: {
                    size: 10
                }
            }
        },
        y1: {
            type: 'linear',
            position: 'right',
            grid: {
                display: false
            },
            ticks: {
                font: {
                    size: 10
                }
            }
        },
    });

    charts.volt = createLineChart($('chart-volt'), [{
            label: '锂电电压 V',
            color: '#3ddc84',
            fill: true,
            yAxisID: 'y'
        },
        {
            label: '太阳能板 V',
            color: '#ff9f43',
            fill: false,
            yAxisID: 'y1'
        },
    ], {
        // 锂电: 独立左轴, 收窄范围让 3.3-4.2V 的充放电波动清晰可见
        y: {
            type: 'linear',
            position: 'left',
            min: 2.9,
            max: 4.3,
            grid: {
                color: '#2e323a'
            },
            ticks: {
                font: {
                    size: 10
                }
            },
            title: {
                display: true,
                text: '锂电 (V)',
                color: '#3ddc84',
                font: {
                    size: 10
                }
            }
        },
        // 光伏: 独立右轴, 覆盖 0-6V 保证夜间 0V 与白天峰值都可见
        y1: {
            type: 'linear',
            position: 'right',
            min: 0,
            max: 6,
            grid: {
                display: false
            },
            ticks: {
                font: {
                    size: 10
                }
            },
            title: {
                display: true,
                text: '光伏 (V)',
                color: '#ff9f43',
                font: {
                    size: 10
                }
            }
        }
    });

    charts.light = createLineChart($('chart-light'), [{
            label: '电量 %',
            color: '#ff6b9d',
            fill: false,
            yAxisID: 'y',
            borderDash: [4, 4]
        },
        {
            label: '光照 mV',
            color: '#feca57',
            fill: true,
            yAxisID: 'y1'
        },
    ], {
        y: {
            type: 'linear',
            position: 'left',
            grid: {
                color: '#2e323a'
            },
            ticks: {
                font: {
                    size: 10
                },
                callback: v => v + '%'
            },
            min: 0,
            max: 100,
            title: {
                display: true,
                text: '%',
                color: '#8f95a0',
                font: {
                    size: 10
                }
            }
        },
        y1: {
            type: 'linear',
            position: 'right',
            grid: {
                display: false
            },
            ticks: {
                font: {
                    size: 10
                }
            },
            title: {
                display: true,
                text: 'mV',
                color: '#8f95a0',
                font: {
                    size: 10
                }
            }
        }
    });
}

// ========== 加载历史数据并填充图表 ==========
async function loadHistory(hours = 1) {
    try {
        const [hR, sR] = await Promise.all([
            fetch(`${API_BASE}/api/history?hours=${hours}&limit=1000`),
            fetch(`${API_BASE}/api/stats?hours=${hours}`),
        ]);
        const rows = await hR.json();
        const stats = await sR.json();

        if (!Array.isArray(rows)) {
            console.warn('history bad', rows);
            return;
        }

        const labels = rows.map(r => {
            const t = r.time || (r.created_at && new Date(r.created_at).toLocaleString());
            if (!t) return '';
            // time 可能是 "YYYY-MM-DD HH:MM:SS"，只取 HH:MM:SS
            const m = String(t).match(/(\d{2}:\d{2}:\d{2})/);
            return m ? m[1] : String(t).substr(11, 8);
        });

        function pushDS(chartIdx, keyMap) {
            const chart = Object.values(charts)[chartIdx];
            chart.data.labels = labels;
            keyMap.forEach((srcKey, dsIdx) => {
                chart.data.datasets[dsIdx].data = rows.map(r => r[srcKey]);
            });
            chart.update('none');
        }

        pushDS(0, ['dht11_temp', 'dht11_humidity']);
        pushDS(1, ['bmp280_pressure', 'bmp280_temp']);
        // chart-volt: 锂电电压 / 太阳能板电压 (电压变动)
        (() => {
            const chart = charts.volt;
            chart.data.labels = labels;
            chart.data.datasets[0].data = rows.map(r => r.battery_voltage);
            chart.data.datasets[1].data = rows.map(r => r.pt_voltage);
            chart.update('none');
        })();
        // chart-light: 电量% / 光照mV
        (() => {
            const chart = charts.light;
            chart.data.labels = labels;
            chart.data.datasets[0].data = rows.map(r => calcLipoSoc(r.battery_voltage));
            chart.data.datasets[1].data = rows.map(r => r.light_mv);
            chart.update('none');
        })();

        // 如果有最新数据，更新大字的统计子文本
        if (latestData) updateBigNumbers(latestData, stats);

        console.log(`[HISTORY] 已加载 ${rows.length} 条数据 (${hours}h)`);
    } catch (e) {
        console.error('[HISTORY] 加载失败:', e);
    }
}

// ========== 追加实时数据点到图表 ==========
const MAX_CHART_POINTS = 600;

function appendRealtime(d) {
    const t = d.time || new Date().toLocaleString();
    const m = String(t).match(/(\d{2}:\d{2}:\d{2})/);
    const label = m ? m[1] : String(t).substr(11, 8);

    function addTo(chartIdx, values) {
        const chart = Object.values(charts)[chartIdx];
        chart.data.labels.push(label);
        values.forEach((v, i) => chart.data.datasets[i].data.push(v));
        if (chart.data.labels.length > MAX_CHART_POINTS) {
            chart.data.labels.shift();
            values.forEach((_, i) => chart.data.datasets[i].data.shift());
        }
        chart.update('none');
    }

    addTo(0, [d.dht11_temp, d.dht11_humidity]);
    addTo(1, [d.bmp280_pressure, d.bmp280_temp]);
    addTo(2, [d.battery_voltage, d.pt_voltage]);
    addTo(3, [calcLipoSoc(d.battery_voltage), d.light_mv]);
}

// ========== MQTT 连接状态轮询 ==========
async function pollStatus() {
    try {
        const r = await fetch(`${API_BASE}/api/status`);
        const s = await r.json();
        $(ids.mqttInd).className = 'status-indicator ' + (s.mqtt_connected ? 'ok' : 'bad');
        // 底部信息随后端实际配置更新
        const t = $('mqtt-topic');
        if (t && s.mqtt_topic) t.textContent = s.mqtt_topic;
        const b = $('mqtt-broker');
        if (b && s.mqtt_broker) b.textContent = String(s.mqtt_broker).replace(/^mqtt:\/\//, '');
        const rt = $('retention');
        if (rt && s.cleanup_interval_hours) rt.textContent = s.cleanup_interval_hours + 'h';
    } catch (_) {
        $(ids.mqttInd).className = 'status-indicator bad';
    }
}

// ========== Socket.IO ==========
function connectSocket() {
    socket = io(API_BASE, {
        transports: ['websocket', 'polling']
    });

    socket.on('connect', () => {
        console.log('[WS] 已连接');
        $(ids.wsInd).className = 'status-indicator ok';
    });
    socket.on('disconnect', () => {
        console.log('[WS] 已断开');
        $(ids.wsInd).className = 'status-indicator bad';
    });
    socket.on('connect_error', () => {
        $(ids.wsInd).className = 'status-indicator bad';
    });

    socket.on('weather_data', async (d) => {
        latestData = d;
        updateBigNumbers(d);
        updatePrediction(d.weather_prediction, d.sw_pressure_prediction);
        updateStates(d);
        appendRealtime(d);
    });

    return socket;
}

// ========== 时间范围按钮 ==========
function bindRangeBtns() {
    document.querySelectorAll('.range-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.range-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            const h = parseFloat(btn.dataset.hours);
            currentHours = h;
            loadHistory(h);
        });
    });
}

// ========== 连接管理 ==========
function updateServerAddr() {
    const el = $('server-addr');
    if (el) el.textContent = `${config.host}:${config.port}`;
}

// 重新连接 (初始化与配置变更后调用)
async function reconnect() {
    API_BASE = getApiBase();
    updateServerAddr();

    // 断开旧连接
    if (socket) {
        try {
            socket.disconnect();
        } catch (e) {
            /* ignore */
        }
        socket = null;
    }
    $(ids.wsInd).className = 'status-indicator bad';
    $(ids.mqttInd).className = 'status-indicator bad';

    // 拉取最新数据
    try {
        const r = await fetch(`${API_BASE}/api/latest`);
        const d = await r.json();
        if (d && d.id) {
            latestData = d;
            updateBigNumbers(d);
            updatePrediction(d.weather_prediction, d.sw_pressure_prediction);
            updateStates(d);
        }
    } catch (e) {
        console.error('[RECONNECT] 获取最新数据失败:', e);
    }

    await loadHistory(currentHours);
    connectSocket();
    pollStatus();
}

// ========== 设置弹窗 ==========
function bindSettings() {
    const modal = $('settings-modal');
    if (!modal) return;
    const hostInput = $('cfg-host');
    const portInput = $('cfg-port');
    const preview = $('cfg-preview');
    const msg = $('cfg-msg');

    function show() {
        hostInput.value = config.host;
        portInput.value = config.port;
        updatePreview();
        msg.textContent = '';
        msg.className = 'modal-msg';
        modal.hidden = false;
    }

    function hide() {
        modal.hidden = true;
    }

    function updatePreview() {
        const h = hostInput.value.trim() || 'localhost';
        const p = portInput.value.trim() || '3000';
        preview.textContent = `http://${h}:${p}`;
    }

    $('settings-btn').addEventListener('click', show);
    $('modal-close').addEventListener('click', hide);
    $('cfg-cancel').addEventListener('click', hide);
    modal.addEventListener('click', (e) => {
        if (e.target === modal) hide();
    });
    hostInput.addEventListener('input', updatePreview);
    portInput.addEventListener('input', updatePreview);

    $('cfg-test').addEventListener('click', async () => {
        const h = hostInput.value.trim() || 'localhost';
        const p = portInput.value.trim() || '3000';
        msg.textContent = '测试中...';
        msg.className = 'modal-msg';
        const ctrl = new AbortController();
        const timer = setTimeout(() => ctrl.abort(), 4000);
        try {
            const r = await fetch(`http://${h}:${p}/api/status`, {
                signal: ctrl.signal
            });
            const s = await r.json();
            msg.textContent = `连接成功 · MQTT ${s.mqtt_connected ? '已连接' : '未连接'} · 主题 ${s.mqtt_topic || '-'}`;
            msg.className = 'modal-msg ok';
        } catch (e) {
            msg.textContent = '连接失败: ' + (e.name === 'AbortError' ? '超时' : e.message);
            msg.className = 'modal-msg err';
        } finally {
            clearTimeout(timer);
        }
    });

    $('cfg-save').addEventListener('click', async () => {
        const h = hostInput.value.trim();
        const p = portInput.value.trim();
        if (!h) {
            msg.textContent = '请填写服务器地址';
            msg.className = 'modal-msg err';
            return;
        }
        if (!/^\d+$/.test(p) || +p < 1 || +p > 65535) {
            msg.textContent = '端口无效 (1-65535)';
            msg.className = 'modal-msg err';
            return;
        }
        saveConfig(h, p);
        hide();
        await reconnect();
    });
}

// ========== 初始化 ==========
async function init() {
    initCharts();
    bindRangeBtns();
    bindSettings();
    updateServerAddr();
    await reconnect();
    setInterval(pollStatus, 10000);
}

document.addEventListener('DOMContentLoaded', init);