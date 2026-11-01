# Capsa 上位机

Avalonia 11 (net8.0 / MVVM) 桌面工具，用于通过串口操作 8 片 24C 系列 EEPROM
组成的存储卡阵列（地址 0x50 ~ 0x57）。

## 构建与运行

```
dotnet build Capsa/Capsa.csproj --no-restore   # 本机 restore 会失败，必须带 --no-restore
dotnet run --project Capsa --no-build
```

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
- `CapsaDeviceService.FormatAsync`：擦数据区 → `UG_0` → `DI_<全0>_!`（次数传 `!`，不改）。
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
