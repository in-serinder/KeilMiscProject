#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NFG 下位机模拟程序 —— 自动化测试
====================================================================

覆盖任务书第十三节的全部测试用例：

  1.  未设置密码时 >NFG_INFO< 返回 [NFG_0]
  2.  >AUTHSET_123456< -> [SET-OK]，再 >NFG_INFO< -> [NFG_1]
  3.  >AUTH_123456< -> [AUTH_OK]；>AUTH_654321< -> [AUTH_ERR]
  4.  未认证时 >INT_W< -> [RX-ERR]
  5.  认证后写两包到 0x0000~0x001F，读回一致
  6.  最后一包不足 16 字节（写 10 字节），读回补 0x00
  7.  越界写（地址超过 0x7FEF）-> [RX-ERR]
  8.  读流程中上位机不回 [RX-OK]，2s 后 -> [RX-ERR]
  9.  >ABORT< 中断写流程 -> [ABORT-OK]
  10. >DESTROY< 后 >NFG_INFO< -> [NFG_0]，数据清零
  11. 检查 sram.bin 的保存与加载

运行：
    python test_nfg_sim.py
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from nfg_sim import (  # noqa: E402
    NfgCard, NfgProtocol, compute_feature, is_feature_blank,
    PLAIN_START, PLAIN_END, FEATURE_START, PACKET_SIZE,
    format_hex_packet, parse_hex_packet,
)


# ---------------------------------------------------------------------------
# 测试用协议驱动器：直接调用 handle_command，收集应答文本
# ---------------------------------------------------------------------------

_UNSET = object()


class FakeIO:
    """
    收集下位机发出的所有字节，并把 [应答] / >R_[..]< / >TX-OVER< 解析成事件列表。
    """

    def __init__(self):
        self.buffer = b""
        self.frames = []          # 解析得到的应答/数据帧（字符串内容）
        self._pending_rx_ok = _UNSET  # 供 _wait_rx_ok 使用
        self._abort = False

    # ---- 协议 send 回调 ----
    def send(self, data: bytes):
        self.buffer += data
        # 解析所有完整帧
        self._drain()

    def _drain(self):
        text = self.buffer.decode("ascii", errors="replace")
        consumed = 0
        i = 0
        while i < len(text):
            ch = text[i]
            if ch == "[":
                j = text.find("]", i)
                if j == -1:
                    break
                self.frames.append(text[i + 1:j])
                i = j + 1
                consumed = i
            elif ch == ">":
                j = text.find("<", i)
                if j == -1:
                    break
                self.frames.append(text[i:j + 1])  # 含 > < 以区分数据包
                i = j + 1
                consumed = i
            else:
                i += 1
                consumed = i
        self.buffer = text[consumed:].encode("ascii", errors="replace")

    # ---- 协议 wait/poll 回调 ----
    def set_next_rx_ok(self, value):
        """设置下一次 _wait_rx_ok 的返回（True/False/'abort'/None=超时）"""
        self._pending_rx_ok = value

    def wait_rx_ok(self, timeout, poll_abort=None):
        v = self._pending_rx_ok
        self._pending_rx_ok = _UNSET
        # 未显式设置时，默认上位机会正常回 [RX-OK]
        if v is _UNSET:
            return True
        return v

    def poll_abort(self):
        return self._abort

    # ---- 便捷查询 ----
    def last(self):
        return self.frames[-1] if self.frames else None

    def clear(self):
        self.frames.clear()
        self.buffer = b""


def make_protocol(tmp_sram=None, persist=True):
    card = NfgCard(sram_file=tmp_sram, log=_SilentLog(), persist=persist)
    io = FakeIO()
    proto = NfgProtocol(
        card=card,
        send=io.send,
        wait_rx_ok=io.wait_rx_ok,
        poll_abort=io.poll_abort,
        rx_timeout=2.0,
        log=_SilentLog(),
    )
    return card, proto, io


class _SilentLog:
    def info(self, *a, **k): pass
    def tx(self, *a, **k): pass
    def rx(self, *a, **k): pass
    def warn(self, *a, **k): pass
    def error(self, *a, **k): pass


def pkt_str(n, start=0):
    """生成 16 字节包（值为 start..start+15）的 'AA,BB' 字符串列表。"""
    return ",".join(f"{((start + i) & 0xFF):02X}" for i in range(n))


# ---------------------------------------------------------------------------
# 测试用例
# ---------------------------------------------------------------------------

class TestNfgSimulator(unittest.TestCase):

    # ---- 用例 1：未设置密码 ----
    def test_01_nfg_info_no_password(self):
        card, proto, io = make_protocol()
        proto.handle_command("NFG_INFO")
        self.assertEqual(io.last(), "NFG_0")

    # ---- 用例 2：设置密码 ----
    def test_02_authset(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_123456")
        self.assertEqual(io.last(), "SET-OK")
        proto.handle_command("NFG_INFO")
        self.assertEqual(io.last(), "NFG_1")

    def test_02b_authset_bad_format(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_12AB56")   # 非数字
        self.assertEqual(io.last(), "SET_ERR")
        io.clear()
        proto.handle_command("AUTHSET_12345")    # 位数不足
        self.assertEqual(io.last(), "SET_ERR")

    # ---- 用例 3：验证密码 ----
    def test_03_auth(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_123456")
        io.clear()
        proto.handle_command("AUTH_123456")
        self.assertEqual(io.last(), "AUTH_OK")
        io.clear()
        proto.handle_command("AUTH_654321")
        self.assertEqual(io.last(), "AUTH_ERR")

    def test_03b_auth_before_set(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTH_123456")      # 未设置密码
        self.assertEqual(io.last(), "AUTH_ERR")

    # ---- 用例 4：未认证进入写模式 ----
    def test_04_int_w_without_auth(self):
        card, proto, io = make_protocol()
        proto.handle_command("INT_W")
        self.assertEqual(io.last(), "RX-ERR")

    # ---- 用例 5：写两包再读回 ----
    def test_05_write_two_packets_read_back(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_123456")
        proto.handle_command("AUTH_123456")
        io.clear()

        proto.handle_command("INT_W")
        self.assertEqual(io.last(), "RX-OK")

        p1 = pkt_str(16, 0x00)   # 00..0F
        p2 = pkt_str(16, 0x10)   # 10..1F
        proto.handle_command(f"W_{p1}")
        self.assertEqual(io.last(), "RX-OK")
        proto.handle_command(f"W_{p2}")
        self.assertEqual(io.last(), "RX-OK")

        proto.handle_command("TX-OVER")
        self.assertEqual(io.last(), "RX-OK")

        # 校验 SRAM 内容
        self.assertEqual(card.sram[0x0000:0x0010], bytes(range(0x00, 0x10)))
        self.assertEqual(card.sram[0x0010:0x0020], bytes(range(0x10, 0x20)))

        # 读回
        io.clear()
        proto.handle_command("INT_R")
        # init 后第一包已由 run_read_loop 推送，这里手动驱动
        proto.run_read_loop()
        self.assertIn(f">R_{p1}<", io.frames)
        self.assertIn(f">R_{p2}<", io.frames)
        self.assertEqual(io.frames[-1], ">TX-OVER<")

    # ---- 用例 6：不足 16 字节补 0x00 ----
    def test_06_pad_short_packet(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_123456")
        proto.handle_command("AUTH_123456")
        proto.handle_command("INT_W")
        io.clear()

        # 只写 10 字节
        short = pkt_str(10, 0xAA)   # AA..B3
        proto.handle_command(f"W_{short}")
        self.assertEqual(io.last(), "RX-OK")

        expected = bytes([(0xAA + i) & 0xFF for i in range(10)]) + bytes([0x00] * 6)
        self.assertEqual(card.sram[0x0000:0x0010], expected)

        proto.handle_command("TX-OVER")
        io.clear()
        proto.handle_command("INT_R")
        proto.run_read_loop()
        self.assertIn(f">R_{format_hex_packet(expected)}<", io.frames)

    # ---- 用例 7：越界写 ----
    def test_07_out_of_range_write(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_123456")
        proto.handle_command("AUTH_123456")
        proto.handle_command("INT_W")
        io.clear()

        # 直接跳到最后一个合法包地址：0x7FE0（写后 current=0x7FF0，超出明文区）
        card.current_addr = 0x7FE0
        proto.handle_command(f"W_{pkt_str(16)}")
        self.assertEqual(io.last(), "RX-OK")          # 0x7FE0~0x7FEF 合法
        self.assertEqual(card.current_addr, 0x7FF0)   # 递增后已越界

        io.clear()
        proto.handle_command(f"W_{pkt_str(16)}")      # 0x7FF0 起越界
        self.assertEqual(io.last(), "RX-ERR")
        self.assertEqual(card.current_addr, 0x7FF0)   # 越界地址不增加

    def test_07b_write_cannot_touch_feature_area(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_123456")
        feat_before = card.get_feature()
        proto.handle_command("AUTH_123456")
        proto.handle_command("INT_W")
        card.current_addr = 0x7FE0
        proto.handle_command(f"W_{pkt_str(16)}")   # 合法
        io.clear()
        proto.handle_command(f"W_{pkt_str(16)}")   # 越界，应被拒
        self.assertEqual(io.last(), "RX-ERR")
        self.assertEqual(card.get_feature(), feat_before)  # 特征码未被动

    # ---- 用例 8：读流程超时 ----
    def test_08_read_timeout(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_123456")
        proto.handle_command("AUTH_123456")
        io.clear()

        proto.handle_command("INT_R")
        # 模拟上位机一直不回 [RX-OK]
        io.set_next_rx_ok(None)     # 超时
        proto.run_read_loop()
        self.assertEqual(io.last(), "RX-ERR")

    def test_08b_read_abort(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_123456")
        proto.handle_command("AUTH_123456")
        io.clear()

        proto.handle_command("INT_R")
        io.set_next_rx_ok("abort")   # 模拟读到 ABORT
        proto.run_read_loop()
        self.assertEqual(io.last(), "ABORT-OK")

    # ---- 用例 9：ABORT 中断写流程 ----
    def test_09_abort_write(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_123456")
        proto.handle_command("AUTH_123456")
        proto.handle_command("INT_W")
        self.assertTrue(card.write_mode)
        io.clear()

        proto.handle_command("ABORT")
        self.assertEqual(io.last(), "ABORT-OK")
        self.assertFalse(card.write_mode)
        # 中断后写包应失败
        io.clear()
        proto.handle_command(f"W_{pkt_str(16)}")
        self.assertEqual(io.last(), "RX-ERR")

    # ---- 用例 10：DESTROY ----
    def test_10_destroy(self):
        card, proto, io = make_protocol()
        proto.handle_command("AUTHSET_123456")
        proto.handle_command("AUTH_123456")
        proto.handle_command("INT_W")
        proto.handle_command(f"W_{pkt_str(16, 0x11)}")
        proto.handle_command("TX-OVER")

        io.clear()
        proto.handle_command("DESTROY")
        self.assertEqual(io.last(), "DESTROY-OK")

        # 密码状态重置
        io.clear()
        proto.handle_command("NFG_INFO")
        self.assertEqual(io.last(), "NFG_0")

        # 数据清零
        self.assertTrue(all(b == 0xFF for b in card.sram))
        self.assertFalse(card.authenticated)

    # ---- 用例 11：持久化保存/加载 ----
    def test_11_persistence(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "sram.bin")
            # 第一个实例：设置密码、写数据、保存
            card1, proto1, io1 = make_protocol(tmp_sram=path, persist=True)
            proto1.handle_command("AUTHSET_123456")
            proto1.handle_command("AUTH_123456")
            proto1.handle_command("INT_W")
            data = pkt_str(16, 0x33)
            proto1.handle_command(f"W_{data}")
            proto1.handle_command("TX-OVER")
            feat = card1.get_feature()
            self.assertTrue(os.path.exists(path))

            # 第二个实例：从文件加载，内容应一致
            card2, proto2, io2 = make_protocol(tmp_sram=path, persist=True)
            self.assertEqual(card2.get_feature(), feat)
            self.assertEqual(
                card2.sram[0x0000:0x0010],
                bytes([(0x33 + i) & 0xFF for i in range(16)]),
            )
            # 加载后密码仍有效
            proto2.handle_command("AUTH_123456")
            self.assertEqual(io2.last(), "AUTH_OK")

    # ---- 辅助命令 ----
    def test_ping(self):
        card, proto, io = make_protocol()
        proto.handle_command("PING")
        self.assertEqual(io.last(), "PONG")

    # ---- 未知命令 ----
    def test_unknown_command(self):
        card, proto, io = make_protocol()
        proto.handle_command("HELLO_WORLD")
        self.assertEqual(io.last(), "RX-ERR")

    # ---- 特征码算法自洽 ----
    def test_feature_algorithm_consistency(self):
        f1 = compute_feature("123456")
        f2 = compute_feature("123456")
        f3 = compute_feature("654321")
        self.assertEqual(f1, f2)
        self.assertNotEqual(f1, f3)
        self.assertEqual(len(f1), 16)
        self.assertFalse(is_feature_blank(f1))
        self.assertTrue(is_feature_blank(bytes([0xFF] * 16)))
        self.assertTrue(is_feature_blank(bytes([0x00] * 16)))

    # ---- 大小写不敏感 ----
    def test_case_insensitive(self):
        card, proto, io = make_protocol()
        proto.handle_command("ping")
        self.assertEqual(io.last(), "PONG")
        proto.handle_command("nfg_info")
        self.assertEqual(io.last(), "NFG_0")


def run():
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(TestNfgSimulator)
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(run())
