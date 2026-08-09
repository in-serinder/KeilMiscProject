const {
    app,
    BrowserWindow,
    shell,
    Menu,
    dialog
} = require('electron');
const path = require('path');
const {
    fork
} = require('child_process');

// ========== 配置 ==========
const HTTP_PORT = 3000;
const BACKEND_ENTRY = path.join(__dirname, '..', 'backend', 'main.js');
const FRONTEND_URL = `http://localhost:${HTTP_PORT}`;

const isDev = process.argv.includes('--dev');

// ========== 全局变量 ==========
let mainWindow = null;
let backendProcess = null;
let backendReady = false;

// ========== 启动后端服务器（子进程） ==========
function startBackend() {
    console.log('[Electron] 正在启动后端服务...');

    // 将当前 electron-desktop/node_modules 加入 NODE_PATH，让 backend/main.js 能找到依赖
    const nodePath = process.env.NODE_PATH ?
        process.env.NODE_PATH + path.delimiter + path.join(__dirname, 'node_modules') :
        path.join(__dirname, 'node_modules');

    const env = {
        ...process.env,
        NODE_PATH: nodePath,
        ELECTRON_WRAPPED: '1'
    };

    backendProcess = fork(BACKEND_ENTRY, [], {
        env: env,
        stdio: ['pipe', 'pipe', 'pipe', 'ipc'],
        cwd: path.dirname(BACKEND_ENTRY),
        windowsHide: true
    });

    // 监听 stdout/stderr
    backendProcess.stdout.on('data', (data) => {
        const msg = data.toString().trim();
        if (msg) {
            console.log('[BACKEND]', msg);
            // 检测服务是否就绪
            if (msg.includes('HTTP/Web 服务') || msg.includes('localhost:' + HTTP_PORT) || msg.includes('已启动')) {
                if (!backendReady) {
                    backendReady = true;
                    console.log('[Electron] 后端就绪，加载前端...');
                    loadFrontend();
                }
            }
        }
    });

    backendProcess.stderr.on('data', (data) => {
        console.error('[BACKEND_ERR]', data.toString().trim());
    });

    backendProcess.on('message', (msg) => {
        console.log('[BACKEND_IPC]', msg);
    });

    backendProcess.on('exit', (code, signal) => {
        console.log(`[Electron] 后端进程退出 code=${code} signal=${signal}`);
        backendReady = false;
        if (!app.isQuiting) {
            // 如果不是正常退出，尝试重启或者提示用户
            dialog.showErrorBox(
                '后端服务异常退出',
                `后端服务进程已退出 (code=${code}). 应用即将关闭，请重新启动。`
            );
            quitApp();
        }
    });

    backendProcess.on('error', (err) => {
        console.error('[Electron] 后端进程错误:', err);
        dialog.showErrorBox('无法启动后端服务', err.message || String(err));
        quitApp();
    });

    // 兜底：如果 stdout 中没有检测到就绪信号，3秒后强制加载
    setTimeout(() => {
        if (!backendReady) {
            console.log('[Electron] 就绪信号超时，尝试加载...');
            backendReady = true;
            loadFrontend();
        }
    }, 5000);
}

// ========== 创建窗口并加载前端 ==========
function loadFrontend() {
    if (!mainWindow) {
        createMainWindow();
    }
    if (mainWindow) {
        mainWindow.loadURL(FRONTEND_URL).catch(err => {
            console.error('[Electron] 加载失败:', err);
            // 重试一次
            setTimeout(() => {
                if (mainWindow && !mainWindow.isDestroyed()) {
                    mainWindow.loadURL(FRONTEND_URL).catch(() => {});
                }
            }, 1500);
        });
    }
}

function createMainWindow() {
    mainWindow = new BrowserWindow({
        width: 1440,
        height: 900,
        minWidth: 1024,
        minHeight: 700,
        backgroundColor: '#1c1e22',
        title: '天气监测节点 · Monitoring Platform',
        autoHideMenuBar: true,
        show: false,
        webPreferences: {
            nodeIntegration: false,
            contextIsolation: true,
            sandbox: true,
            spellcheck: false
        },
        icon: path.join(__dirname, '..', 'web', 'favicon.png')
    });

    mainWindow.once('ready-to-show', () => {
        mainWindow.show();
        if (isDev) mainWindow.webContents.openDevTools({
            mode: 'detach'
        });
    });

    // 外链在浏览器中打开
    mainWindow.webContents.setWindowOpenHandler(({
        url
    }) => {
        shell.openExternal(url);
        return {
            action: 'deny'
        };
    });

    // 阻止拖拽导航
    mainWindow.webContents.on('will-navigate', (e, url) => {
        if (!url.startsWith(FRONTEND_URL) && !url.startsWith('http://localhost:' + HTTP_PORT)) {
            e.preventDefault();
        }
    });

    mainWindow.on('closed', () => {
        mainWindow = null;
    });
}

// ========== 菜单 ==========
function buildMenu() {
    const template = [{
            label: '文件',
            submenu: [{
                    label: '重新加载',
                    accelerator: 'F5',
                    click: () => mainWindow && mainWindow.reload()
                },
                {
                    label: '强制重载',
                    accelerator: 'Ctrl+F5',
                    click: () => mainWindow && mainWindow.webContents.reloadIgnoringCache()
                },
                {
                    type: 'separator'
                },
                {
                    label: '在浏览器中打开',
                    click: () => shell.openExternal(FRONTEND_URL)
                },
                {
                    type: 'separator'
                },
                {
                    label: '退出',
                    accelerator: process.platform === 'darwin' ? 'Cmd+Q' : 'Ctrl+Q',
                    click: () => quitApp()
                }
            ]
        },
        {
            label: '视图',
            submenu: [{
                    label: '开发者工具',
                    accelerator: process.platform === 'darwin' ? 'Cmd+Alt+I' : 'F12',
                    click: () => mainWindow && mainWindow.webContents.toggleDevTools()
                },
                {
                    type: 'separator'
                },
                {
                    label: '实际大小',
                    accelerator: 'Ctrl+0',
                    click: () => mainWindow && mainWindow.webContents.setZoomLevel(0)
                },
                {
                    label: '放大',
                    accelerator: 'Ctrl++',
                    click: () => mainWindow && mainWindow.webContents.setZoomLevel(mainWindow.webContents.getZoomLevel() + 0.5)
                },
                {
                    label: '缩小',
                    accelerator: 'Ctrl+-',
                    click: () => mainWindow && mainWindow.webContents.setZoomLevel(mainWindow.webContents.getZoomLevel() - 0.5)
                },
                {
                    type: 'separator'
                },
                {
                    label: '全屏',
                    accelerator: 'F11',
                    click: () => mainWindow && mainWindow.setFullScreen(!mainWindow.isFullScreen())
                }
            ]
        },
        {
            label: '帮助',
            submenu: [{
                label: '关于',
                click: () => {
                    dialog.showMessageBox(mainWindow, {
                        type: 'info',
                        title: '关于',
                        message: '天气监测节点 · Monitoring Platform',
                        detail: `版本: ${app.getVersion()}\n` +
                            `Electron: ${process.versions.electron}\n` +
                            `Node.js: ${process.versions.node}\n\n` +
                            `MQTT Broker: 8.130.191.142:1883\n` +
                            `Topic: IOTGP/WMN\n` +
                            `数据保留周期: 72 小时\n\n` +
                            `后端端口: ${HTTP_PORT}\n` +
                            `数据库: SQLite (weather.db)`
                    });
                }
            }]
        }
    ];
    const menu = Menu.buildFromTemplate(template);
    Menu.setApplicationMenu(menu);
}

// ========== 退出清理 ==========
function quitApp() {
    app.isQuiting = true;

    // 1. 关闭窗口
    try {
        if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.removeAllListeners('close');
            mainWindow.close();
        }
    } catch (e) {}

    // 2. 结束后端进程
    if (backendProcess && !backendProcess.killed) {
        console.log('[Electron] 正在终止后端进程...');
        try {
            // 发送 SIGINT 让后端优雅关闭（Windows 下 child_process 会模拟）
            backendProcess.removeAllListeners('exit');
            backendProcess.kill('SIGINT');
            // 兜底 2 秒后强制 kill
            setTimeout(() => {
                if (backendProcess && !backendProcess.killed) {
                    backendProcess.kill('SIGKILL');
                }
                app.quit();
            }, 2000);
            return;
        } catch (e) {
            console.error(e);
        }
    }

    app.quit();
}

// ========== App 生命周期 ==========
app.on('ready', () => {
    console.log('[Electron] App ready');
    console.log('[Electron] 后端入口:', BACKEND_ENTRY);
    console.log('[Electron] NODE_PATH +=', path.join(__dirname, 'node_modules'));

    buildMenu();
    startBackend();
});

app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') {
        quitApp();
    }
});

app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
        if (backendReady) {
            loadFrontend();
        }
    }
});

app.on('before-quit', (e) => {
    if (!app.isQuiting) {
        e.preventDefault();
        quitApp();
    }
});

// 未捕获的异常
process.on('uncaughtException', (err) => {
    console.error('[Electron] Uncaught Exception:', err);
});