# 跨平台改造记录（Windows + Linux）

上位机 `Capsa` 的 Linux 兼容改造。目标是**单一代码库、单一构建产物形态**同时服务 Windows 与 Linux，
不为任何平台开分支、复制服务实现或引入第二套 UI。

- 技术栈不变：.NET 8 + Avalonia 11.3.12 + System.IO.Ports（Avalonia 自带 X11/FreeDesktop 后端）
- 改造原则：**平台差异只在"语义"层收敛，不做整层抽象**
- 改造前状态：`Avalonia` 侧本来就跨平台，真正的耦合只有三类——文件名/路径语义、串口错误语义、端口来源

---

## 1. 结论与平台矩阵

| 能力 | Windows | Linux（改造前） | Linux（改造后） |
| --- | --- | --- | --- |
| 启动 / 界面 / 全部业务逻辑 | ✅ | ✅ | ✅ |
| `--self-test` 纯逻辑自检 | ✅ | ❌ 2 条 FAIL | ✅ 全 PASS |
| 串口枚举 | ✅ | ⚠️ 能列，但描述全是 `Serial port` | ✅ 带 `USB Serial (1a86:7523)` 描述 |
| 连接 / 读写 / 校验 / 心跳 | ✅ | ✅ | ✅ |
| 端口被占用的提示 | ✅ 准确 | ❌ 误报为"串口助手占用" | ✅ 区分"无权限/不存在/被占用" |
| 手动指定端口（by-id、pts、容器） | ❌ | ❌ 只能从下拉选 | ✅ 可手输 |
| 构建产物 | `Capsa.exe` | `dotnet Capsa.dll` / `dotnet publish -r linux-x64` | 同左 |

---

## 2. 逐条修改记录

### 2.1 文件名与路径去平台化 — `Models/FilePayloadCodec.cs`

**问题**：`SanitizeFileName` 依赖 `Path.GetFileName` 和 `Path.GetInvalidFileNameChars()`。
Unix 上 `'\'` 不是分隔符、非法字符集只有 `\0` 和 `/`，于是：

- `C:\tmp\a.bin` 不会被剥成 `a.bin`（整串带着盘符写进 EEPROM 的 64 B 名字区）
- `a<b>.bin` 不会被转成 `a_b_.bin`

直接后果是 `--self-test` 的 `path stripped`、`invalid chars replaced` 两条在 Linux 必然 FAIL，
同一份数据的文件名在两个平台读回不一样。

**改法**：不再问 `Path`，自己定义规则 ——

- 分隔符固定为 `/` 和 `\`，取最后一个分隔符之后的部分
- 非法字符固定为 Windows 那一套 `< > : " / \ | ? *`（Unix 是它的子集），保证两端**得到同一结果**

### 2.2 串口错误语义分类 — `Models/PortFailure.cs`（新增）

**问题**：代码里把 `UnauthorizedAccessException` 当成"端口被占用"，这只在 Windows 成立
（串口独占）。Linux 上 `open()` 失败抛的是**同一个类型**，但含义通常是"权限不足"。
用户被提示去关一个根本没开着的串口助手，排查方向从一开始就是错的。

**改法**：新增 `PortFailureKind`（`PermissionDenied / PortInUse / DeviceRemoved / BadConfiguration / Io / Unknown`）
与 `PortFailure.Classify(ex, portName)`，把异常翻译成**平台正确的**单行文案。调用点：

| 位置 | 用途 |
| --- | --- |
| `CapsaSerialPortService.ProbeAsync` | 探测失败原因（状态栏 + 弹窗） |
| `CapsaSerialPortService.ConnectAsync` | 打开失败转成带平台文案的 `InvalidOperationException`（原异常留在 `InnerException`，ErrorLog 仍有堆栈） |
| `CapsaSerialPortService.HeartbeatTick` | 用 `IsFatal` 取代 `ex is UnauthorizedAccessException or IOException` 的硬判定 |
| `CapsaProtocol.ReadFrame` | `LastPortError` 改成分类后的文案 |
| `ProtocolSmokeTest` | 日志里先打一句人话，再打完整堆栈 |

**实测踩到的坑（重要）**：.NET 在 Unix 上把 `open()` 的**所有**失败都包成 `UnauthorizedAccessException`，
设备根本不存在（ENOENT）也是。所以分类前先 `File.Exists(port)`：

```
FAILED ttyUSB9 不存在（设备未插入，或端口名拼错）
EXCEPTION System.UnauthorizedAccessException: Access to the port '/dev/ttyUSB9' is denied.
          ---> System.IO.IOException: No such file or directory
```

不先判断存在性，就会把"没插线"说成"没权限"。

**追加修正（组名不能写死）：** 第一版把 `dialout` 写死在"权限不足"的文案里。
Debian/Ubuntu/Fedora 确实如此，但 **Arch / Manjaro / openSUSE 用的是 `uucp`，根本没有 `dialout` 组**，
用户照抄命令会得到 `usermod：“dialout”组不存在`。现由
`Models/UnixSerialGroup.cs`（新增）在运行时读 `/etc/group`：

- 按 `dialout → uucp` 顺序取**本机存在**的组名，填进文案与界面排查清单；
- 顺带读 `/etc/passwd` 拿当前用户主 GID，判断"是否已经在组里" ——
  已在组里还打不开，说明不是加组问题，改提示往 `udev` 属组 / 重新登录方向查；
- 两个组都没有（容器、极简系统）时，提示直接去看 `ls -l /dev/ttyUSB0` 的属主属组。

纯文本解析，无 P/Invoke、无子进程。`--self-test` 增加 3 条用例守住这一点
（文案必须提到本机真实组名、不得提到不存在的那个组）。

### 2.3 端口名归一化 — `PortFailure.NormalizePortName`

Linux 上允许只敲 `ttyUSB0` 或 `serial/by-id/…`，统一补 `/dev/` 前缀；
Windows 原样返回。归一化放在 `CapsaProtocol` 构造函数里，所以 `--sim-test ttyUSB0` 也能用。

### 2.4 端口描述补全 — `Services/Ports/PortCatalogue.cs`（新增）

`SerialPort.GetPortNames()` 在 Linux 只给 `/dev/ttyUSB0` 这种裸名字，插两个 USB-TTL 就分不清谁是谁
（Mock 里那句"真实实现可读取注册表 / WMI 获取驱动描述"从来没落到 Linux 上）。

**改法**：读 sysfs，**不引入任何 P/Invoke**：

```
/sys/class/tty/ttyUSB0/device/interface   → "USB Serial"
/sys/class/tty/ttyUSB0/device/../product  → 设备产品名
/sys/class/tty/ttyUSB0/device/../idVendor + idProduct → "1a86:7523"
```

拼成 `USB Serial (1a86:7523)`。任何一步失败都退回 `Serial port`，枚举绝不抛异常。
顺带把排序改成先按长度（`ttyUSB2` 不会排到 `ttyUSB10` 后面）。

### 2.5 端口占用诊断 — `Services/Ports/PortCatalogue.cs` 内的 `PortInspector`

**问题**：Windows 的独占让"被占用"自己会报错；Linux 的 tty 不排他，两个进程能同时打开、
**都不报错**，症状变成"帧被撕裂 / 没有应答"，用户看不出是谁在抢。

**改法**：探测失败后扫一遍 `/proc/[pid]/fd` 的软链，把真正持有该 tty 的进程名附在错误后面
（跳过本进程，支持 by-id 软链先解析真实路径，最多 3 条，150 ms 预算）。

**明确不做**：不把它当作"拒绝连接"的依据。ModemManager 这类探测进程只持有几毫秒，
拿它当占用会引入偶发失败。这是**诊断**，不是**互斥**。

### 2.6 端口可手动输入 — `ViewModels/Shell/TopBarViewModel.cs` + `Views/Shell/TopBarView.axaml`

- `ComboBox` 改 `IsEditable="True"`，`Text` 双向绑到新增的 `PortText`
- `ResolvePort()`：输入框文本优先于下拉选中项；不在枚举结果里就新建一条 `manual entry`
- 刷新列表时把手动输入过的端口补回列表（`_manualPortName`），不会"输一次、刷新一次丢一次"
- 刷新改为"先记住当前端口名 → 重建列表 → 按名字找回选中项"，避免 `SelectedItem` 指向已被清空的对象
- 列表为空时的提示改为 `No serial ports found — type a port name to connect manually`

### 2.7 探测失败排查清单按平台给 — `TopBarViewModel.BuildNoAnswerReport`

Windows 给"串口助手 / PuTTY 是否占着"；Linux 给三条真实原因：

- 当前用户是否在串口组 —— **组名运行时探测**（Debian 系 `dialout`，Arch 系 `uucp`），
  不在则 `sudo usermod -aG <组名> $USER`（重新登录生效）
- `minicom / screen / 另一个 Capsa 实例` 是否在收同一个端口
- ModemManager 是否在探测该设备（udev 规则加 `ENV{ID_MM_DEVICE_IGNORE}="1"`）

换行统一用 `Environment.NewLine`。

### 2.8 等宽字体链 — `Styles/Controls.axaml`

`Cascadia Mono,Consolas,Menlo,Monospace` 前三个 Linux 上都不存在，
改成 `Cascadia Mono,Consolas,Menlo,DejaVu Sans Mono,Noto Sans Mono CJK SC,Source Code Pro,monospace`
（十六进制视图与错误日志窗口共用这两处样式）。

### 2.9 项目文件 — `Capsa.csproj`

- 补 `Version / AssemblyVersion / FileVersion / Product`：`MainWindowViewModel` 的
  `ApplicationVersion = "0.1.0"` 注释里写着"与 csproj 的 `<Version>` 保持一致"，但 csproj 里一直没有，
  实际程序集版本是 1.0.0。
- `ApplicationManifest`（`app.manifest`）保留不动：MSBuild 在 Linux 上直接忽略，无需条件化。

### 2.10 构建歧义 — `Capsa - Backup.csproj`

目录里同时存在两个 `.csproj` 时，`cd Capsa && dotnet run` 会报 MSB1011（无法确定项目）。
重命名为 `Capsa - Backup.csproj.bak`，内容保留、不再参与构建。

---

## 3. 已验证

全部在 Linux（Ubuntu / .NET SDK 8.0.133 / Avalonia 11.3.12）上实跑：

| 检查项 | 结果 |
| --- | --- |
| `dotnet restore` + `dotnet build Capsa/Capsa.csproj` | ✅ 0 error（1 个既有的 CS8620 警告，与本次改动无关） |
| `dotnet Capsa.dll --self-test`（改造前） | ❌ `SELF-TEST FAILED (2)`：`path stripped`、`invalid chars replaced` |
| `dotnet Capsa.dll --self-test`（改造后） | ✅ `SELF-TEST OK` |
| `CAPSA_MOCK=1 CAPSA_SHOT=<dir>` + Xvfb 跑完整走查 | ✅ 16 张截图全部产出，界面/等宽十六进制对齐正常 |
| `dotnet Capsa.dll --sim-test ttyUSB9` | ✅ 输出 `ttyUSB9 不存在（设备未插入，或端口名拼错）`，退出码 1 |

> 沙箱里 CoreCLR 默认起不来（`GC heap initialization failed 0x8007000E`），
> 上面所有命令都带了 `DOTNET_GCHeapHardLimit=0x18000000` 才能跑。**正常机器不需要这个变量。**

### 未在本次验证范围内

- 真实 USB-TTL + 板子的读写往返（需要有硬件）
- Windows 侧回归（见 §5 清单）

---

## 4. 未做的事（以及为什么）

| 事项 | 状态 | 原因 |
| --- | --- | --- |
| `IByteTransport` 抽象 + 进程内 loopback | 待做（P1） | 让协议回归测试摆脱 com0com/socat，两块平台都能在 CI 里跑；改动会动到协议层核心循环，单独一轮做更安全 |
| Linux 端口独占（`TIOCEXCL`） | 不做 | `SerialPort` 不暴露 fd，得 P/Invoke 自写 `open/ioctl` 或换串口库；收益（"能阻止"）远小于成本，当前只做到"能诊断" |
| by-id 稳定名持久化 | 待做（P1） | Linux 上 `ttyUSB0/1` 会随插拔顺序变化；建议下拉显示短名、内部存 by-id，会牵动 UI 与配置存储 |
| `SIGINT` / `SIGTERM` 优雅退出 | 待做（P2） | 终端里 Ctrl+C 不会发 `ABORT`，固件靠每包 2 s 超时自己退出；影响有限 |
| 中文字体内嵌 | 不做 | 主界面文案基本是英文，中文只出现在错误弹窗；内嵌 CJK 要 10~20 MB，改为 README 提示装 `fonts-noto-cjk` |
| `System.IO.Ports` / `CodePages` 版本对齐 | 评估后保留 | 目前是 `10.0.14` 配 `net8.0` 目标，实际解析 netstandard2.0 资产 + 10.x 传递依赖；能跑，但没有实际收益前不动依赖，避免破坏已经工作的 Windows 构建 |
| 并行探测端口 | 待做（P2） | 每口最多 ~2 s 串行探测；正常机器只有 1~2 个 USB 口，暂不影响 |

---

## 5. Windows 回归检查清单

改动的都是双平台共用的代码路径，需要在 Windows 上确认没有回退：

1. `dotnet Capsa.dll --self-test` 仍然 `SELF-TEST OK`
2. 下拉选 COM 口 → Connect → 断开 → 重新连接，心跳正常
3. **占用场景文案仍是 Windows 口径**：打开串口助手占用 COMx，再点 Connect，应提示"被其它程序占用（串口助手 / PuTTY / 另一个 Capsa 实例？）"
4. 导入一个文件（尤其文件名带路径/尖括号等字符）→ 写入 → 读回，文件名还原正确
5. 端口下拉框可编辑后：仍能正常下拉选择、刷新后选中项不丢、灰度（已连接时）行为正常
6. 端口列表刷新后仍按 `COM1/COM2/…/COM10` 顺序（新增了按长度排序）
7. 拔线 → 顶栏显示 `Device lost (...)`，主界面复位

---

## 6. Linux 使用与部署

### 6.1 运行

```bash
dotnet build Capsa/Capsa.csproj        # 首次需要能访问 nuget.org
dotnet run   --project Capsa           # 或：dotnet Capsa/bin/Debug/net8.0/Capsa.dll
```

发布自包含包（目标机不需要装 .NET）：

```bash
dotnet publish Capsa/Capsa.csproj -c Release -r linux-x64 --self-contained true \
  -p:PublishSingleFile=false -o dist/linux-x64
```

### 6.2 串口权限（组名随发行版变，别照抄）

```bash
ls -l /dev/ttyUSB0                        # 先看设备属组，最可靠
groups | grep -E 'dialout|uucp'           # 没输出就是没权限
sudo usermod -aG dialout $USER            # Debian / Ubuntu / Fedora
sudo usermod -aG uucp    $USER            # Arch / Manjaro / openSUSE
# 之后必须重新登录（不是重新开终端，是要新的登录会话）
```

> **踩过的坑**：早先版本把 `dialout` 写死在文案里。Arch 上根本没有这个组，
> 用户照敲 `sudo usermod -aG dialout $USER` 直接得到
> `usermod：“dialout”组不存在` —— 一个纯由发行版差异制造的假故障。
> 现在 `Models/UnixSerialGroup.cs` 读 `/etc/group` 选出真实存在的组
> （并顺便判断用户是否**已经在**组里，是的话就提示往 udev / 重登录方向查），
> `--self-test` 里也有对应用例防止再退化。

### 6.3 已知的 Linux 环境坑

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 插上 USB-TTL 后设备节点立刻消失 | Ubuntu 22.04+ 的 `brltty` 抢占 CH340（`1a86:7523`） | `sudo apt remove brltty` 或加 udev 黑名单 |
| 首次握手偶发失败、之后正常 | ModemManager 探测新 ttyUSB 并发送 `AT` 命令，吃掉首帧 | udev 规则加 `ENV{ID_MM_DEVICE_IGNORE}="1"`；程序本身有 3 次重试兜底 |
| 端口名每次插拔都变 | 内核按枚举顺序分配 `ttyUSB0/1` | 在端口框里直接手输 `/dev/serial/by-id/usb-…` |
| 中文显示成方块 | 系统缺 CJK 字体 | `sudo apt install fonts-noto-cjk` |
| 无显示器/SSH 下启动即退出 | 纯 GUI，需要 X11 或 Wayland | 无头场景用 `--self-test` / `--sim-test` |

---

## 7. 附：仓库卫生建议

`Capsa/bin`、`Capsa/obj` 当前被 git 跟踪，一次构建就会产生几十个二进制 diff。
建议加 `.gitignore`（`bin/`、`obj/`、`.vs/`、`__pycache__/`）并把已跟踪的构建产物移出版本库，
否则跨平台协作时两边的产物会互相覆盖。
