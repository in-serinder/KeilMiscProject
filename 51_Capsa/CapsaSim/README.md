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
| `meta_24c02.bin` | 256 B | 存储卡元信息（初始化标志、WP/健康掩码、名称、已使用空间、数据哈希、写入次数、UID 等） |

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
| 写入完成 | `UG_<字节数>` + `DI_<哈希>_<次数>` | 上报占用、整卡哈希与写入次数（哈希由上位机算，次数由上位机 +1） |
| 重新初始化 / 格式化 | 擦数据区 → `UG_0` → `DI_<全0哈希>_!` | 占用归零、旧哈希失效；**累计写入次数原样保留**（寿命指标） |
| 读取完成 | `DIG` | 取回哈希与次数并比对；不一致只提示，不中断读取 |
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

## 8. 验证数据哈希校验（故意破坏数据）

哈希校验在**上位机**做：写完后把整卡数据的 FNV-1a 哈希与次数用 `DI` 存进 24C02，
读取后用 `DIG` 取回来比对。想验证它能发现问题，按下面的顺序操作（顺序错了会“看起来通过”）：

```bash
# 1) 停止模拟器（Ctrl+C）——它只在启动时读取 bin
# 2) 翻掉 0x50 的一个字节
python corrupt_eeprom.py data/eeprom_0x50_24C64.bin --offset 3
# 3) 重新启动模拟器
python capsa_sim.py --port COM2 --data-dir ./data
# 4) 上位机点「Read & Verify」
```

预期：状态栏变成

```text
Read complete · 1,253 bytes · used space 1.2 KB · HASH MISMATCH (stored A1B2…, computed 9C7E…)
```

并弹出 `Data may be corrupted` 提示（数据仍会装载，不会丢弃）。

### 为什么有时候“破坏后依然通过”

| 情况 | 现象 | 说明 |
| --- | --- | --- |
| 模拟器**运行中**改 bin | 通过 | bin 只在启动时读入内存；运行期改文件无效，下次写包还会把内存写回覆盖掉 |
| 改了**另一片**或**另一个型号**的文件 | 通过 | 例如实际是 `eeprom_0x50_24C64.bin`，却改了 `..._24C128.bin` |
| 改的偏移 **大于已使用空间** | 通过 | 哈希只覆盖 `0 ~ UGS-1`；空白区（0xFF）不参与校验 |
| 破坏后又用上位机写了一次 | 通过 | 写入会把哈希更新为新数据的值 |
| 卡上没有哈希记录（旧卡） | 通过（但会提示） | 状态栏显示 `no hash record on card` |
| `UGS = 0` 时的整卡回退读取 | 通过（但会提示） | 状态栏显示 `hash not checked (read … B, used 0 B)` |

> 上位机现在**无论结论如何**都会在状态栏写明 `hash OK` / `HASH MISMATCH` / `no hash record on card` /
> `hash not checked`，不会再出现“悄悄跳过、看起来正常”的情况。
