# Capsa 上位机

Avalonia 11 (net8.0 / MVVM) 桌面工具，用于通过串口操作 8 片 24C 系列 EEPROM
组成的存储卡阵列（地址 0x50 ~ 0x57）。

## 构建与运行

Windows：

```
dotnet build Capsa/Capsa.csproj --no-restore   # 本机 restore 会失败，必须带 --no-restore
dotnet run --project Capsa --no-build
```

Linux（同一份代码，无需改工程）：

```
dotnet build Capsa/Capsa.csproj
dotnet run --project Capsa
dotnet publish Capsa/Capsa.csproj -c Release -r linux-x64 --self-contained true -o dist/linux-x64
```

> Linux 上串口名是 `/dev/ttyUSB0` 这种；当前用户必须在串口组里 ——
> Debian/Ubuntu/Fedora 是 `dialout`，**Arch/openSUSE 是 `uucp`**
> （`sudo usermod -aG uucp $USER` 后必须重新登录）。不确认就先 `ls -l /dev/ttyUSB0`
> 看设备属组，Capsa 自己也会按 `/etc/group` 探测出本机真实的组名并写进报错。
> 端口下拉框可直接手输，支持 `/dev/serial/by-id/…`。
> 跨平台改动记录、平台差异与已知环境坑见 [`docs/CROSS_PLATFORM.md`](docs/CROSS_PLATFORM.md)。

诊断开关（环境变量）：

| 变量 | 作用 |
| --- | --- |
| `CAPSA_MOCK=1` | 使用内存 Mock（无硬件演示） |
| `CAPSA_SHOT=<dir>` | 自动走查并截图（配合 `CAPSA_MOCK`） |
| `CAPSA_SHOT_REAL=1` | 走查「扫描 → 连接 → 初始化 → 写入 → 读取」全流程 |
| `CAPSA_SHOT_READ=1` | 走查「连接 → 读取 & 校验」 |

命令行：`Capsa.exe --self-test [log.txt]` 跑纯逻辑自检。

## 存储卡元数据语义（重要）

24C02 里有两块**不是数据内容**的元信息：

| 区域 | 含义 | 写入规则 |
| --- | --- | --- |
| `0x2C ~ 0x31` | 当前已使用空间（UG） | 写入完成后上报；**重新初始化 / 格式化归零** |
| `0x32 ~ 0x39` | 数据哈希（DI） | 写入完成后计算并下发；数据被覆写后**清成 8 个 0**（视为“无记录”） |
| `0x3A ~ 0x3D` | 累计写入次数（DI） | 每次写入成功后 +1；**任何初始化都不清零** |

**累计写入次数是 EEPROM 的寿命指标**，用来估算擦写寿命余量。它只应由一次
成功写入（写入 + 回读校验通过）推进，初始化、重新初始化、格式化都不得复位，
否则等于伪造了一块新卡。

实现约束（改动这些地方时务必保持该语义）：

- `CapsaCardInitService`：初始化收尾只发 `UG_0` 与“清哈希”，**不发** `DI_..._0`。
- `WritePanelViewModel` 是格式化 / 写入的**唯一入口**（`RunWriteSessionAsync`）。
  `CapsaDeviceService.FormatAsync` 是早期"逐片直接写"的实现，**界面已不再调用**；
  它仍然 `擦数据区 → UG_0 → DI_<全0>_!`，保留只为协议层回归测试。
- 格式化收尾必须走 `SET`（9 字段）→ `UG_0` → `DI`，三步的返回值都要检查：
  以前 `UG_0` 失败是静默的，表现就是"格式化完容量还在"。
- `WritePanelViewModel.BuildDeviceInfo`：初始化完成后面板用初始化结果覆盖，必须
  把 `WriteCount = _device.WriteCount`（刷新后的真实值）带过去，否则界面会把
  刚保住的次数显示成 0。
- `DataIntegrity.IsNoRecordHash`：空哈希与全 0 哈希都判定为“无记录”，
  避免格式化后误报哈希不一致。
- `MockDeviceService`：Mock 行为必须与真机一致（重新初始化 / 格式化不清次数）。

下位机协议细节见 `../CapsaSim/docs/COMMAND_REFERENCE.md` §4.3 / §4.4 / §6B。

## 界面约定

- 左侧属性面板：设备信息 + 容量占用 + 数据哈希/写入次数 + 健康分。
  写入过程中容量占用随进度**实时增长**（整卡覆写语义：从 0 涨到 payload 长度）。
- 右侧 8 卡阵列：整卡即进度条；写保护（WP）与写使能（WEL）徽标。
- 健康分级：0 = HEALTHY（蓝）/ 1-9 = WARN（黄）/ 10-20 = MEDIUM（橙）/ >20 = ERR（红）。

## 写保护交互（WP 点击）

- 卡片右上角的 **WP 徽章是按钮**：点击翻转该片的写保护 —— `SET_!_<mask>_!…_!`
  （其余字段全 `!` 占位，固件只更新 WP）→ PCF8574 立即生效。
- 被保护的芯片：卡片**整块淡黄**、徽章黄色；容量被**剔出有效空间**
  （`DeviceInfo.ArrayEffectiveBytes`）；写入 / 读取 / 格式化全部**跳过该芯片**。
- 属性面板的 **Card capacity** 行显示剔除后的有效容量
  （`40 KB (6/8 chips)`），无保护时显示满容量。
- 左侧容量进度条是**三层自定义结构**（底轨 / 黄色保护段右对齐缩放 / 蓝色已用段
  左对齐缩放），两段都以满容量 8 片为基准 —— 黄段不可用时进度条右侧留黄。
- 注意：叠加层 Border **不能用 `HorizontalAlignment="Right"`**（无内容时宽度
  收缩为 0，ScaleTransform 缩 0 还是 0），要用默认 Stretch + 右侧 Origin。

## 格式化

**两种格式化，语义不同：**

| | Quick format | Format card（完整格式化） |
| --- | --- | --- |
| 动作 | 只发 `UG_0` + `DI_<全0>_<次数>` | 逐片写 0x00 全地址 + 回读校验 + 收尾三步 |
| 耗时 | 秒级（2 条命令） | 分钟级（全片总线刷写） |
| 数据 | **字节原样留在芯片上** | 全部变成 0x00 |
| 语义 | 卡"不记得有文件了"，之后写入从头覆写 | 物理擦除到全 0 |
| 前置 | 卡必须已初始化（固件拒绝未初始化卡的 UG） | 未初始化可先 SET 建属性再写 |

- **Quick format 实现**（`WritePanelViewModel.QuickFormatAsync`）：
  `SetUsedSpaceAsync(0)` → `SetDataInfoAsync(全0, 原次数)`，返回值逐项检查，
  本地 `BeginWrite()` + `SetLiveUsedBytes(0)` + refresh；失败弹可复制详情。
- 完整格式化的其余行为见下。

**目标：完整格式化 = 一次"覆盖面最大"的普通写入，用户在界面上看到的每一处反馈都与写入一致。**

- **范围**：每一片可写芯片的**完整地址空间** `0x0000 .. chipBytes-1`，
  payload 长度 = `可写片数 × 单片容量`（保护片跳过，路由层按 WP 掩码分发）。
  不再用"整卡有效容量"这种二义口径 —— 长度直接由 `writable × CapacityBytes` 算出来，
  状态栏还会显示 `6 writable chip(s) × 32.0 KB = 192.0 KB · 0x0000–0x7FFF per chip`。
- **编辑器同步填满 0x00**：点击 Format 后 hex 编辑器当场装载等长的全 0x00 缓冲
  （来源名 `format · N×32.0 KB of 0x00`）—— 格式化的语义就是"把一个全 0 文件写进阵列"，
  用户在编辑器里看到的正是这次写入的内容。
- **同一套会话**：ROUTER → ADDR 0..末地址 → INT_W → `W_*` → TX-OVER → 回读校验，
  所以底部进度条（写入 85% + 校验 15%）、速率、**Stop 中断**、左侧卡片逐片填充、
  左侧占用百分比全部与"Verify & Write"完全一致。
- **卡未初始化也能格式化**：先发一次 `SET_`（9 字段）把属性建立起来
  —— 名称 / WP 掩码 / 健康掩码 / 阵列容量 / 型号 / 修改时间 / 读取时间 / 介质 / UID。
  固件在 `INT_W` 前会校验 `Meta_InfoValid()`，没有属性的卡根本开不了写入会话。
  因此 **Format card 按钮只受"没有任务在跑"限制**，不再要求卡已初始化。
- **收尾三步（顺序固定，任一步失败都会写进状态栏并弹可复制详情）**：
  1. `SET_<9 字段>` —— 属性完整、`init_flag = 1`、时间戳刷新；
  2. `UG_0` —— **已用容量归零**（"格式化后还显示旧容量"就是这一步没成功，
     以前代码没检查返回值，失败是静默的）；
  3. `DI_<16 个 0>_<原次数>` —— 哈希失效，**累计写入次数保留**（寿命指标）。
- **★ 打断 / 失败也要清占用**：格式化中途 Stop 或出错时，只要已经有数据写到卡上
  （`_wroteAnyBytes`，写阶段进度 > 0 即置位），就**强制补发 `UG_0` + 清哈希**
  （收尾命令用 `CancellationToken.None`，不受取消影响）—— 部分覆写已经破坏了
  旧数据的完整性，"已使用 N 字节"的旧记录从此不可信，必须归零。
  卡片与左侧占用同步清零，状态栏显示 `used space reset to 0`。
- 收尾后本地立即 `BeginWrite()` + `SetLiveUsedBytes(0)`，再 `RefreshDeviceAsync()`
  用真实 UGS 值覆盖：卡片与左侧占用当场清零，不需要等下一次手动刷新。
- 全保护时报 `All 8 chips are write-protected — release WP before formatting`。

## 占用记录（UG）与数据完整性 —— 核心原则

**任何覆写一旦开始，"已使用 N 字节"的旧记录就是谎言。** 用户看到旧占用会以为
卡里还有完整文件 —— 所以记录必须在破坏发生时（而不是失败后）就归零：

| 操作 | 清 0 时机 |
| --- | --- |
| 完整格式化 | **会话开始时**（固件仍处空闲态，`CancellationToken.None`）|
| 普通写入 | **会话开始时**（同上，与格式化统一）|
| 写入失败 / 校验失败 / 取消 | 兜底：会话前清 0 未成功（`_usedClearedDuringWrite = false`）时补 `UG_0` + DI 清哈希 |
| 芯片健康检查 | **测试开始时**（整片覆写图案，破坏性同格式化）|
| 读取 | 只读不破坏，不清 |

为什么"先清"优于"失败后补偿"：补偿依赖上位机活着 —— 崩溃、断线、断电时补偿代码
根本没机会跑，卡上就会残留"还有完整数据"的假象。而"先清"保证了从破坏的第一字节起，
卡上的记录就是诚实的。

**★ 为什么清 0 必须在会话开始、绝不能在会话中途**：这套协议是**半双工主从式** ——
写会话中固件在流式处理 `W_` 包，插队的 `UG_0` 会与数据流竞争（固件处理 UG 时还要
写 24C02、阻塞主循环）。实测后果：应答撕裂成 `[RX-OK
UU-OK]`、后续 `W_` 包
2 s 超时、写入失败。**任何命令只能在自己会话之外发**，UG 也不例外。

## 错误日志与详情窗口

**设计动机：任何失败（初始化 / 写入 / 校验 / 读取 / 连接）都不再只是一行截断提示。**

- `Capsa.Diagnostics.ErrorLog`：全局环形日志（最近 200 条）。每条含时间、来源、
  摘要和**完整详情** —— 异常类型 + 消息 + 堆栈 + `InnerException` 全链 +
  **最近 40 条协议交互**（`CapsaProtocol.RecentTrace()`：发了什么命令、卡回了什么、
  耗时多少毫秒；`W_` / `R_` 数据包折叠成 `W_A1B2C3D4… (32 B)`）。
- **`ErrorDetailsWindow`**：只读但完全可选、可复制的等宽文本框 +
  `Copy` / `Copy full log` / `Close`。触发路径：
  - 任何走 `AppServices.Messages.ShowErrorAsync` 的错误（连接失败、初始化失败、
    格式化收尾警告……）自动记日志并弹出该窗口；
  - 初始化窗口失败后，底部有 **Details** 按钮可再次打开；
  - 顶栏出现 **Logs** 按钮（有过错误时才显示），随时翻全部日志。
- **全局兜底**（`App.InstallGlobalErrorTraps`）：
  `Dispatcher.UIThread.UnhandledException`（UI 线程崩溃，只记日志不吞异常）、
  `TaskScheduler.UnobservedTaskException`（后台任务异常，`SetObserved` 后不再杀进程）、
  `AppDomain.UnhandledException` —— 全部进 `ErrorLog`。
- 新增 catch 的约定：凡是用户能感知的失败，都应
  `ErrorLog.Add(来源, 摘要, ex, 现场描述)`；只走 `ShowErrorAsync` 的可以不重复记
  （弹窗服务会自动记）。

## 掉线检测（心跳）

- `CapsaSerialPortService` 每 **3 s** 检查一次，**只在无任务时探测**
  （`CapsaProtocol.IsSessionBusy` 时强制让路 —— 读会话里上位机十几秒只收不发，
  光看最后写命令时间会误判空闲，probe 会插进正在回数据的总线抢串口）：
  1. 端口是否还在 `SerialPort.GetPortNames()` 里 —— USB 拔出立即断开；
  2. 最近 1.5 s 内有总线活动（收到的每个字节都刷新 `LastActivityMs`）就跳过；
  3. 空闲时发 `TYPE-CAPASA`（500 ms），**连续 2 次**无应答才判定掉线。
- **端口故障与"没响应"分开处理**：probe 时写入抛 `UnauthorizedAccessException`
  （端口被其它程序占用，Windows 串口独占）或 `IOException`（设备被移除），
  以及读阶段记入 `CapsaProtocol.LastPortError` 的同类故障 —— 都是确定性故障，
  **立即断开**，顶栏显示 `Device lost (port taken or removed · 拒绝访问端口 COMx)`
  之类的明确原因，不给重试机会。
- 心跳 tick 间隔自监控：间隔超过 3 倍周期（9 s）→ ErrorLog 记一条
  `heartbeat thread was stalled`（诊断用，**不会主动断开连接**）。
- 掉线 → `ConnectionChanged(false, reason)` → 顶栏切回 `Disconnected`，
  主界面复位设备面板与卡片（不会停留在过期数据上）。
- ~~UI 连接看门狗~~：曾试验过"UI 侧 4 s 检测端口消失 + 12 s 心跳停摆即断开"的
  看门狗，实测对"心跳线程偶发卡死"过于敏感（把还活着的连接杀掉），**已移除**。
  心跳自己卡死时拔线检测会失效，这是已知取舍 —— 稳定连接优先于极端场景的自动断开。

## 中断（Stop）

- 写入模式与读取模式都有 **Stop** 按钮（`IsCancelable` 为 true 时显示）。
- 动作：先 `RequestAbort()` 让固件发 `[ABORT-OK]` 退出会话（否则它会白等
  每包的 RX-OK 2 秒），再 `CancellationTokenSource.Cancel()` 中止主机侧循环。
- 结束后状态栏显示 `Cancelled by user`，并用真实设备状态刷新卡片/面板。
