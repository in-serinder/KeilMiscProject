// 临时诊断脚本：加载 http://localhost:3000 并抓取渲染进程 console 错误
const electron = require('electron');
const app = electron.app;
const BrowserWindow = electron.BrowserWindow;
const path = require('path');

const URL = 'http://localhost:3000';
const WAIT_MS = 8000;

try {
    app.disableHardwareAcceleration();
} catch (e) {
    console.log('[TEST] disableHW failed:', e.message);
}

app.on('ready', async () => {
    const win = new BrowserWindow({
        width: 1440,
        height: 900,
        show: false,
        webPreferences: {
            nodeIntegration: false,
            contextIsolation: true,
            sandbox: true,
            spellcheck: false
        }
    });

    const logs = [];

    win.webContents.on('console-message', (e, level, message, line, sourceId) => {
        logs.push(`[CONSOLE:${level}] ${message} (${sourceId}:${line})`);
    });
    win.webContents.on('did-fail-load', (e, code, desc, url) => {
        logs.push(`[FAIL-LOAD] code=${code} desc=${desc} url=${url}`);
    });
    win.webContents.on('render-process-gone', (e, details) => {
        logs.push(`[RENDER-GONE] ${JSON.stringify(details)}`);
    });
    win.webContents.on('did-finish-load', () => logs.push('[DID-FINISH-LOAD]'));
    win.webContents.on('did-stop-loading', () => logs.push('[DID-STOP-LOADING]'));

    try {
        await win.loadURL(URL);
    } catch (err) {
        logs.push('[LOAD-ERR] ' + err.message);
    }

    await new Promise(r => setTimeout(r, WAIT_MS));

    // 额外抓取页面当前状态
    try {
        const state = await win.webContents.executeJavaScript(`JSON.stringify({
            hasApp: !!document.querySelector('.app'),
            chartVolt: !!document.getElementById('chart-volt'),
            chartLight: !!document.getElementById('chart-light'),
            chartMisc: !!document.getElementById('chart-misc'),
            tempVal: (document.getElementById('dht11-temp')||{}).textContent,
            voltUpd: (document.getElementById('volt-update')||{}).textContent,
            lightUpd: (document.getElementById('light-update')||{}).textContent,
            bodyLen: document.body.innerHTML.length
        })`);
        logs.push('[PAGE-STATE] ' + state);
    } catch (err) {
        logs.push('[STATE-ERR] ' + err.message);
    }

    console.log('=========== RENDERER DIAGNOSTIC ===========');
    logs.forEach(l => console.log(l));
    console.log('===========================================');
    app.exit(0);
});

setTimeout(() => {
    console.log('TIMEOUT - forcing exit');
    app.exit(1);
}, WAIT_MS + 15000);