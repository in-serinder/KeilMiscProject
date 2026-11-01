#!/usr/bin/env python3
"""破坏 EEPROM bin 里的一个字节，用来验证上位机的数据哈希校验。

注意：模拟器只在**启动时**把 bin 读进内存，运行期间改文件不会生效
（而且下一次写包还会把内存镜像覆盖回 bin）。所以顺序必须是：

    1. 先停止模拟器（Ctrl+C）
    2. python corrupt_eeprom.py data/eeprom_0x50_24C64.bin --offset 3
    3. 重新启动模拟器
    4. 上位机点「Read & Verify」→ 状态栏应显示 HASH MISMATCH 并弹窗

用法：
    python corrupt_eeprom.py <bin 路径> [--offset 0] [--value 0x00] [--xor 0xFF] [--show]

参数（三选一，默认 --xor FF）：
    --value V   直接把该字节改成 V
    --xor M     该字节与 M 异或（等价于翻转若干位）
    --show      只打印当前前 32 字节，不修改
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="翻掉 EEPROM bin 里的一个字节（用于测试哈希校验）")
    parser.add_argument("path", help="例如 data/eeprom_0x50_24C64.bin")
    parser.add_argument("--offset", type=lambda s: int(s, 0), default=0, help="字节偏移，默认 0")
    parser.add_argument("--value", type=lambda s: int(s, 0), default=None, help="改成这个值")
    parser.add_argument("--xor", type=lambda s: int(s, 0), default=None, help="与这个掩码异或，默认 0xFF")
    parser.add_argument("--show", action="store_true", help="只显示前 32 字节")
    args = parser.parse_args()

    path = Path(args.path)
    if not path.exists():
        print(f"找不到文件：{path}", file=sys.stderr)
        return 1

    raw = bytearray(path.read_bytes())

    def dump() -> None:
        head = raw[:32]
        print("前 32 字节：" + " ".join(f"{b:02X}" for b in head))

    if args.show:
        dump()
        return 0

    if not 0 <= args.offset < len(raw):
        print(f"偏移越界：0 ~ {len(raw) - 1}", file=sys.stderr)
        return 1

    before = raw[args.offset]
    if args.value is not None:
        raw[args.offset] = args.value & 0xFF
    else:
        mask = 0xFF if args.xor is None else (args.xor & 0xFF)
        raw[args.offset] ^= mask

    path.write_bytes(bytes(raw))

    print(f"文件   ：{path}")
    print(f"偏移   ：0x{args.offset:X}")
    print(f"字节   ：0x{before:02X} -> 0x{raw[args.offset]:02X}")
    print("提示   ：模拟器运行中改文件不会生效，请先停止再改，然后重新启动模拟器。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
