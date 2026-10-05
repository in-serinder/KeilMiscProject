# STRC.MD — 模拟下位机架构文档

本文档说明模拟下位机的软件架构、模块划分与关键设计决策。

---

## 1. 总体目标

用 Python 模拟一台下位机（STC8G1K08A 单片机 + 多片 24CXX EEPROM）的串口协议行为，用于和上位机联调：

- 不依赖真实 I2C 硬件，EEPROM 用内存数组模拟。
- 数据持久化到独立 bin 文件，保证重启不丢失。
- 帧协议 `>内容<`，115200 8N1。
- 模拟 I2C 速率 250kHz、24CXX 写周期 20ms、读流控 RX-OK 超时 2s。
- 4 位数码管仅做控制台模拟输出。

---

## 2. 目录结构

```
lower-level_simulation/
├── eeprom_sim/              # 模拟下位机源码包
│   ├── __init__.py
│   ├── __main__.py          # 入口：参数解析 + 装配 + 启动
│   ├── devices.py           # 设备表 + EEPROM 内存/bin 持久化
│   ├── i2c_timing.py        # 模拟 I2C 时序（速率/sleep）
│   ├── tm1637.py            # 4 位数码管模拟显示
│   ├── protocol.py          # 帧定义、构建与解析
│   ├── transport.py         # 传输层（真实串口 / stdio）
│   └── state_machine.py     # 命令解析 + 状态机 + 读写流程
├── tools/                   # 上位机测试工具
│   ├── __init__.py
│   ├── host_client.py       # HostClient / SubprocessHost
│   └── demo.py              # 交互式演示
├── tests/
│   └── test_sim.py          # 端到端测试（22 个场景）
├── CMD.md                   # 指令文档
├── STRC.MD                  # 本文档
└── requirements.txt
```

---

## 3. 分层架构

```
                 ┌──────────────────────────────┐
                 │         上位机 (外部)          │
                 └──────────────┬───────────────┘
                                │ >帧< 字节流
                 ┌──────────────▼───────────────┐
                 │  Transport  (transport.py)     │
                 │  SerialTransport / StdioTransport │
                 └──────────────┬───────────────┘
                                │ read_frame() / write_frame()
                 ┌──────────────▼───────────────┐
                 │  Protocol   (protocol.py)      │
                 │  帧构建、字节列表解析、地址解析   │
                 └──────────────┬───────────────┘
                                │
                 ┌──────────────▼───────────────┐
                 │  Simulator  (state_machine.py) │
                 │  命令分发 + 状态机 + 读写流程    │
                 └───────┬───────────────┬───────┘
                         │               │
        ┌────────────────▼──┐     ┌──────▼──────────┐
        │ EepromDevice(s)   │     │ I2CTiming       │
        │ (devices.py)      │     │ (i2c_timing.py) │
        │ 内存 + bin 持久化  │     └─────────────────┘
        └───────────────────┘     ┌─────────────────┐
                                  │ TM1637Sim       │
                                  │ (tm1637.py)     │
                                  └─────────────────┘
```

### 3.1 Transport 层

- 统一接口 `read_frame(timeout)` / `write_frame(body)`，以及 `eof` 属性（输入流是否结束）。
- `read_frame` 返回帧内容字符串；`timeout=None` 表示阻塞等待，`timeout>0` 表示超时返回 `None`。
- **帧间隔 `\r\n`**：`write_frame` 经 `protocol.build` 自动在 `<` 后追加 `\r\n`；`read_frame` 在遇到 `<` 即返回，随后作为帧间间隔出现的 `\r\n`（或单独 `\r`/`\n`）位于“未开始帧”状态，被自然忽略。仅当 `\r`/`\n` 出现在**帧内**（`>` 与 `<` 之间）才判为非法帧丢弃。
- `SerialTransport`：pyserial，115200 8N1，无流控；按**字节**解析（`buf.append(byte)`）。
- `StdioTransport`：用 stdin/stdout 模拟串口字节流。
  - **关键**：stdin 阻塞读无法直接配合超时，因此用**后台线程**持续读字节放入 `queue.Queue`，`read_frame` 用 `queue.get(timeout=...)` 实现精确超时。
- **日志与帧流分离**：所有 `[SIM]` / `[TM1637]` 日志一律输出到 **stderr**，stdout 只承载帧字节。否则 stdio 模式下日志会污染帧流（开发中曾出现该 bug）。
- **已知修复**：`SerialTransport` 早期版本用 `buf.append(ch.encode("ascii"))`，向 `bytearray` 追加 `bytes` 会抛 `TypeError` 导致进程崩溃；现改为按字节追加，并对畸形帧容错。

### 3.2 Protocol 层

- `build(body)`：把内容包成 `>body<`，并在末尾追加标准换行间隔 `\r\n`（常量 `FRAME_EOL`）。
- 应答常量：`ACK_RS` / `ACK_ERR` / `ACK_RXOK` / `ACK_TXOVER` / `ACK_YES`；命令常量含 `CMD_TYPE_ECHO`。
- `parse_data_payload(body)`：解析 `R_[..]` / `W_[..]` 的十六进制列表，校验每项两位。
- `parse_hex_addr(text)`：解析 `0xXXXX` / `XXXX`。
- `format_data_body(prefix, data)`：格式化为大写两位。
- 自定义 `FrameError` 表示格式错误。

### 3.3 State Machine 层

`Simulator` 是核心，持有传输、设备表、时序、显示对象，维护状态与当前操作地址。

**状态集合**：

| 状态 | 含义 |
|---|---|
| ST_IDLE | 空闲 |
| ST_ROUTER_SET | 已设 ROUTER，等待 EDGE |
| ST_EDGE_SET | 已设 EDGE，等待 INT_R / INT_W |
| ST_WRITE_WAIT | 写等待（已 INT_W） |
| ST_WRITE_CURR | 正在写当前包 |
| ST_WRITE_OVER | 当前包写完成 |
| ST_READ_WAIT | 读等待 |
| ST_READ_CURR | 正在读当前包 |
| ST_READ_OVER | 当前包读完成 |

**命令分发**（`_handle`）：按精确匹配/前缀分派到 `_cmd_type_echo` / `_cmd_router` / `_cmd_edge` / `_cmd_int_r` / `_cmd_int_w` / `_cmd_w` / `_cmd_tx_over` / `_cmd_abort`，未知命令回 `R-ERR`。

- `TYPE_ECHO` 优先做精确匹配，随时可调用；`_cmd_type_echo` 只回 `YES`，**不读写任何状态字段**，因此可在任意状态安全调用。

**顶层循环**：阻塞读取完整帧 → 打日志 → 处理；任何未预期异常都回 `R-ERR`，保证不崩溃。

- **读取阶段也容错**：`read_frame` 抛出的瞬时异常会被捕获、记日志并继续循环（连续错误过多才退出），串口抖动不会导致进程中断。
- 只有 `transport.eof` 为真（stdio EOF / 串口关闭）时才结束进程；否则空读只是继续等待。
- 任何命令处理异常都回 `R-ERR` 后 `_abort_to_idle()`，不退出。

**读流程**（`_run_read`）：循环发送 R 包 → 等待 `RX-OK`（2s 超时）→ 地址 +N → 超过 EDGE 则发 `TX-OVER` → 回 IDLE。

**写流程**（`_cmd_w`，每包一帧触发）：校验状态/长度/格式 → 回 `RX-OK` → 模拟 I2C 写（约 0.8ms）→ sleep 20ms 写周期 → **写内存** → 回 `R-S`；失败回 `R-ERR` 且地址不增加并兜底落盘。真正的落盘发生在 `TX-OVER` / `ABORT` / 退出。

**会话生命周期**：写 `TX-OVER`（先落盘再应答）/ 读结束 / 任何错误后调用 `_abort_to_idle()`，清空 `edge`（`router` 也随 ROUTER 重设而更新），回到 IDLE。已写入内存的数据保留，并在流程结束/退出时落盘。

### 3.4 Devices 层

`DeviceSpec` 用 `@dataclass(frozen=True)` 描述每片 EEPROM 的静态规格；`DEVICE_TABLE` 固定 8 个设备。

`EepromDevice` 持有：
- `mem`：`bytearray(size)` 内存镜像，**作为主存储**。
- `bin_path`：独立 bin 文件（镜像）。
- `dirty`：内存是否比磁盘新（有未落盘改动）。
- `_load()`：存在且大小匹配则加载；否则补齐/截断；不存在则用 `0xFF` 填充并落盘。
- `read_block(addr, n)`：从内存读。
- `write_block(addr, data)`：**只写内存**并置 `dirty=True`（`flush_each_packet=True` 时立即落盘），支持错误注入返回 `False`。
- `flush()`：若有改动则落盘，成功返回 `True`，重试失败返回 `False`（不抛出）。
- `_flush()`：**原子落盘**（先写 `.tmp`、`fsync`、再 `os.replace`），对瞬时 `PermissionError`/`OSError` **递增退避重试**（最多 6 次）。
- 内部 `RLock` 保证线程安全（当前单线程，防御性设计）。

**落盘策略**（方案 B：内存为主，流程结束统一落盘）：
- 写命令默认只改内存，**不逐包刷盘**。
- `Simulator.flush_all_devices()` 在以下时机统一落盘所有 `dirty` 设备：
  1. 写流程 `TX-OVER`（正常结束）；
  2. `ABORT`（主动中止）；
  3. 写失败（错误注入 NACK）；
  4. 进程退出（`__main__` 的 `finally`）。
- 落盘失败只记 `WARN` 日志，业务仍回 `R-S`（内存是主存储，数据不丢，留待下次落盘）。
- `--flush-each-packet` 可恢复“每写一包立即落盘”的旧行为。

### 3.5 时序与显示

- `I2CTiming`：`byte_time = 9 / rate`；`packet_transfer()` 用 22 字节估算约 0.8ms 并 sleep。可用 `--no-i2c-sleep` 关闭以加速测试。
- `TM1637Sim`：把状态码映射为两位字符 + 路由地址两位，去重打印，输出走 stderr。

---

## 4. 关键设计决策

1. **日志走 stderr**：stdio 模式下 stdout 必须纯净，只放帧字节。
2. **后台线程读 stdin**：解决 stdio 无法设置读超时的问题，是 RX-OK 2s 超时正确性的前提。
3. **N 动态计算**：地址自增按有效字节数而非固定 16，最后一包补 0 只在协议层，不写 EEPROM。
4. **原子落盘**：`.tmp` + `fsync` + `os.replace` 避免写文件过程中掉电损坏；对瞬时占用（杀毒软件等）递增退避重试。
5. **内存为主、流程末落盘（方案 B）**：写命令不逐包刷盘，避免高频重写大文件（128KB）导致 Windows `PermissionError`；在 `TX-OVER`/`ABORT`/退出时统一落盘，兼顾性能与可靠性。
6. **错误统一**：所有异常分支统一 `R-ERR`，集中 `_abort_to_idle`，避免状态泄漏。
7. **错误注入**：`--inject-fail 0x0010,...` 指定“当前地址”，写该地址时返回失败，用于验证 `R-ERR` 且地址不增加。
8. **可测试性**：`SubprocessHost` 用子进程 + 管道驱动，端到端覆盖真实帧流；`--no-i2c-sleep` 加速。

---

## 5. 容量与地址

| chip | 型号 | 字节 | max_addr | 地址字节数 |
|---|---|---|---|---|
| 0x50 | 24C01 | 128 | 0x7F | 1 |
| 0x51 | 24C08 | 1024 | 0x3FF | 1 |
| 0x52 | 24C16 | 2048 | 0x7FF | 1 |
| 0x53 | 24C64 | 8192 | 0x1FFF | 2 |
| 0x54 | 24C128 | 16384 | 0x3FFF | 2 |
| 0x55 | 24C256 | 32768 | 0x7FFF | 2 |
| 0x56 | 24C512 | 65536 | 0xFFFF | 2 |
| 0x57 | 24C1024 | 131072 | 0x1FFFF | 2 |

（地址字节数用于模拟 I2C 传输估算，协议层不直接体现。）

---

## 6. 运行与配置

见 `CMD.md` 与 `README.md`。核心命令：

```powershell
python -m eeprom_sim --port COM3
python -m eeprom_sim --stdio
python -m tests.test_sim
```

主要参数：`--port --baudrate --i2c-rate --write-cycle --rxok-timeout --data-dir --stdio --no-display --no-i2c-sleep --flush-each-packet --inject-fail`。
