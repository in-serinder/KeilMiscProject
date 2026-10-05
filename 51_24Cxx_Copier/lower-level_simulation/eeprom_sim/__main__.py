"""模拟下位机入口。

启动示例：
    python -m eeprom_sim --port COM3
    python -m eeprom_sim --stdio
"""
from __future__ import annotations

import argparse
import os
import sys

from .state_machine import Simulator
from .transport import make_transport


def parse_fail_addrs(text: str) -> set[int]:
    """解析错误注入地址列表，如 '0x0010,0x0020' 或 '16,32'。"""
    result: set[int] = set()
    if not text:
        return result
    for tok in text.split(","):
        tok = tok.strip()
        if not tok:
            continue
        base = 16 if tok.lower().startswith("0x") else 16
        result.add(int(tok, base))
    return result


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="eeprom_sim",
        description="STC8G1K08A + 多片 24CXX EEPROM 串口协议模拟下位机",
    )
    p.add_argument("--port", "-p", default="COM2",
                   help="串口端口，如 COM2 或 /dev/ttyUSB0")
    p.add_argument("--baudrate", "-b", type=int, default=115200,
                   help="波特率，默认 115200")
    p.add_argument("--i2c-rate", type=int, default=250000,
                   help="模拟 I2C 速率 Hz，默认 250000")
    p.add_argument("--write-cycle", type=float, default=20.0,
                   help="24CXX 写周期等待 ms，默认 20")
    p.add_argument("--rxok-timeout", type=float, default=2.0,
                   help="读流程等待 RX-OK 超时 秒，默认 2.0")
    p.add_argument("--data-dir", "-d",
                   default=os.path.join(os.getcwd(), "eeprom_data"),
                   help="bin 文件存放目录，默认 ./eeprom_data")
    p.add_argument("--stdio", action="store_true",
                   help="用标准输入输出模拟串口字节流")
    p.add_argument("--no-display", action="store_true",
                   help="关闭 TM1637 数码管模拟输出")
    p.add_argument("--no-i2c-sleep", action="store_true",
                   help="不真实 sleep 模拟 I2C 传输耗时（加速测试）")
    p.add_argument("--flush-each-packet", action="store_true",
                   help="每写一包立即落盘（默认在流程结束/退出时统一落盘）")
    p.add_argument("--inject-fail", default="",
                   help="错误注入：写失败地址列表，如 0x0010,0x0020")
    p.add_argument("--list-devices", action="store_true",
                   help="打印设备表后退出")
    return p


def list_devices() -> None:
    from .devices import DEVICE_TABLE
    print(f"{'chip':>6} {'model':>8} {'size':>8} {'range':>16} "
          f"{'w/r addr':>12}  bin")
    for addr, s in DEVICE_TABLE.items():
        rng = f"0x00~0x{s.max_addr:X}"
        wr = f"0x{s.write_addr:02X}/0x{s.read_addr:02X}"
        print(f"0x{addr:02X} {s.name:>8} {s.size:>8} {rng:>16} "
              f"{wr:>12}  {s.bin_name}")


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    if args.list_devices:
        list_devices()
        return 0

    os.makedirs(args.data_dir, exist_ok=True)

    inject = parse_fail_addrs(args.inject_fail)
    if inject:
        print(f"[SIM] inject write-fail at addrs: "
              f"{sorted(hex(a) for a in inject)}", flush=True)

    transport = make_transport(args)
    sim = Simulator(
        transport=transport,
        data_dir=args.data_dir,
        i2c_rate=args.i2c_rate,
        write_cycle=args.write_cycle / 1000.0,
        rxok_timeout=args.rxok_timeout,
        display=not args.no_display,
        i2c_sleep=not args.no_i2c_sleep,
        inject_fail_addrs=inject,
        flush_each_packet=args.flush_each_packet,
    )
    print(f"[SIM] data dir: {os.path.abspath(args.data_dir)}",
          file=sys.stderr, flush=True)
    if args.stdio:
        print("[SIM] stdio mode (stdin/stdout as serial bytes)",
              file=sys.stderr, flush=True)
    else:
        print(f"[SIM] serial {args.port} @ {args.baudrate} 8N1",
              file=sys.stderr, flush=True)

    try:
        sim.run()
    except KeyboardInterrupt:
        print("\n[SIM] interrupted.", file=sys.stderr, flush=True)
    finally:
        # 退出时兜底落盘：内存为主存储，尽量不丢数据
        sim.flush_all_devices()
        transport.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
