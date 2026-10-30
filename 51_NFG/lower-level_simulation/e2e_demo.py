#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NFG 下位机模拟程序 —— 端到端 (E2E) 联调脚本
====================================================================

通过子进程 + 标准输入输出模拟串口，与 nfg_sim.py --stdio 进行真实字节流对话，
验证帧解析、读流程推包、超时、ABORT 等完整链路。

运行：
    python e2e_demo.py
"""

import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "nfg_sim.py")


def _extract_frame(buf):
    """
    从 buf 中提取最前面的一个完整帧。
    返回 (frame, remaining)；无完整帧返回 (None, buf)。
    帧类型：'[...]'（应答）或 '>...<'（数据/命令）。
    """
    # 找到最早的起始定界符
    i_sq = buf.find("[")
    i_gt = buf.find(">")
    starts = [i for i in (i_sq, i_gt) if i != -1]
    if not starts:
        # 全是杂散字符，丢弃（保留最后 1 个以防跨调用）
        return None, buf[-1:] if len(buf) > 1 else buf
    start = min(starts)
    if buf[start] == "[":
        end = buf.find("]", start)
        if end == -1:
            return None, buf
        return buf[start:end + 1], buf[end + 1:]
    else:
        end = buf.find("<", start)
        if end == -1:
            return None, buf
        return buf[start:end + 1], buf[end + 1:]


class StdioPeer:
    """把 nfg_sim.py --stdio 子进程当作"串口对端"。"""

    def __init__(self, sram_file, rx_timeout=2.0):
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "latin-1"
        cmd = [
            sys.executable, "-u", SIM,
            "--stdio",
            "--sram-file", sram_file,
            "--rx-timeout", str(rx_timeout),
            "--quiet",
        ]
        self.p = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, env=env,
        )
        self._buf = ""

    def send(self, text):
        self.p.stdin.write(text.encode("ascii"))
        self.p.stdin.flush()

    def read_frame(self, timeout=3.0):
        """读取下一个完整帧（'[...]' 或 '>...<'），保持跨调用缓冲。"""
        deadline = time.time() + timeout
        while True:
            # 先在已缓冲内容中尝试提取一个完整帧
            frame, rest = _extract_frame(self._buf)
            if frame is not None:
                self._buf = rest
                return frame
            # 缓冲不足以构成完整帧 -> 继续读
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError(f"读帧超时，已收到: {self._buf!r}")
            ch = self.p.stdout.read(1)
            if ch:
                self._buf += ch.decode("latin-1")

    def cmd(self, text, timeout=3.0):
        """发送命令并读取一条应答。"""
        self.send(text)
        return self.read_frame(timeout)

    def close(self):
        try:
            self.p.stdin.close()
        except Exception:
            pass
        try:
            self.p.terminate()
            self.p.wait(timeout=2)
        except Exception:
            self.p.kill()


def pkt_bytes(values):
    return ",".join(f"{v:02X}" for v in values)


def main():
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        sram = os.path.join(d, "sram.bin")
        peer = StdioPeer(sram)
        results = []

        def check(name, got, want):
            ok = got == want
            results.append(ok)
            flag = "PASS" if ok else "FAIL"
            print(f"[{flag}] {name}: got={got!r} want={want!r}")

        try:
            # 1. PING
            check("PING", peer.cmd(">PING<"), "[PONG]")

            # 2. 未设置密码
            check("NFG_INFO(未设置)", peer.cmd(">NFG_INFO<"), "[NFG_0]")

            # 3. 设置密码
            check("AUTHSET", peer.cmd(">AUTHSET_123456<"), "[SET-OK]")
            check("NFG_INFO(已设置)", peer.cmd(">NFG_INFO<"), "[NFG_1]")

            # 4. 错误密码
            check("AUTH 错误", peer.cmd(">AUTH_654321<"), "[AUTH_ERR]")

            # 5. 未认证写
            check("INT_W 未认证", peer.cmd(">INT_W<"), "[RX-ERR]")

            # 6. 正确密码
            check("AUTH 正确", peer.cmd(">AUTH_123456<"), "[AUTH_OK]")

            # 7. 写两包
            check("INT_W", peer.cmd(">INT_W<"), "[RX-OK]")
            p1 = pkt_bytes(range(0x00, 0x10))
            p2 = pkt_bytes(range(0x10, 0x20))
            check("W_ 包1", peer.cmd(f">W_{p1}<"), "[RX-OK]")
            check("W_ 包2", peer.cmd(f">W_{p2}<"), "[RX-OK]")
            check("TX-OVER", peer.cmd(">TX-OVER<"), "[RX-OK]")

            # 8. 读回
            check("INT_R", peer.cmd(">INT_R<"), "[RX-OK]")
            r1 = peer.read_frame()          # >R_[...]<
            check("读回 包1", r1, f">R_{p1}<")
            peer.send("[RX-OK]")            # 上位机应答
            r2 = peer.read_frame()
            check("读回 包2", r2, f">R_{p2}<")
            peer.send("[RX-OK]")
            # 继续读到 TX-OVER（中间会有很多 0xFF 包，逐包回 RX-OK）
            got_over = False
            for _ in range(3000):
                fr = peer.read_frame(timeout=3.0)
                if fr == ">TX-OVER<":
                    got_over = True
                    break
                peer.send("[RX-OK]")
            results.append(got_over)
            print(f"[{'PASS' if got_over else 'FAIL'}] 读流程以 TX-OVER 结束")

            # 9. 读流程超时：发起读后不回 RX-OK
            peer.cmd(">INT_R<")             # 会先收到第一包
            peer.read_frame()               # 读掉第一包
            # 故意不回，等待超时（2s）
            t0 = time.time()
            fr = None
            for _ in range(5):
                try:
                    fr = peer.read_frame(timeout=3.0)
                except TimeoutError:
                    break
                if fr == "[RX-ERR]":
                    break
            took = time.time() - t0
            ok = fr == "[RX-ERR]"
            results.append(ok)
            print(f"[{'PASS' if ok else 'FAIL'}] 读超时 -> [RX-ERR] (耗时 {took:.2f}s)")

            # 10. ABORT
            check("ABORT", peer.cmd(">ABORT<"), "[ABORT-OK]")

            # 11. 销毁
            check("DESTROY", peer.cmd(">DESTROY<"), "[DESTROY-OK]")
            check("NFG_INFO(销毁后)", peer.cmd(">NFG_INFO<"), "[NFG_0]")

        finally:
            peer.close()

        print()
        print(f"===== E2E 结果: {sum(results)}/{len(results)} 通过 =====")
        return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
