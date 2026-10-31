# CapsaSim · 下位机模拟器

用于与上位机 `Capsa` 联调的模拟下位机：用内存数组模拟 **I2C1 上 8 片 24Cxx（0x50~0x57）** 与
**I2C2 上的 24C02 元信息（0x50）+ PCF8574 写保护（0x58）**，所有数据持久化到独立 `.bin` 文件。

协议：上位机 `>命令<`，下位机 `[应答]`；数据包与掩码统一用十六进制（`W_FF,0A,…`、`SET_..._00_FF_...`）；完整协议见 **[docs/COMMAND_REFERENCE.md](docs/COMMAND_REFERENCE.md)**。

---

## 1. 运行环境

* Python 3.9+
* [pyserial](https://pypi.org/project/pyserial/)（仅串口模式需要）

```bash
pip install -r requirements.txt
```

## 2. 快速开始

```bash
# 默认：COM1 / 115200 8N1 / ./data
python capsa_sim.py

# 指定端口与数据目录
python capsa_sim.py --port COM3 --data-dir ./data

# 无串口时用标准输入输出模拟字节流（脚本联调）
python capsa_sim.py --stdio --no-delay --data-dir ./.tmpdata
```

启动后控制台输出：

```text
15:05:25 [INFO ] Capsa lower-machine simulator
15:05:25 [INFO ]   transport : COM1 @ 115200 8N1
15:05:25 [INFO ]   data dir  : F:\...\CapsaSim\data
15:05:25 [INFO ]  I2C1      : 8 × 24Cxx @ 0x50-0x57 (default 24C64, 8KB each)
15:05:25 [INFO ]  I2C2      : 24C02 meta @ 0x50 · PCF8574 WP @ 0x58
15:05:25 [INFO ] listening… (Ctrl+C to quit)
```

## 3. 参数

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--port` | `COM1` | 串口端口（Windows `COM3`，Linux `/dev/ttyUSB0`） |
| `--baud` | `115200` | 波特率，固定 8N1 |
| `--data-dir` | `./data` | bin 文件目录，不存在会自动创建 |
| `--rx-timeout` | `2.0` | 读流程等待主机 `[RX-OK]` 的超时（秒） |
| `--stdio` | 关闭 | 用 stdin/stdout 模拟串口；日志改写到 stderr，stdout 只输出协议应答 |
| `--no-delay` | 关闭 | 关闭 I2C 时序 sleep（自动化测试用，正式联调请勿开启） |
| `--log-level` | `INFO` | `DEBUG` / `INFO` / `WARNING` / `ERROR` |

## 4. 目录结构

```text
CapsaSim/
├─ capsa_sim.py                 # 入口：参数解析、串口/stdio 传输、帧解析、日志
├─ capsa_machine.py             # 协议解析 + 状态机（SET/GET/ROUTER/ADDR/读写/ABORT）
├─ capsa_devices.py             # 24Cxx、24C02 元信息、PCF8574 与 bin 持久化
├─ docs/
│  └─ COMMAND_REFERENCE.md      # 《指令文档》（命令表/字段编码/UG·UGS/时序/错误码/测试）
├─ tests/
│  └─ test_capsa_sim.py         # 15 个场景的单元测试
└─ data/                        # 运行时生成（可删除，重建后为 0xFF）
```

## 5. 数据文件

| 文件 | 大小 | 说明 |
| --- | --- | --- |
| `eeprom_0x50_24C64.bin` … `eeprom_0x57_24C64.bin` | 8192 B × 8 | 阵列镜像，`SET` 改型号时按新型号重建 |
| `meta_24c02.bin` | 256 B | 存储卡元信息（初始化标志、WP/健康掩码、名称、已使用空间、UID 等） |

* 文件不存在 → 用 `0xFF` 填满并写盘（等价于“空白芯片”）；
* 每次 `W_` / `SET` 成功都会立即写回 bin（`*.tmp` + `os.replace` 原子写入）；写包期间只 flush，
  `TX-OVER` / `ABORT` 时才 fsync，避免模拟器拖慢串口吞吐；
* 想恢复出厂状态：删除整个 `data` 目录后重启模拟器。

## 6. 测试

```bash
python -m unittest discover -s tests -v
```

15 个用例覆盖任务书第十五节全部场景（初始化、字段编码、写保护、越界、读写流程、超时、ABORT、bin 文件）。

手工联调示例：

```bash
printf '>TYPE-CAPASA<>GET_!<>SET_DEV1_00_FF_64_64_1700000000_1700000100_0_A1B2C3D4E5F6<>GET_!<' \
  | python capsa_sim.py --stdio --no-delay --data-dir ./.tmpdata
```

## 7. 与上位机（Capsa）对接

上位机已实现真实串口对接，链路如下：

| 上位机动作 | 发出的命令 | 依据 |
| --- | --- | --- |
| 点击 Refresh | 对每个 COM 口 `TYPE-CAPASA` | 应答 `[YES]` 的端口打 ★ 并自动选中 |
| 手动选择未探测的口并 Connect | `TYPE-CAPASA` | 失败弹窗提示，成功打 ★ |
| 连接成功 | `GET_!` | 回 `>INFO_...<` 视为已初始化；`[SET_ERR]` 视为未初始化 |
| 未初始化 | 自动打开初始化窗口 | 窗口内 `SET` 写入元信息（写保护默认全关）+ 0x00/0xFF 读写测试 + 回写健康掩码 |
| 写入完成 | `UG_<字节数>` → `UGS` | 上报实际占用；`UGS` 回填左侧容量占用与 8 张卡片进度 |
| Check chip health | `ROUTER/ADDR/INT_W/W_/TX-OVER` ×2 图案 | 错字节数分级 |
| Read & Verify | `ROUTER/ADDR/INT_R` + 逐包 `[RX-OK]` | 数据装入编辑器 |
| Verify & Write | 写流程 + 回读比对 | 不一致即报错 |

芯片健康分级（每片测试窗口默认 256 B，0x00 与 0xFF 各写读一遍）：

| 错字节数 | 分级 | 颜色 |
| --- | --- | --- |
| 0 | HEALTHY | 蓝 |
| 1 - 9 | WARN | 黄 |
| 10 - 20 | MEDIUM | 橙 |
| > 20 | ERR | 红 |

> 测试会覆盖被测窗口内的数据（破坏性），请勿对存有重要内容的卡片执行。

### 上位机侧的环境变量

| 变量 | 作用 |
| --- | --- |
| `CAPSA_MOCK=1` | 使用内存 Mock（无硬件演示 / 截图） |
| `CAPSA_SHOT=<dir>` | 自动走查并截图（配合 `CAPSA_MOCK=1`） |
| `CAPSA_SHOT_REAL=1` | 走查改用“扫描 → 连接 → 初始化”真机流程 |

### 命令行自检

```bash
# 一端跑模拟器，另一端跑上位机的协议自检（COM1 ↔ COM2 桥接）
python capsa_sim.py --port COM2 --data-dir ./data
Capsa.exe --sim-test COM1 sim-test.log
```

`sim-test.log` 会输出 PING / SET / GET / WRITE / READ / 芯片测试 / 传输 / 初始化 / UG·UGS
各环节的 PASS/FAIL。其中 UG 段会打印 156 kbit（19968 B）写入后的分片占用：

```text
UG-SLOTS 0x50:8192B 0x51:8192B 0x52:3584B 0x53:0B 0x54:0B 0x55:0B 0x56:0B 0x57:0B
UG-DEVICE used=19968B array=65536B pct=30.5%
```
