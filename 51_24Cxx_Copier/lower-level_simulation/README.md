# STC8G1K08A + 多片 24CXX EEPROM 模拟下位机

用 Python 3 模拟单片机 + 多片 24CXX 的串口协议行为，用于和上位机联调。
不需要真实 I2C 硬件：EEPROM 用内存数组模拟，并持久化到独立 bin 文件。

## 特性

- 串口 115200 8N1，帧格式 `>指令内容<`，帧尾带标准换行间隔 `\r\n`。
- 模拟 I2C 250kHz、24CXX 写后等待 20ms、读流控 RX-OK 超时 2s。
- 8 片设备 0x50~0x57（24C01~24C1024），各自独立 bin 文件，掉电不丢失。
- 内存为主存储、磁盘为镜像：写命令只改内存，流程结束（TX-OVER/ABORT/退出）统一落盘，避免高频重写大文件。
- 4 位数码管状态模拟（前两位状态，后两位路由 I2C 地址）。
- 无串口时可用 `--stdio` 用标准输入输出模拟字节流。
- 内置错误注入，便于验证错误分支。

## 安装

```powershell
pip install -r requirements.txt
```

（仅真实串口模式需要 pyserial；`--stdio` 模式无需。）

## 运行

真实串口：

```powershell
python -m eeprom_sim --port COM3 --baudrate 115200
```

stdio（用管道/终端交互）：

```powershell
python -m eeprom_sim --stdio
```

查看设备表：

```powershell
python -m eeprom_sim --list-devices
```

## 参数配置

| 参数                  | 默认          | 说明                                   |
| --------------------- | ------------- | -------------------------------------- |
| `--port / -p`         | COM3          | 串口端口                               |
| `--baudrate / -b`     | 115200        | 波特率                                 |
| `--i2c-rate`          | 250000        | 模拟 I2C 速率 (Hz)                     |
| `--write-cycle`       | 20            | 24CXX 写周期等待 (ms)                  |
| `--rxok-timeout`      | 2.0           | 读流程等待 RX-OK 超时 (s)              |
| `--data-dir / -d`     | ./eeprom_data | bin 文件目录                           |
| `--stdio`             | 关            | 用标准输入输出模拟串口                 |
| `--no-display`        | 关            | 关闭数码管模拟输出                     |
| `--no-i2c-sleep`      | 关            | 不 sleep 模拟 I2C（加速测试）          |
| `--flush-each-packet` | 关            | 每写一包立即落盘（默认流程末统一落盘） |
| `--inject-fail`       | 空            | 写失败地址列表，如 `0x0010,0x0020`     |

日志（`[SIM]` / `[TM1637]`）输出到 **stderr**，stdout 只承载协议帧字节。

## 测试

端到端测试（22 个场景、64 项断言）：

```powershell
python -m tests.test_sim
```

演示脚本：

```powershell
python tools/demo.py
```

覆盖场景：

1. 路由 0x50 (24C01) 写 0x00~0x7F 读回验证。
2. 路由 0x53 (24C64) 写 0x0000~0x001F 读回验证。
3. 路由 0x57 (24C1024) 写 0x00000~0x0001F 读回验证。
4. 最后一包不足 16 字节（EDGE=0x000A）补 0x00 验证。
5. EDGE 越界（0x50 时 0x0080）→ R-ERR。
6. EDGE 越界（0x57 时 0x20000）→ R-ERR。
7. 未发 ROUTER 直接发 EDGE → R-ERR。
8. 未发 INT*W 直接发 W* → R-ERR。
9. 读流程不回 RX-OK，2s 后 → R-ERR。
10. 写包后约 20ms 写周期，bin 已更新。
11. 写失败注入 → R-ERR 且地址不增加。
12. 各 bin 文件生成且大小正确。
13. 非法 ROUTER 地址 → R-ERR。
14. 数据格式错误 / 包长错误 → R-ERR。
15. 重启后 bin 数据持久化。
16. `TYPE_ECHO` 查询工作设备 → `YES`，且不影响状态。
17. 未知/畸形帧（如误输入 `>TYPE-ECHO<`）不崩溃，回 `R-ERR` 后可继续工作。
18. 帧外垃圾字节/换行被忽略，合法帧仍可正常解析。
19. 帧尾 `\r\n` 间隔：发送含 `\r\n` 的帧正常应答，连续多帧、单独 `\n` 间隔均可解析。
20. 下位机发出帧尾确实带 `\r\n`（字节级校验）。
21. `ABORT` 时兜底落盘（保留内存已写数据）。
22. `--flush-each-packet` 时 W 包阶段即落盘。

## 文档

- `CMD.md` — 指令文档（协议、命令、错误、流程）。
- `STRC.MD` — 架构文档（模块、状态机、设计决策）。

## 快速流程示例

写：

```
>ROUTER_0x50<   → >R-S<
>EDGE_0x007F<   → >R-S<
>INT_W<         → >R-S<
>W_[00,01,...,0F]< → >RX-OK<  → >R-S<
...
>TX-OVER<       → (统一落盘) → >R-S<
```

读：

```
>ROUTER_0x50<   → >R-S<
>EDGE_0x007F<   → >R-S<
>INT_R<         → >R-S<
                    >R_[...16字节...]<
>RX-OK<
                    >R_[...16字节...]<
>RX-OK<
...
                    >TX-OVER<
```

查询设备：

```
>TYPE_ECHO<     → >YES<
```

注意（`⏎` 表示帧尾 `\r\n`）：

```
>ROUTER_0x50<⏎>EDGE_0x007F<⏎>INT_W<⏎>W_[..16字节..]<⏎ ...
```

TX-OVER 后下位机回到 IDLE，再次读写需重新发 ROUTER + EDGE。
