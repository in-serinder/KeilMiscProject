# PDL 上位机（Avalonia）结构设计

> 版本：v0.4（界面二轮：每通道独立曲线面板 + 弹出/停靠 + 配置弹窗）

> **v0.4 已实现（2026-11-13 第八轮）**：
> - 主窗口曲线区改为 **2×2 停靠网格 + GridSplitter**，每通道一个独立面板，Y 轴直接标本路电压刻度（`ChannelChart` 控件）
> - 每个面板可**弹出**为独立 `ChannelWindow`（与主界面共享同一 `ChartBuffer`，级联定位），可**停靠**回来；顶栏"全部弹出/全部停靠"
> - **触发配置**与**定时/时长/存储档**改为模态弹窗（`TriggerDialog` / `TimingDialog`），与左栏共享同一 `AcquireViewModel`，改完立即生效
> - 切换记录模式到「定时/触发」时**自动弹出**对应配置窗口
> - 左栏只留摘要行 `TrigSummary`，采样参数与通道使能仍在左栏
>
> **温度曲线（同轮追加）**：`TempLive` 内置 720 点历史（约 12 分钟）；
> 温度条右侧内嵌**迷你折线**（`TempChart`，点击即弹出），也可点"弹出"按钮；
> `TempWindow` 独立窗口两路同图（℃ 轴自动量程、平坦时扩 ±0.5 ℃），与迷你折线共用同一份历史。
>
> **`.pdl` 落盘与回放（已实现，`Models/Pdl/PdlFile.cs`）**：文件 = Header（PDLF/ver/时间/速率/四路配置 25 B 每通道）+ Record*（u8 type|u16 len|payload|u32 crc，type 1=RAW块 2=BIN块 3=温度块 4=事件 5=IDX标记）+ Index（17 B/条）+ Footer（记录数/块数/时长/文件CRC）。会话开始即写 spool（%TEMP%\PDL_session\*.pdl.part），ABORT/定时到点自动收尾；"保存"= 另存对话框 + 复制改名，"丢弃"= 删 spool，退出未保存自动删。回放：坏记录即停（截断文件可读已写部分）、抽稀 ≤3000 点（min/max 保留）、功率 + 温度概览 + 统计表。坑：**通道配置是 25 B/个不是 24**（ch1+vFs2+fsMa2+4×4+off2+off2），数错一次；事件 payload 8 B。

> **回放视窗缩放（同轮追加）**：`StaticChart` 挂 `ViewStartMs/ViewEndMs`（TwoWay StyledProperty），**滚轮以鼠标为中心缩放、拖拽平移、双滑块选段、"全览"复位**；VM 持全量解码数据，视窗变化时二分定位 `[t0,t1)` 再 min/max 抽稀 ≤3000 点，功率/温度共享同一视窗。统计表保持全程口径。
> **关键修正：时间轴必须归一到会话首块**——设备时钟基准不定（模拟器用 epoch ms，真机是上电 ms），`Decode()` 统一减去 `min(TFirstMs)`，否则视窗过滤交集为空、图表全空。

> 工程：`PDL_upper/PDL_upper/PDL_upper.csproj`（net8.0 / Avalonia 11.3 / CommunityToolkit.Mvvm 8.2.1 / 编译绑定已开）
> 视觉参考：`51_PWMCtrol`（深色面板 + 通道卡片 + 自绘控件 + 底部日志）

---

## 1. 设计取向

1. **沿用 51_PWMCtrol 的视觉语言**：深色底、卡片分块、通道固定配色、等宽数字、底部日志。用户已熟悉这套观感。
2. **布局按"4 功率 + 2 温度"重构**：功率卡横排 4 张，温度压成一条细带（温度不需要大卡片）。
3. **除 MainWindow 外全部 UserControl**，每个都写 `x:DataType`，编译绑定。
4. **自绘控件零第三方依赖**：曲线、表盘、大数字全部 `Control.Render` 画。
5. **交互做到"事无巨细"**：每通道每物理量独立显隐、可缩放、可游标读取、可导出选区。

---

## 2. 目录结构

```
PDL_upper/PDL_upper/
├─ App.axaml / App.axaml.cs          # 加 Styles、DI 容器
├─ Program.cs                        # --tcp / --hid / --selftest / --ui-demo
├─ ViewLocator.cs
├─ Styles/
│  ├─ Theme.axaml                    # 颜色/字号/间距
│  ├─ Controls.axaml                 # 按钮/输入框/卡片/TabItem
│  └─ ChannelColors.axaml            # 4 功率 + 2 温度配色
├─ Models/
│  ├─ Protocol/
│  │  ├─ PdlConstants.cs             # 生成
│  │  ├─ Messages.cs                 # 生成（DTO + Pack/Unpack + 长度断言）
│  │  ├─ Frame.cs
│  │  └─ FrameCodec.cs
│  ├─ Transport/
│  │  ├─ ITransport.cs
│  │  ├─ HidTransport.cs             # HidSharp
│  │  └─ TcpTransport.cs
│  ├─ DeviceSession.cs
│  ├─ PowerChannelConfig.cs
│  ├─ TempChannelConfig.cs
│  ├─ AcquireParams.cs
│  ├─ SampleBlock.cs
│  ├─ ChartBuffer.cs                 # 环形 + min/max 分桶
│  ├─ Pyramid.cs                     # L0–L3
│  ├─ PdlFile.cs
│  └─ SpoolWriter.cs
├─ Services/
│  ├─ AcquisitionService.cs
│  ├─ StatisticsService.cs
│  ├─ StorageService.cs
│  ├─ ExportService.cs
│  └─ AppServices.cs
├─ Controls/
│  ├─ PowerChart.cs                  # 主曲线（多模式、双 Y 轴、游标）
│  ├─ Sparkline.cs                   # 迷你趋势
│  ├─ BigValue.cs                    # 大号读数（SI 前缀 + 超限变色）
│  ├─ LevelBar.cs                    # 量程占比条
│  ├─ TriggerBadge.cs
│  └─ SegmentDisplay.cs              # 复用
├─ ViewModels/
│  ├─ MainWindowViewModel.cs
│  ├─ ConnectionViewModel.cs
│  ├─ AcquisitionViewModel.cs        # 采样 + tier + 时长 + 记录模式
│  ├─ TriggerViewModel.cs            # V/I/P + 阈值 + 消抖
│  ├─ PowerChannelViewModel.cs       # ×4
│  ├─ TempChannelViewModel.cs        # ×2
│  ├─ ChartViewModel.cs              # 显隐矩阵、缩放、游标
│  ├─ StatisticsViewModel.cs
│  ├─ SessionViewModel.cs
│  └─ LogViewModel.cs
├─ Views/
│  ├─ MainWindow.axaml
│  └─ Panels/
│     ├─ TopBar.axaml
│     ├─ ConnectionPanel.axaml
│     ├─ AcquirePanel.axaml
│     ├─ TriggerPanel.axaml
│     ├─ PowerCard.axaml             # ItemsControl 复用 ×4
│     ├─ TempStrip.axaml             # 温度细带 ×2
│     ├─ ChartPanel.axaml
│     ├─ StatisticsPanel.axaml
│     ├─ SessionPanel.axaml
│     └─ LogPanel.axaml
└─ Converters/
```

---

## 3. 布局

```
┌─ TopBar ───────────────────────────────────────────────────────────────────┐
│ [设备▾][刷新][连接]  ● F103-PDL 已连接   00:12:34  ●REC   [保存] [丢弃]   │
├─ Sidebar 320 ─┬─ 主区 ─────────────────────────────────────────────────────┤
│ ▸ 连接         │ ┌ 功率卡 ×4（横排，可折叠）────────────────────────────┐ │
│   端口/设备     │ │ CH0  ●    CH1 ●    CH2 ●    CH3 ●(大电流)           │ │
│ ▸ 采集参数     │ └──────────────────────────────────────────────────────┘ │
│   采样率 1000Hz │ ┌ 温度带 ──────────────────────────────────────────────┐ │
│   间隔   100 ms │ │ TH0 25.4℃ ▁▂▃▂   TH1 26.1℃ ▁▁▂▁   板载 31.2℃      │ │
│   存储档 [无损▾]│ └──────────────────────────────────────────────────────┘ │
│   时长   [∞  ]  │ ┌ PowerChart ──────────────────────────────────────────┐ │
│   温度间隔 1s   │ │ 模式[叠加▾] 显隐: ☑CH0 ☑CH1 ☐CH2 ☐CH3  V ☑ I ☑ P ☐ │ │
│ ▸ 记录模式      │ │ ┌──────────────────────────────────────────────────┐ │ │
│   ○手动 ○定时   │ │ │  ╱╲    ╱╲                                       │ │ │
│   ○触发         │ │ │ ╱  ╲__╱  ╲___   ← V 实线(左轴) I 虚线(右轴)     │ │ │
│ ▸ 触发配置      │ │ └──────────────────────────────────────────────────┘ │ │
│   源 [电压▾]    │ │ 时基 10s▾  [自动Y]  ├─游标 12.483V / 2.104A        │ │
│   通道 [CH0▾]   │ └──────────────────────────────────────────────────────┘ │
│   开阈值 12000mV│ ┌ Tab: 统计表 │ 通道详情 │ 事件 │ 协议日志 ──────────┐ │
│   保持   50 ms  │ │ CH  Vavg  Vmax  Vmin  Vpp  纹波%  Iavg  Pavg ...  │ │
│   关阈值 8000mV │ │ CH0 12.48 12.51 12.45 60mV 0.48% 2.10  26.2W      │ │
│   保持   50 ms  │ │ CH1 ...                                           │ │
│   ☑自动重武装   │ │ TH0 25.4 26.1 24.9  —     —      —    —          │ │
│ ▸ 会话          │ └──────────────────────────────────────────────────────┘ │
│   未保存 128 MB │                                                          │
│   [保存][丢弃]  │                                                          │
├────────────────┴──────────────────────────────────────────────────────────┤
│ 1000 Hz · 334 帧/s · 丢块 0 · 缓冲 12/64 · 2026-11-13 14:52:03           │
└───────────────────────────────────────────────────────────────────────────┘
```

要点：
- **温度不做大卡片**：温度是慢变量，一条细带 + 迷你趋势就够，省下来的空间给曲线。
- **参数全在左栏**，卡片只放"每通道独有"的东西（标签、量程、校准、当前值、状态）。
- **曲线区可拖高**（GridSplitter），用户看细节时会想压扁统计表。
- **Tab 在底部**：统计表最常看，默认页；协议日志是排障用。

---

## 4. ViewModel 结构

```
MainWindowViewModel
├── ConnectionViewModel
├── AcquisitionViewModel       参数 + ACQUIRE/ABORT + tier + 记录模式
├── TriggerViewModel           source/ch/阈值/消抖/自动重武装（并入 ACQUIRE 载荷）
├── PowerChannelViewModel[4]   配置 + 最新值 + 指标 + 尖峰阈值
├── TempChannelViewModel[2]    配置 + 最新值 + 指标
├── ChartViewModel             显隐矩阵、模式、时基、游标、Y 轴
├── StatisticsViewModel        表格（33 ms 刷新）
├── SessionViewModel           spool 状态、保存、丢弃、回放
└── LogViewModel               日志环（2000 条上限）
```

全部用 `[ObservableProperty]` / `[RelayCommand]` 源生成。

```csharp
public partial class PowerChannelViewModel : ObservableObject {
    [ObservableProperty] private string _label = "CH0";
    [ObservableProperty] private bool _enabled = true;
    [ObservableProperty] private double _voltage;     // V
    [ObservableProperty] private double _current;     // A
    [ObservableProperty] private double _power;       // W
    [ObservableProperty] private double _vpp;         // 纹波
    [ObservableProperty] private ChannelStatus _status;

    // 尖峰阈值（用户可设，参与统计）
    [ObservableProperty] private double _spikeThresholdV;
    [ObservableProperty] private long _spikeCount;

    public bool IsSelectedForChart { get; set; }        // 显隐矩阵的一格
}
```

### 4.1 命令可用性（关键交互规则）

| 场景 | UI 行为 |
|---|---|
| 未连接 | 采集/触发/通道配置全部禁用，只留"连接" |
| 已连接 · IDLE | 可改参数、可 ACQUIRE |
| RUNNING / ARMED | **参数控件全部禁用并置灰**，顶部提示"采集中，改参数请先停止" |
| 采集中点"改参数"（如果没禁用） | 弹窗：结束当前记录？[保存并重开][直接重开][取消] |
| ABORT 中（DRAINING） | 显示"正在收尾…还剩 N 块"，不响应新命令 |

> 这对应你说的"交互时禁止改采样"。**不是靠提示，是把控件禁用掉**，从交互上杜绝。

### 4.2 参数下发与回填

```csharp
var p = BuildAcquireParams();                 // 汇总各子 VM
var ack = await _session.AcquireAsync(p);     // 超时 200ms × 3
ApplyEffective(ack);                          // 回填 eff_* 值
```

设备可能取整（1000 Hz → 976 Hz）。ACK 里的 `eff_*` 必须回填到 UI，并给一个短暂的"已调整为 976 Hz"提示。
`BAD_PARAM` 的 `detail` 是出错字段的载荷偏移，映射成具体控件标红。

---

## 5. 线程模型

```
[IO 线程]  Task.Run(AcquisitionService.RunAsync)
   while(connected):
     transport.Read(buf, 100)
     codec.Feed(buf)                    // 拆帧 + CRC + 重组
     switch(TYPE):
       DATA_BLOCK → _blockQueue.Writer.TryWrite(block)   // 有界 256，满则丢最旧+计数
       ACK        → _pending[seq].TrySetResult(ack)
       EVENT      → _eventQueue.Writer.TryWrite(evt)

[消费线程]  await foreach (b in _blockQueue.Reader.ReadAllAsync())
   if (b.IsTemp)  tempSeries.Push(b); stats.AccumulateTemp(b);
   else           pyramid.Push(b); chartBuffer.Push(b); stats.AccumulatePower(b);

[UI 线程]  DispatcherTimer(33 ms)
   Snapshot → 更新 VM 属性（只更新可见项）
   → PowerChart.InvalidateVisual()
```

**三条铁律**

1. UI 刷新走 33 ms 定时器批量更新，**不要每块 `Dispatcher.Post`**（334 帧/s 会把 UI 打死）。
2. 队列有界（256 块），满了丢最旧并计数，绝不无限增长。
3. 日志 2000 条上限，滚动丢弃；协议日志默认摘要，hex 按需展开。

---

## 6. 主曲线 PowerChart（最硬的部分）

### 6.1 三种显示模式

| 模式 | 说明 | 适用 |
|---|---|---|
| **叠加 Overlay** | 所有勾选通道画在同一坐标系；V 用左轴实线，I 用右轴虚线，P 可选归一化虚线 | 对比多路输出 |
| **堆叠 Stacked** | 每通道一个子图，共享 X 轴（像示波器多通道） | 各通道量纲差异大时 |
| **单通道 Single** | 一个通道占满，V/I/P 三线 | 看细节、量纹波 |

### 6.2 显隐矩阵（"可隐藏"的落地）

```
             CH0  CH1  CH2  CH3
  V (左轴)    ☑    ☑    ☐    ☐
  I (右轴)    ☑    ☐    ☐    ☐
  P (归一化)  ☐    ☐    ☐    ☐
```

- 一张 4×3 的勾选矩阵，独立控制每条曲线
- 图例点击 = 快速切换该通道全部曲线
- 通道卡片上的眼睛图标 = 切换该通道整列
- 温度曲线单独一个开关（画在副轴上或独立细带）

### 6.3 缩放与游标（"可缩放细节"的落地）

| 操作 | 行为 |
|---|---|
| 滚轮 | 以光标位置为中心的 X 轴缩放（1× ~ 10000×） |
| 左键拖拽 | 平移 |
| 右键拖拽 | 框选放大 |
| 双击 | 复位到"全时段" |
| Shift+滚轮 | Y 轴缩放（关闭自动 Y 时生效） |
| 拖 X 轴选中区间 | 显示区间统计与"导出此段" |
| 鼠标悬停 | 十字线 + 各可见曲线在该时刻的读数浮层 |

### 6.4 渲染实现

```
1. 取可见窗口 [t0, t1]，映射到像素
2. 按像素分桶：每桶合并 min/max/avg（L0 原始点时先降采样）
3. 包络：每 X 像素画一条竖线 (yMin→yMax)，低透明
4. 均值：一条 StreamGeometry 折线，实色
5. 叠加：阈值虚线、触发标记、缺口斜纹、选中区间高亮
```

- Avalonia 里逐像素 `DrawLine` 每帧约 1200 次，30 fps 无压力
- 均值线用单个 `StreamGeometry` 一次构建，避免逐段调用
- **Pen/Brush 缓存成字段**，绝不每帧 new（GC 抖动会掉帧）
- 超 500 万点时必须先降采样，**绝不把原始点丢给渲染**

### 6.5 从金字塔选层

```
可见窗口 > 1 h  → 用 L2 (1 s 桶)
        > 1 min → 用 L1 (10 ms 桶)
        否则    → 用 L0 (原始)
```

这样缩放时不会因为点位太多而卡，也不会因为用粗层而看不到毛刺。

---

## 7. 统计面板

### 7.1 功率通道列（可勾选隐藏列）

| 列 | 说明 |
|---|---|
| 通道 | 色块 + 标签 |
| `Vavg / Vmax / Vmin` | 平均 / 最大 / 最小电压 |
| `Vpp` | 纹波峰峰值 |
| `纹波率` | `Vpp / Vavg × 100%` |
| `Vrms_ac` | 交流有效值（去直流后的纹波能量） |
| `Iavg / Imax / Imin` | 电流 |
| `Ipp` | 电流峰峰 |
| `Pavg / Pmax` | 平均 / 最大功率 |
| `尖峰次数` | 超过用户设阈值的次数 |
| `超限时长` | 累计超出阈值的时长 |
| `稳定度` | `std(V) / Vavg` |
| 状态 | 正常 / 饱和 / 未启用 |

### 7.2 温度通道列

`Tavg / Tmax / Tmin / 上升速率(℃/min) / 状态`

### 7.3 会话级

`时长 · 总 tick · 总块 · 丢块 · 丢点率 · 平均帧率 · 触发次数 · 记录体积`

### 7.4 交互

- 列可拖动排序、可勾选隐藏、可冻结首列
- 点某行 → 主图切到"单通道"模式显示该通道
- 数值超阈值 → 该格红底
- 右键 → 复制行 / 导出该通道统计 CSV
- 全部指标**增量计算**，O(1) 每块，不做全量重扫

---

## 8. 视觉规范

### 8.1 配色

| 用途 | 值 |
|---|---|
| 窗口背景 | `#16181D` |
| 侧栏 / 面板 | `#1E2127` |
| 卡片 | `#23272F` |
| 卡片边框 | `#2E333D` |
| 主文字 | `#E6E8EC` |
| 次文字 | `#8B93A1` |
| 录制/危险 | `#E24B4A` |
| 正常/运行 | `#43D17A` |

**通道色**

| CH | 色值 | | TH | 色值 |
|---|---|---|---|---|
| CH0 | `#4A9EFF` 蓝 | | TH0 | `#FF8A65` 橙红 |
| CH1 | `#43D17A` 绿 | | TH1 | `#FFD54F` 黄 |
| CH2 | `#FFB020` 琥珀 | | | |
| CH3 | `#B07CFF` 紫（大电流，边框可加粗区分） | | | |

### 8.2 字体

- UI：Inter（已引 `Avalonia.Fonts.Inter`）
- 数字：`Consolas` / `Cascadia Mono`，等宽右对齐，避免跳位

### 8.3 功率卡内容

```
┌ ● CH0  电池放电 ✎ ────────── 正常 ┐   默认标签 CH0–CH3，可改
│  12.483 V          2.104 A         │   大号读数（等宽）
│  26.27 W     纹波 60 mV (0.48%)    │
│  ▁▂▃▅▇▅▃▂ 近 60 s                  │   Sparkline
│  [5.000 A ▰▰▰▱▱]                   │   量程条按 fs_current_ma
│  [校准] [尖峰阈值 12.6V] [☑]       │
└────────────────────────────────────┘
```

- **标签可改**：点 ✎ 就地编辑，默认 `CH0`–`CH3`，存在上位机侧（不占设备 Flash）
- **量程条按 `fs_current_ma` 画**：CH0–2 是 5 A 刻度，CH3 是 20 A 刻度
- **电压刻度按本通道 `v_fs_dv`**：CH0 14.2 V / CH1 28.1 V / CH2 75.9 V / CH3 102.3 V —— 四路不同，不能共用一个 0–100 V 轴
- 每张卡角落常驻显示本路量程与分辨率，如 `0–14.19 V · 3.46 mV`
- **CH3 加"大电流"角标**，并提示"分辨率 9.77 mA"（比 05B 差 4 倍，是选它的代价）
- 电压超 90% 量程变黄、超 100% 变红

> 主曲线在"叠加"模式下必须**按通道分别归一化**（各自除以本路 `v_fs_dv`），否则 14 V 的 CH0 曲线会被 102 V 的 CH3 压成一条直线。或者在"堆叠"模式下每通道独立 Y 轴。

### 8.4 温度带

```
TH0  25.1 ℃  ▁▂▃▂▁  [DS18B20 · 12bit]   TH1  26.1 ℃  ▁▁▂▁   板载 31.2 ℃
```

- 默认标签 `TH0` / `TH1`，同样可重命名
- 显示当前分辨率（9/10/11/12 bit）；改分辨率要 ABORT 后重开
- **探头掉线**：显示 `--` 变红（对应 `EVENT TEMP_LOST`）
- **85 ℃ 陷阱**：若收到值恰为 `0x0550`，一律显示 `--` 而不是 85.0 ℃，并在日志记一条"探头未完成转换"

---

## 9. `.pdl` 文件格式

```
[Header]  magic "PDLF" | ver | device info | 4 功率配置 | 2 温度配置 | 参数
[Record]* 变长：u8 type | u16 len | payload | u32 crc
  0x01 Block(功率 RAW)
  0x02 Block(功率 BIN)
  0x03 Block(温度)
  0x04 Event
  0x05 SyncMarker
  0x06 ConfigSnapshot     ← 会话开始时写一次（运行中不改配置，所以只有快照）
[Index]   (offset, sample_index, t_ms, isTemp) —— 秒开 + 随机跳转
[Footer]  summary | record count | file crc
```

- 追加写，读到坏记录即停，已写数据仍可用
- 写入 64 KB 或 1 s 触发 flush + fsync
- 保存 = 写 Index + Footer + 改名为 `.pdl`

---

## 10. 联调

```bat
python simulator\gui.py --listen 127.0.0.1:5555 --hid-pacing on --ram 10240
dotnet run --project PDL_upper\PDL_upper.csproj -- --tcp 127.0.0.1:5555
```

建议同时提供：

| 开关 | 用途 |
|---|---|
| `--selftest` | 只跑编解码 golden vector，不开窗口 |
| `--ui-demo` | 不连设备，灌假数据看界面（调样式极省事） |
| `--record <file>` | 启动即录，无人值守 |

---

## 11. 已知坑（继承 51_PWMCtrol 的踩坑记录）

| 现象 | 原因 | 处理 |
|---|---|---|
| `dotnet restore` 报 `Parameter 'path1'` | 本机 SDK 的 NuGet 配置解析 | 用 VS 的 `MSBuild.exe -t:Restore`，之后 `--no-restore` |
| 界面发虚 / 右侧被裁 | 缺 DPI 感知 | `app.manifest` 声明 + `SetProcessDpiAwarenessContext` |
| 自定义 `Control` 不显示 | 没实现 `MeasureOverride`，尺寸算成 0 | 自绘控件必须重写 |
| 4 张卡片挤不下 | 固定列宽 | `Grid` 用 `*,*,*,*` + 卡片最小宽度，窄窗口给横向滚动 |
| 高刷数据把 UI 卡死 | 每帧 Post | 33 ms 批量刷新 |
| 编译绑定报错难查 | 缺 `x:DataType` | 每个 UserControl 都写 |
| 曲线掉帧 | 每帧 new Pen/Brush | 缓存成字段 |

---

## 12. 实现顺序

1. `Models/Protocol` + `--selftest`（golden vector 跑通，不碰 UI）
2. `TcpTransport` + `DeviceSession`，控制台跑通 IDENTIFY → READ_TEMP → ACQUIRE → 收块 → ABORT
3. `MainWindow` 骨架 + `TopBar` + `ConnectionPanel`
4. `PowerCard ×4` + `TempStrip` + `PowerChart`（最花时间）
5. `AcquirePanel` + 参数下发与回填 + 运行中禁用
6. `Pyramid` + `SessionPanel` + 保存/回放
7. `TriggerPanel` + V/I/P 触发联调
8. `StatisticsPanel` 全指标 + 导出 + 样式打磨
