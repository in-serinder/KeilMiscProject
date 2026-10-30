#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NFG 下位机模拟程序 —— 控制台对话演示
====================================================================

以人类可读的方式，打印一段完整的"上位机 <-> 下位机"对话，
可作为联调参考与《控制台日志示例》交付物。

运行：
    python demo_conversation.py
"""

import os
import tempfile

from nfg_sim import (
    NfgCard, NfgProtocol, format_hex_packet,
)


class _ConsoleLog:
    """把下位机日志按固定格式打印到控制台。"""
    def info(self, m): print(f"      · {m}")
    def tx(self, m):   print(f"  下位机 -> {m}")
    def rx(self, m):   print(f"  上位机 -> {m}")
    def warn(self, m): print(f"      ! {m}")
    def error(self, m):print(f"      x {m}")


class DemoIO:
    """
    演示用 I/O：下位机日志由 _ConsoleLog.tx 打印，这里静默累积即可，
    避免同一帧重复显示。
    """
    def __init__(self, log):
        self.log = log
        self.buf = b""

    def send(self, data: bytes):
        self.buf += data   # 实际字节流（演示中无需展示）

    def wait_rx_ok(self, timeout, poll_abort=None):
        return True        # 演示中上位机总是及时回 [RX-OK]

    def poll_abort(self):
        return False


def host(cmd):
    print(f"  上位机 -> >{cmd}<")


def main():
    with tempfile.TemporaryDirectory() as d:
        sram = os.path.join(d, "sram.bin")
        log = _ConsoleLog()
        card = NfgCard(sram_file=sram, log=log, persist=True)
        io = DemoIO(log)
        proto = NfgProtocol(card, io.send, io.wait_rx_ok, io.poll_abort, log=log)

        def do(cmd):
            host(cmd)
            proto.handle_command(cmd)

        print("=" * 68)
        print("NFG 即时销毁存储卡 —— 下位机模拟 对话演示")
        print(f"SRAM 文件: {sram}")
        print("=" * 68)

        print("\n--- [1] 上电后查询密码状态 ---")
        do("NFG_INFO")

        print("\n--- [2] 设置密码 123456 ---")
        do("AUTHSET_123456")
        do("NFG_INFO")

        print("\n--- [3] 错误密码 / 正确密码 ---")
        do("AUTH_654321")
        do("AUTH_123456")

        print("\n--- [4] 写两包数据 ---")
        do("INT_W")
        do("W_00,01,02,03,04,05,06,07,08,09,0A,0B,0C,0D,0E,0F")
        do("W_10,11,12,13,14,15,16,17,18,19,1A,1B,1C,1D,1E,1F")
        do("TX-OVER")

        print("\n--- [5] 读回验证（只展示前 2 包，随后 ABORT 提前结束）---")
        host("INT_R")
        proto.handle_command("INT_R")     # 下位机回 [RX-OK] 并准备推包
        p1 = format_hex_packet(card.sram[0x00:0x10])
        print(f"  下位机 -> >R_{p1}<")
        print("  上位机 -> [RX-OK]")
        p2 = format_hex_packet(card.sram[0x10:0x20])
        print(f"  下位机 -> >R_{p2}<")
        print("  上位机 -> [RX-OK]")
        do("ABORT")

        print("\n--- [6] 销毁 ---")
        do("DESTROY")
        do("NFG_INFO")

        print("\n--- [7] 关闭（保存 SRAM）---")
        card.save_sram()
        size = os.path.getsize(sram) if os.path.exists(sram) else 0
        print(f"  已生成 {sram}（{size} 字节）")

        print("=" * 68)
        print("演示结束")
        print("=" * 68)


if __name__ == "__main__":
    main()
