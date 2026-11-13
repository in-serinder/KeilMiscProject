# PDL HID 通信协议规范

> 版本：**PDL/1.4-draft**
> 适用：STM32F103C6T6 下位机 ↔ Avalonia 上位机；Python 模拟器 ↔ 上位机（同一套字节流）
> 字节序：**全部小端（Little-Endian）**

---

## 0. 版本变更

### v1.4（本次）
| 变更 | 原因 |
|---|---|
| **4 路电压量程各自独立**（4.3 / 8.5 / 23 / 31），满量程 14.2 / 28.1 / 75.9 / 102.3 V | 你定了 CH0=12V, CH1=24V, CH2=64V, CH3=100V |
| `v_fs_dv` 从 `IDENTIFY` **移到 `SET_CH_CONFIG` / `GET_CH_CONFIG`**（每通道一个） | 设备级一个值表达不了四路不同量程 |
| `IDENTIFY` 载荷 34 → **32 B**（去掉全局 `v_fs_dv`，`fs_current_ma[4]` 前移） | |
| `caps` bit7 改为"各通道量程不同" | 上位机据此决定是否逐通道读配置 |
| 上位机建链流程增加"逐通道 `GET_CH_CONFIG`"一步 | 不读就不知道每路满量程 |

### v1.3
温度改 DS18B20（1-Wire 数字）；`data_fmt=7 TEMP_C16`；新增 `temp_res_bits`；电流量程 5A×3 + 20A×1。
### v1.2
4 功率 + 2 温度；删 `UPDATE_PARAMS`（运行中禁改采样）；V/I/P 三源触发。
### v1.1
单指令 `ACQUIRE`、`ABORT` 排空、触发消抖、四档 tier、BIN 统计块。

---

## 1. 传输层

### 1.1 USB 描述符

| 项 | 取值 |
|---|---|
| 接口类 | 0x03 HID，SubClass/Protocol 0x00 |
| 端点 | EP1 IN + EP1 OUT，Interrupt |
| `bInterval` | 1（1 ms） |
| Report | 单 64 B IN + 单 64 B OUT，**无 Report ID** |
| Usage Page / Usage | 0xFF00 / 0x0001 |

F103C6T6 只有 USB 全速：**理论上限 64 kB/s、1000 帧/s**，实际可用 ~50 kB/s。

### 1.2 报文

`REPORT_SIZE = 64`；帧 = 头(8) + 载荷(0–54) + CRC(2)。

### 1.3 传输抽象

```csharp
void Write(ReadOnlySpan<byte> frame);
int  Read(Span<byte> buffer, int timeoutMs);
```

真机 `HidTransport`，模拟器 `TcpTransport`（按 64 B 切块）。

---

## 2. 帧格式

```
偏移 长度 字段
 0    1   SOF = 0xA5
 1    1   VER = 0x01
 2    1   TYPE
 3    1   FLAGS
 4    2   LEN   0..54
 6    2   SEQ
 8    LEN PAYLOAD
8+LEN 2   CRC16  CRC-16/CCITT-FALSE over [0 .. 8+LEN-1]
```

### 2.1 CRC

poly `0x1021`，init `0xFFFF`，不反射，无终异或，u16 小端。

```c
static uint16_t pdl_crc16(const uint8_t *p, uint16_t n) {
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*p++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}
```

### 2.2 FLAGS

| 位 | 名称 | 值 | 含义 |
|---|---|---|---|
| 0 | `RESP` | 0x01 | 应答帧 |
| 1 | `EVENT` | 0x02 | 异步事件 |
| **2** | **`FRAG_FIRST`** | **0x04** | **块首片** |
| **3** | **`FRAG_MORE`** | **0x08** | **后面还有分片** |
| 4–7 | 保留 | — | 0 |

分片四态（`FIRST`,`MORE`）：

| FIRST | MORE | flags | 含义 |
|---|---|---|---|
| 1 | 0 | `0x04` | 单片，块完整 |
| 1 | 1 | `0x0C` | 首片，后面还有 |
| 0 | 1 | `0x08` | 中间片 |
| 0 | 0 | `0x00` | 末片 |

> 位序与最初的草案相反（早期版本写的是 bit2=MORE），以本节和模拟器实现为准。
> 温度块用 `FLAGS=0x04`（单片），`TEMP_BLOCK` 是**块头 flags 的 bit7**，不要放到帧 FLAGS 里。

---

## 3. 分片与重组

1. 只有 `DATA_BLOCK (0x90)` 可分片
2. 同块分片必须连续；事件撞上则推迟
3. 首片以 14 B 块头开头，后续片直接续接
4. 按 tick 边界切分
5. 首片后若 SEQ 断或非 DATA_BLOCK → 丢弃整块并计数

---

## 4. 消息类型

### 4.1 上位机 → 下位机

| TYPE | 名称 | 作用 |
|---|---|---|
| `0x10` | **IDENTIFY** | 识别本项目下位机 |
| `0x11` | GET_CH_CONFIG | 读回通道配置 |
| `0x20` | **ACQUIRE** ★ | 单指令：使能通道 + 全部参数 + 记录模式 + 触发 |
| `0x21` | READ_TEMP_ONCE | 读一次当前温度（不采集时可用） |
| `0x22` | **ABORT** ★ | 立即停采样 → 排空 → 汇总 |
| `0x23` | GET_STATUS | 状态与健康度 |
| `0x30` | SET_CH_CONFIG | 换算系数 / 零点 / 温度分辨率 |
| `0x31` | ZERO | 零点标定 |
| `0x32` | SAVE_CONFIG | 写 Flash |
| `0x40` | TIME_SYNC | 时间基准 |
| `0x60` | RESET | 软复位 |
| `0x7F` | ECHO | 调试回环 |

### 4.2 下位机 → 上位机

`0x80 ACK` / `0x81 NACK` / `0x90 DATA_BLOCK` / `0xA0 EVENT` / `0xA1 HEARTBEAT`

---

## 5. 指令详解

> 所有命令必须有应答；超时 200 ms 重试 3 次（`ABORT` 例外：2 s）。
> ACK 前 4 字节固定：`u8 cmd, u8 status, u16 detail`。

### 5.1 IDENTIFY (0x10)

**请求**：`u32 nonce`

**应答 extra（32 B）**

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | u8[4] | `magic` | `"PDL1"` |
| 4 | u16 | `proto_min` | 1 |
| 6 | u16 | `proto_max` | 4 |
| 8 | u16 | `fw_version` | `major<<8 \| minor` |
| 10 | u16 | `hw_rev` | |
| 12 | u8 | `power_channels` | 4 |
| 13 | u8 | `temp_channels` | 2 |
| 14 | u8 | `caps` | 见下 |
| 15 | u8 | `max_report_size` | 64 |
| 16 | u16 | `max_sample_rate_hz` | 20000 |
| 18 | u16 | `max_block_ticks` | 1024 |
| 20 | u16 ×4 | `fs_current_ma[0..3]` | 例 5000/5000/5000/20000 |
| 28 | u32 | `nonce` | 回显 |

**caps**

| 位 | 能力 |
|---|---|
| 0 | RAW 模式 |
| 1 | BIN 统计模式 |
| 2 | 四档 tier |
| 3 | 温度通道（DS18B20） |
| 4 | 电平触发（V/I/P） |
| 5 | 定时模式 |
| 6 | 设备侧滚动缓冲（预留） |
| 7 | **各通道电压量程不同**（per-channel range） |

> **v1.4 重要变化**：`v_fs_dv` 从 IDENTIFY 里**移出**，改为每通道独立，放在 `GET_CH_CONFIG` / `SET_CH_CONFIG` 里。
> 原因：CH0–CH3 的分压比不同（4.3 / 8.5 / 23 / 31），设备级一个值表达不了。
>
> 上位机连接流程因此变为：**IDENTIFY → 逐通道 GET_CH_CONFIG ×4**（+ 温度 ×2），拿到每路的 `v_fs_dv` / `fs_current_ma` / 换算系数后再建 UI。

### 5.2 ACQUIRE (0x20) ★

**请求载荷（48 B）**

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | u8 | `power_mask` | bit0..3，非 0 |
| 1 | u8 | `temp_mask` | bit0..1，可为 0 |
| 2 | u8 | `record_mode` | 0 手动 / 1 定时 / 2 触发 |
| 3 | u8 | `tier` | 0..3 |
| 4 | u32 | `sample_rate_hz` | 功率采样率 |
| 8 | u32 | `report_interval_ms` | 功率上报间隔 |
| 12 | u32 | `bin_ms` | BIN 桶宽，0 = 用 tier 预设 |
| 16 | u32 | `duration_s` | 0 = 不限 |
| 20 | u32 | `temp_interval_ms` | 温度上报周期 |
| 24 | u8 | `trig_source` | 0=V, 1=I, 2=P |
| 25 | u8 | `trig_ch` | 0..3 |
| 26 | u8 | `trig_flags` | bit0 开触发, bit1 关触发, bit2 自动重武装, bit3 到时长即停 |
| **27** | **u8** | **`temp_res_bits`** | **9/10/11/12，0 = 默认 12** |
| 28 | u16 | `start_hold_ms` | 开触发消抖 |
| 30 | u16 | `stop_hold_ms` | 关触发消抖 |
| 32 | i32 | `start_val` | 阈值，单位随 source：mV / mA / mW |
| 36 | i32 | `stop_val` | 同上 |
| 40 | u32 | `post_trigger_ms` | 触发关后继续录多久 |
| 44 | u32 | `pre_trigger_ms` | 预留，当前必须 0 |

**校验规则（失败 → `BAD_PARAM`，`detail` = 出错字段偏移）**

| 规则 | 说明 |
|---|---|
| `power_mask & 0xF0 == 0` 且 ≠0 | |
| `temp_mask & 0xFC == 0` | |
| `1 <= sample_rate_hz <= max_sample_rate_hz` | |
| `1 <= report_interval_ms <= 60000` | |
| `sample_rate_hz × report_interval_ms / 1000 <= max_block_ticks` | |
| `record_mode==2` 时 `(trig_flags&0x03)!=0` 且 `trig_ch < 4` | |
| `temp_res_bits ∈ {0,9,10,11,12}` | |
| **`temp_interval_ms >= 转换时间(temp_res_bits)`** | 9→100, 10→200, 11→400, 12→800 ms（含余量） |
| `bin_ms==0` 或 `1..60000` | |

**温度转换时间约束**（DS18B20 硬限制）

| 分辨率 | 理论转换 | 建议最小间隔 |
|---|---|---|
| 9 bit (0.5 ℃) | 93.75 ms | 100 ms |
| 10 bit (0.25 ℃) | 187.5 ms | 200 ms |
| 11 bit (0.125 ℃) | 375 ms | 400 ms |
| **12 bit (0.0625 ℃)** | 750 ms | **800 ms** |

> 想快速响应就用 9/10 bit；想精度高就 12 bit、间隔 ≥800 ms。
> 对"看设备发热"这种场景，12 bit + 1 s 最合适。

**各记录模式**

| mode | 收到后 | 结束 |
|---|---|---|
| 0 手动 | 立即 `RUNNING` | 只有 `ABORT` |
| 1 定时 | 立即 `RUNNING` | `duration_s` 到 → 自动收尾 |
| 2 触发 | `ARMED`，只发温度块与心跳 | 开触发满足 → `RUNNING`；关触发或到时长 → 收尾 |

**应答 extra（20 B）**

| 偏移 | 类型 | 字段 |
|---|---|---|
| 0 | u8 | `state`（0x02 ARMED / 0x03 RUNNING） |
| 1 | u8 | `eff_tier` |
| 2 | u16 | `eff_bin_ms` |
| 4 | u32 | `eff_sample_rate_hz` |
| 8 | u32 | `eff_report_interval_ms` |
| 12 | u32 | `first_sample_index` |
| 16 | u16 | `eff_temp_interval_ms` |
| 18 | u8 | `eff_temp_res_bits` |
| 19 | u8 | `reserved` |

> ACK 必须先于第一个数据块。固件顺序：校验 → 回 ACK → 配 ADC/DMA + 启动 1-Wire 转换 → 开始 tick。

### 5.3 READ_TEMP_ONCE (0x21)

无载荷，**不进入采集状态**，立刻读两个 DS18B20（最长阻塞 750 ms，超过 12 bit 时先降分辨率）。

**应答 extra**：`i16 temp_c16[0..1]`（1/16 ℃，0x7FFF = 探头不存在）

用途：连接后立刻显示室温；验证探头是否接反/掉线。

### 5.4 ABORT (0x22) ★

**请求**：`u8 flags`（bit0 = 丢弃未发数据）

```
1. 立即停 ADC 采样与 tick 计数（时间轴封口）
2. flags.discard==0：发完缓冲内剩余块 + 一个末温度块，末块置 FINAL
3. 回 ACK + 汇总
```

**应答 extra（24 B）**：`final_state, stop_reason, rsv, total_ticks, total_blocks, dropped_ticks, duration_ms, trig_count`

> 上位机 ABORT 超时设 2 s。排空中的块仍计数。

### 5.5 GET_STATUS (0x23)

**应答 extra（44 B）**

| 偏移 | 类型 | 字段 |
|---|---|---|
| 0 | u8 | `state` |
| 1 | u8 | `power_mask` |
| 2 | u8 | `temp_mask` |
| 3 | u8 | `error_flags`（bit0 溢出, bit1 饱和, bit2 过温, bit3 校准失效, bit4 温度探头掉线） |
| 4 | u32 | `sample_rate_hz` |
| 8 | u32 | `report_interval_ms` |
| 12 | u32 | `total_ticks` |
| 16 | u32 | `total_blocks` |
| 20 | u32 | `overrun_blocks` |
| 24 | u32 | `dropped_ticks` |
| 28 | u16 | `buffer_level` |
| 30 | u16 | `buffer_capacity` |
| 32 | i16 | `temperature_c10`（板载） |
| 34 | u32 | `uptime_s` |
| 38 | u16 | `trig_state`（0 未武装, 1 等开, 2 已触发, 3 等关） |
| 40 | i16 ×2 | `temp_c16[0..1]` |

### 5.6 SET_CH_CONFIG (0x30)

**载荷（28 B）**

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | u8 | `ch` | 0..3 功率，4..5 温度 |
| 1 | u8 | `enabled` | |
| 2 | u8 | `sensor_type` | 功率固定 0；温度固定 1（DS18B20） |
| 3 | u8 | `temp_res_bits` | 仅温度通道：9..12 |
| 4 | u32 | `v_num` | 电压：raw→mV 分子 |
| 8 | u32 | `v_den` | 分母 |
| 12 | u32 | `i_num` | 电流：raw→mA 分子 |
| 16 | u32 | `i_den` | 分母 |
| 20 | i16 | `v_off` | 零点（raw） |
| 22 | i16 | `i_off` | 温度通道：用该字段存 1/16 ℃ 偏移修正 |
| 24 | u16 | `fs_current_ma` | 满量程电流，仅功率通道 |
| 26 | u16 | `v_fs_dv` | **该通道电压满量程 ×0.1 V**（v1.4 新增，原 reserved） |

**换算**

```
电压 mV  = (v_raw - v_off) * v_num / v_den
电流 mA  = (i_raw - i_off) * i_num / i_den
功率 mW  = 电压mV * 电流mA / 1000        // 上位机 i64
温度 ℃   = (temp_c16 + i_off) / 16      // DS18B20 原生就是 1/16 ℃
```

**出厂理论系数（每通道独立，Vref 3.3 V、12 bit）**

| 通道 | 分压比 | `v_fs_dv` | 电压 num/den | 每 count |
|---|---|---|---|---|
| CH0 | 4.3 | `142`（14.2 V） | `346/100` | **3.46 mV** |
| CH1 | 8.5 | `281`（28.1 V） | `685/100` | **6.85 mV** |
| CH2 | 23 | `759`（75.9 V） | `1853/100` | **18.53 mV** |
| CH3 | 31 | `1023`（102.3 V） | `2498/100` | **24.98 mV** |

| 通道 | 电流器件 | 满量程 | 电流 num/den | 每 count |
|---|---|---|---|---|
| CH0–CH2 | ACS712-05B | 5000 mA | `2441/1000` | 2.441 mA |
| CH3 | ACS712-20A | 20000 mA | `9766/1000` | 9.766 mA |

> 以上是理论值，实际用 `ZERO` + 两点实测校准修正。`num/den` 用有理数是因为 F103 无 FPU。
> **上位机不能假设四路系数相同** —— 必须逐通道读回。

### 5.7 ZERO (0x31) / SAVE_CONFIG (0x32)

`ZERO`：`u8 power_mask, u8 what`(bit0=V, bit1=I), `u16 samples` → 短接/空载时求平均写 `v_off/i_off`。
`SAVE_CONFIG`：无载荷，`detail` = 写入字节数。

### 5.8 TIME_SYNC (0x40) / RESET (0x60) / ECHO (0x7F)

TIME_SYNC：请求 `u64 host_mono_us`，应答 `u64 host_mono_us, u64 device_mono_us`。
`rtt = now - host_mono_us`；`offset = device_mono_us - (host_mono_us + rtt/2)`。

---

## 6. 数据块

### 6.1 块头（14 B，仅首片）

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | u32 | `sample_index` | 功率首 tick 序号；温度块填当时的 tick 序号 |
| 4 | u32 | `t_first_ms` | 设备单调毫秒 |
| 8 | u16 | `block_seq` | 功率块与温度块共用一个序列 |
| 10 | u8 | `count` | RAW: tick 数；BIN: 桶数；TEMP: 点数 |
| 11 | u8 | `channel_mask` | 按 `data_fmt` 解释 |
| 12 | u8 | `flags` | |
| 13 | u8 | `data_fmt` | |

**flags**

| 位 | 名称 |
|---|---|
| 0 | `SATURATED` |
| 1 | `OVERRUN` |
| 2 | `CFG_CHANGED`（预留） |
| 3 | `FIRST` |
| 4 | `CAL_INVALID` |
| 5 | `TRIGGERED` |
| 6 | `FINAL` |
| 7 | `TEMP_BLOCK` |

### 6.2 data_fmt

| 值 | 名称 | 单通道字节 | 说明 |
|---|---|---|---|
| 0 | `RAW_COUNTS16` | 4（tick） | `i16 v, i16 i`，**默认** |
| 1 | `RAW_COUNTS32` | 8（tick） | 未来 24 bit ADC |
| 2 | `ENG_UV_UA32` | 8（tick） | 工程单位 |
| 3 | `ENG_MV_MA16` | 4（tick） | 压缩工程单位 |
| 4 | `BIN_STATS16` | 16（桶） | 功率 min/max/avg/rms |
| 5 | `BIN_STATS32` | 32（桶） | 同上 i32 |
| **7** | **`TEMP_C16`** | **2（点）** | `i16 = ℃×16`（DS18B20 原生） |

> 6 号留空（原 v1.2 的 `TEMP_COUNTS16`，DS18B20 不需要）。

### 6.3 功率 RAW 块体

```
for tick in 0..count-1:
    for ch in 0..3 where (power_mask>>ch)&1:
        i16 v_raw, i16 i_raw
```

### 6.4 功率 BIN 块体（每通道每桶 16 B）

`i16 v_min, v_max, v_avg, v_rms, i_min, i_max, i_avg, i_rms`

排列 `for bin: for ch: <16 B>`。交流纹波由上位机算：
`Vpp = (v_max - v_min) × k`，`Vrms_ac = sqrt(v_rms² - v_avg²) × k`

### 6.5 温度块体

```
for p in 0..count-1:
    for ch in 0..1 where (temp_mask>>ch)&1:
        i16 temp_c16          // ℃ × 16
```

例：`0x0191 = 401` → 401/16 = **25.0625 ℃**

对齐：`sample_index` 填采样时的功率 tick 序号，`t_first_ms` 是精确时刻。上位机用 `t_first_ms` 插值到功率时间轴。

---

## 7. 事件 EVENT (0xA0)

`u8 code, u8 ch, u16 param, u32 t_ms`

| code | 名称 | param |
|---|---|---|
| 0x01 | `OVERRUN` | 丢弃块数 |
| 0x02 | `SATURATE` | 0 |
| 0x03 | `OVERTEMP` | 温度×0.1 |
| 0x04 | `CAL_INVALID` | 0 |
| 0x05 | `SELF_TEST_FAIL` | 位图 |
| 0x06 | `CFG_APPLIED` | 预留 |
| 0x07 | `LOG_FULL` | 预留 |
| 0x08 | `USB_RESET` | 0 |
| 0x09 | `HW_FAULT` | 故障码 |
| 0x0A | `TRIG_ARMED` | 阈值 |
| 0x0B | `TRIG_STARTED` | 实际值 |
| 0x0C | `TRIG_STOPPED` | 实际值 |
| 0x0D | `AUTO_STOP` | stop_reason |
| 0x0E | `ABORTED` | 0 |
| **0x0F** | **`TEMP_LOST`** | **ch**（DS18B20 无应答/CRC 错） |

> DS18B20 断线很常见（线长、上拉不足）。设备必须上报 `TEMP_LOST` 而不是静默给 85 ℃（DS18B20 上电默认值 85 ℃ 是经典坑，上位机要能识别并标灰）。

---

## 8. 状态码

| 值 | 名称 | 上位机应对 |
|---|---|---|
| 0x00 | `OK` | — |
| 0x01 | `UNKNOWN_CMD` | 版本不匹配 |
| 0x02 | `BAD_PARAM` | 高亮 `detail` 字段 |
| 0x03 | `BAD_STATE` | 运行中禁改 |
| 0x04 | `BUSY` | 延时重试 |
| 0x05 | `CRC_ERROR` | 重发 |
| 0x06 | `LEN_ERROR` | 重发 |
| 0x07 | `UNSUPPORTED` | 隐藏 UI |
| 0x08 | `NOT_CALIBRATED` | 引导标定 |
| 0x09 | `OVERFLOW` | 降采样率或升 tier |
| 0x0A | `NO_DATA` | — |
| 0x0F | `INTERNAL` | 记日志 |

---

## 9. 状态机

`BOOT 0` `IDLE 1` `ARMED 2` `RUNNING 3` `DRAINING 4` `ERROR 5`

```
BOOT → IDLE
        │ ACQUIRE(mode 0/1) ──────────► RUNNING ──ABORT──► DRAINING → IDLE
        │ ACQUIRE(mode 2) ──► ARMED ──触发开──► RUNNING
        │                       ▲                │
        │                       └─自动重武装─────┘
        │                    RUNNING ─到时长/触发关─► DRAINING → IDLE
任意 ─致命故障─► ERROR ─RESET─► BOOT
```

| 状态 | 允许命令 |
|---|---|
| `IDLE` | IDENTIFY / GET_STATUS / GET_CH_CONFIG / SET_CH_CONFIG / ZERO / SAVE_CONFIG / READ_TEMP_ONCE / ACQUIRE / TIME_SYNC / RESET |
| `ARMED` | IDENTIFY / GET_STATUS / ABORT / READ_TEMP_ONCE |
| `RUNNING` | IDENTIFY / GET_STATUS / ABORT / READ_TEMP_ONCE |
| `DRAINING` | GET_STATUS / IDENTIFY |
| `ERROR` | **IDENTIFY / RESET（永远可用）** |

**运行中禁止**：`ACQUIRE` / `SET_CH_CONFIG` / `ZERO` / `SAVE_CONFIG` → `BAD_STATE`。

---

## 10. 时序

### 10.1 建链

```
 │ ── IDENTIFY(nonce) ─────────────►│
 │ ◄─ ACK{magic, caps(bit7=1), fs_current_ma[], nonce} │
 │ ── GET_CH_CONFIG(0) ───────────►│   逐通道读，4 次
 │ ◄─ ACK{v_fs_dv, v_num/den, i_num/den, ...}          │
 │ ── GET_CH_CONFIG(1..3) ────────►│
 │ ◄─ ACK{...} ─────────────────── │   ← 拿到每路真实量程
 │ ── GET_CH_CONFIG(4..5) ────────►│   温度通道
 │ ◄─ ACK{res_bits, offset} ────── │
 │ ── TIME_SYNC(t1) ──────────────►│
 │ ◄─ ACK{t1, t_dev} ───────────── │
 │ ── READ_TEMP_ONCE ─────────────►│
 │ ◄─ ACK{temp_c16[0..1]} ──────── │   ← 立刻显示室温
 │ ── GET_STATUS ─────────────────►│
 │ ◄─ ACK{IDLE} ────────────────── │
```

> 建链必须读 6 次 `GET_CH_CONFIG`。少读一次，CH3 就会按 CH0 的 14.2 V 量程显示，读数会差 7 倍。

### 10.2 手动采集

```
 │ ── ACQUIRE{power_mask=0F, temp_mask=03, mode=0, tier=0, 1000Hz, 100ms, temp 1000ms/12bit} ─►│
 │ ◄─ ACK{state=RUNNING, eff_*} ────────────────────────────────────────────────────────────── │
 │ ◄─ DATA_BLOCK(功率首片, mask=0F, count=2, FLAGS=FRAG_FIRST|FRAG_MORE) ───────────────────── │
 │ ◄─ DATA_BLOCK(功率续片) ... 每 100 ms 一批 ...                                              │
 │ ◄─ DATA_BLOCK(温度, FLAGS=FRAG_FIRST|TEMP_BLOCK, count=1) ← 每秒一次                        │
 │ ── ABORT{0} ───────────────────────────────────────────────────────────────────────────────►│
 │ ◄─ DATA_BLOCK(剩余 + 末温度块, FINAL=1) ◄─ ACK{total_ticks, duration_ms} ─────────────────── │
```

### 10.3 触发

```
 │ ── ACQUIRE{mode=2, trig_source=2(功率), trig_ch=0, start=50000mW, hold=50ms} ─►│
 │ ◄─ ACK{ARMED} ◄─ EVENT{TRIG_ARMED} ─────────────────────────────────────────── │
 │   ... 设备按 tick 判定（不受 100 ms 通讯延迟影响）...
 │ ◄─ EVENT{TRIG_STARTED, 50300mW} ────────────────────────────────────────────── │
 │ ◄─ DATA_BLOCK(flags|=TRIGGERED) ────────────────────────────────────────────── │
 │   ... 功率回落并保持 50ms ...
 │ ◄─ EVENT{TRIG_STOPPED} ◄─ DATA_BLOCK(FINAL=1) ◄─ ACK ───────────────────────── │
```

---

## 11. 带宽预算（4 功率 + 2 温度）

| 通道数 | 每 tick | 首片 tick | 续片 tick | 帧/tick |
|---|---|---|---|---|
| 4 | 16 B | 2 | 3 | ≈0.34 |
| 3 | 12 B | 3 | 4 | ≈0.27 |
| 2 | 8 B | 5 | 6 | ≈0.18 |
| 1 | 4 B | 10 | 13 | ≈0.09 |

| 配置 | 数据率 | 帧/s | 帧槽 | 结论 |
|---|---|---|---|---|
| 4ch RAW @500 Hz | 8 kB/s | 167 | 17% | ✅ |
| **4ch RAW @1 kHz** | 16 kB/s | 334 | 33% | ✅ 推荐 |
| 4ch RAW @2 kHz | 32 kB/s | 668 | 67% | ✅ |
| 4ch RAW @2.5 kHz | 40 kB/s | 835 | 84% | ⚠️ 上限 |
| 4ch RAW @3 kHz+ | ≥48 kB/s | ≥1000 | 100% | ❌ |
| 4ch BIN 10 ms | 6.4 kB/s | 200 | 20% | ✅ |
| 温度 1 s ×2ch | 4 B/s | 1 | — | 可忽略 |

**结论**：4 通道全开 RAW 天花板 **约 2.5 kHz**。要更高只能减通道或用 BIN。

---

## 12. 字节流示例（golden vector）

以下字节全部由 `simulator/pdl/proto.py` 实际生成，存于 `protocol/vectors/golden.json`。
上位机与固件的编解码必须能一字不差地复现它们。

### EX1 IDENTIFY 请求（14 B）

```
A5 01 10 00 04 00 01 00 44 33 22 11 47 12
```

### EX2 IDENTIFY 应答（46 B，含 ACK 头）

```
A5 01 80 01 24 00 01 00
10 00 00 00                ACK 头：cmd=0x10, status=OK, detail=0
50 44 4C 31                "PDL1"
01 00 04 00                proto 1..4
00 01 01 00                fw 1.0, hw 1
04 02 BF 40                4 功率, 2 温度, caps=0xBF, report_size 64
20 4E 00 04                max 20000 Hz, max_block_ticks 1024
88 13 88 13 88 13 20 4E    fs_current = 5000,5000,5000,20000 mA
44 33 22 11                nonce 回显
E0 DB                      CRC
```

`caps = 0xBF` = RAW|BIN|TIER|TEMP|TRIGGER|TIMED|bit7(各通道量程不同)。

### EX3 GET_CH_CONFIG 应答（CH0，46 B）

```
A5 01 80 01 20 00 03 00
11 00 00 00                ACK 头：cmd=0x11
00 01 00 00                ch=0, enabled=1, sensor=0, res=0
5A 01 00 00                v_num = 346
64 00 00 00                v_den = 100        → 3.46 mV/count
89 09 00 00                i_num = 2441
E8 03 00 00                i_den = 1000       → 2.441 mA/count
00 00 00 08                v_off = 0, i_off = 2048（0 A 在中点）
88 13 8E 00                fs_current_ma = 5000, v_fs_dv = 142 (14.2 V)
7D 5A                      CRC
```

### EX4 ACQUIRE（58 B）

```
A5 01 20 00 30 00 02 00
0F 03 00 00    4 功率全开, 2 温度全开, 手动, tier0
E8 03 00 00    1000 Hz
64 00 00 00    100 ms
00 00 00 00    bin_ms=0
00 00 00 00    duration=0
E8 03 00 00    temp_interval=1000 ms
00 00 03 0C    trig=V / ch0 / flags=0x03 / temp_res=12bit
32 00 32 00    holds 50/50 ms
E0 2E 00 00    start 12000 mV
40 1F 00 00    stop 8000 mV
00 00 00 00    post=0
00 00 00 00    pre=0
DA 65          CRC
```

### EX5 功率块首片（56 B，4ch，2 tick）

```
A5 01 90 04 2E 00 04 00
A0 86 01 00   E8 03 00 00   07 00   02   0F   00   00
00 08 DC 05 | 01 08 DB 05 | 02 08 DA 05 | 03 08 D9 05
00 08 DC 05 | 01 08 DB 05 | 02 08 DA 05 | 03 08 D9 05
01 28
```

`FLAGS=0x04` = FRAG_FIRST 且没有 MORE → **单片完整块**。
块头：`sample_index=100000`、`t=1000ms`、`seq=7`、`count=2`、`mask=0x0F`、`flags=0`、`fmt=0`。
CH0 tick0：`00 08`=2048, `DC 05`=1500；CH1：2049/1499；…

### EX6 功率块中间片（58 B，3 tick）

```
A5 01 90 08 30 00 05 00
02 08 E6 05 | 03 08 E5 05 | 04 08 E4 05 | 05 08 E3 05
02 08 E6 05 | 03 08 E5 05 | 04 08 E4 05 | 05 08 E3 05
02 08 E6 05 | 03 08 E5 05 | 04 08 E4 05 | 05 08 E3 05
09 2A
```

`FLAGS=0x08` = FRAG_MORE，**中间片，没有块头**，48 B 纯数据续接。

### EX7 温度块（30 B，2 通道 1 点）

```
A5 01 90 04 12 00 06 00
C0 D4 01 00   E0 2E 00 00   2A 00   01   03   88   07
91 01 98 01
A7 12
```

块头 flags = `0x88` = `TEMP_BLOCK(0x80) | FIRST(0x08)`，`data_fmt=7`。
`0x0191=401` → 25.0625 ℃（TH0）；`0x0198=408` → 25.5 ℃（TH1）。

> 注意 `TEMP_BLOCK` 在**块头 flags** 的 bit7，不在帧 FLAGS 里。帧 FLAGS 只有 `0x04`（单片）。

### EX8 ABORT 应答（38 B）

```
A5 01 80 01 1C 00 09 00
22 00 00 00    cmd=0x22
01 00 00 00    final_state=IDLE, stop_reason=0(主机), rsv
10 27 00 00    total_ticks = 10000
14 00 00 00    total_blocks = 20
00 00 00 00    dropped_ticks = 0
88 13 00 00    duration_ms = 5000
00 00 00 00    trig_count = 0
69 FE          CRC
```

---

## 13. 版本演进

1. `VER` 只随帧结构变化
2. 语义变化改 `proto_max`
3. 新字段只追加在载荷尾部
4. 不改已有字段偏移与类型
5. 每版维护 golden vector

---

## 14. 固件检查清单（F103C6T6）

- [ ] ADC 扫描：`V0,I0,V1,I1,V2,I2,V3,I3`（8 通道，V/I 相邻）
- [ ] **四路电压换算系数各不相同**（3.46 / 6.85 / 18.53 / 24.98 mV/count），固件里是一张按通道索引的表，不能写死一个值
- [ ] `caps` bit7 必须置 1（各通道量程不同），否则上位机会以为四路同量程
- [ ] **PB0/PB1（ADC IN8/IN9）留空不接**，PCB 上只留测试点
- [ ] DS18B20 走普通 GPIO（PB5/PB6），**两条独立总线**，各带 4.7k 上拉
- [ ] 1-Wire 时序必须用 DWT 或 TIM 做 µs 级延时，**不能用 `for` 空循环**（72 MHz 下编译器优化会打乱）
- [ ] DS18B20 转换期间（750 ms）不能阻塞主循环，用状态机轮询
- [ ] 上电后先读一次 ROM，确认探头存在并缓存 ROM ID（避免每次 ROM 搜索）
- [ ] 读到 `0x0550`（85 ℃ 默认值）要判为"未转换完成"，不能直接上报
- [ ] CRC 查表版
- [ ] 帧解析状态机放主循环，不放 USB ISR
- [ ] 时间戳在 DMA 完成中断里打
- [ ] ACQUIRE 的 ACK 在开 ADC 之前发出
- [ ] 溢出丢最旧块
- [ ] IDENTIFY 在 ERROR 状态也必须应答
- [ ] 触发消抖用 tick 累加，不用 `delay_ms`
- [ ] 功率触发用 `i32` 算 `V_mV × I_mA / 1000`，中间量走 i64
