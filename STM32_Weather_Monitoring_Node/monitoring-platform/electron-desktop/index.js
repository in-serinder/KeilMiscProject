const {
    app,
    BrowserWindow,
    shell,
    Menu,
    dialog
} = require('electron');
const path = require('path');
const fs = require('fs');

const isDev = process.argv.includes('--dev');

let mainWindow = null;

// ========== 资源路径 (开发: web/ 目录; 打包: resources/web) ==========
function getFrontendPath() {
    if (app.isPackaged) {
        return path.join(process.resourcesPath, 'web', 'index.html');
    }
    return path.join(__dirname, '..', 'web', 'index.html');
}

function getIconPath() {
    const p = app.isPackaged ?
        path.join(process.resourcesPath, 'web', 'favicon.png') :
        path.join(__dirname, '..', 'web', 'favicon.png');
    return fs.existsSync(p) ? p : undefined;
}

// ========== 创建窗口 ==========
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
        icon: getIconPath(),
        webPreferences: {
            nodeIntegration: false,
            contextIsolation: true,
            sandbox: true,
            spellcheck: false
        }
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

    mainWindow.on('closed', () => {
        mainWindow = null;
    });

    console.log('[Electron] 加载前端:', getFrontendPath());
    mainWindow.loadFile(getFrontendPath()).catch(err => {
        console.error('[Electron] 加载前端失败:', err);
        dialog.showErrorBox('加载失败', String((err && err.message) || err));
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
                    label: '退出',
                    accelerator: process.platform === 'darwin' ? 'Cmd+Q' : 'Ctrl+Q',
                    click: () => app.quit()
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
                            `Electron: ${process.versions.electron}\n\n` +
                            `这是一个独立的前端客户端，不包含后端服务。\n` +
                            `请在页面底部点击“设置”配置后端服务地址。\n` +
                            `后端需先运行: cd backend && npm start`
                    });
                }
            }]
        }
    ];
    Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

// ========== App 生命周期 ==========
app.on('ready', () => {
    console.log('[Electron] App ready');
    buildMenu();
    createMainWindow();
});

app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') {
        app.quit();
    }
});

app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
        createMainWindow();
    }
});

// 未捕获的异常
process.on('uncaughtException', (err) => {
    console.error('[Electron] Uncaught Exception:', err);
});