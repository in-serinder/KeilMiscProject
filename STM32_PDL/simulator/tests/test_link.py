# -*- coding: utf-8 -*-
"""
端到端链路测试：真的开一个 TCP 服务，用客户端跑一遍完整流程。

    python tests/test_link.py

覆盖：IDENTIFY -> GET_CH_CONFIG x6 -> ACQUIRE -> 收数据 -> ABORT
"""

import os
import socket
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from pdl import proto as P                      # noqa: E402
from pdl.device import Device                   # noqa: E402
from transports.tcp import TcpLink              # noqa: E402
from run_sim import Runner                      # noqa: E402

PORT = 5599


def recv_frame(sock, timeout=2.0):
    sock.settimeout(timeout)
    buf = b""
    while len(buf) < P.REPORT:
        chunk = sock.recv(P.REPORT - len(buf))
        if not chunk:
            raise RuntimeError("连接断开")
        buf += chunk
    return P.parse_frame(buf)


def main():
    dev = Device()
    link = TcpLink("127.0.0.1", PORT, hid_pacing=False, log=lambda *_: None)
    runner = Runner(dev, link, time_scale=1.0)
    link.on_frame = runner.on_frame
    link.start()
    threading.Thread(target=runner.run_forever, daemon=True).start()
    time.sleep(0.3)

    s = socket.create_connection(("127.0.0.1", PORT), timeout=2)
    ok = []

    def check(name, cond, detail=""):
        ok.append(bool(cond))
        print(("  [ OK ] " if cond else "  [FAIL] ") + name + ("" if cond else "  " + str(detail)))

    try:
        # --- IDENTIFY ---
        s.sendall(P.Frame(P.T.IDENTIFY, 0, 1, struct.pack("<I", 0xDEADBEEF)).encode())
        f = recv_frame(s)
        check("IDENTIFY 应答", f and f.type == P.T.ACK)
        magic = f.payload[4:8]
        nonce = struct.unpack("<I", f.payload[32:36])[0]
        check("magic = PDL1", magic == b"PDL1", magic)
        check("nonce 回显", nonce == 0xDEADBEEF, hex(nonce))

        # --- 逐通道配置 ---
        vfs = []
        for ch in range(6):
            s.sendall(P.Frame(P.T.GET_CH_CONFIG, 0, 2 + ch, bytes([ch])).encode())
            f = recv_frame(s)
            cfg = f.payload[4:32]
            vfs.append(struct.unpack("<H", cfg[26:28])[0])
        check("四路满量程读回", vfs[:4] == [142, 281, 759, 1023], vfs[:4])
        check("温度分辨率 12 bit", vfs[4] == 0 and vfs[5] == 0)

        # --- ACQUIRE ---
        acq = struct.pack("<BBBBIIIIIBBBBHHiiII",
                          0x0F, 0x03, 0, 0, 1000, 100, 0, 0,
                          1000, 0, 0, 0x03, 12, 50, 50, 12000, 8000, 0, 0)
        s.sendall(P.Frame(P.T.ACQUIRE, 0, 20, acq).encode())
        f = recv_frame(s)
        check("ACQUIRE 应答", f.type == P.T.ACK and f.payload[1] == P.ST.OK)
        check("进入 RUNNING", f.payload[4] == P.ST_.RUNNING)

        # --- 收 600 ms 数据 ---
        n_data = n_tick = n_temp = 0
        deadline = time.time() + 0.6
        while time.time() < deadline:
            try:
                f = recv_frame(s, 0.4)
            except (socket.timeout, RuntimeError):
                break
            if f.type == P.T.DATA:
                n_data += 1
                if f.flags & P.FF.FIRST:
                    fmt = f.payload[13]
                    if fmt == P.FMT.TEMP16:
                        n_temp += 1
                    elif fmt == P.FMT.RAW16:
                        n_tick += f.payload[10]
        check("收到数据帧", n_data > 50, n_data)
        check("tick 数接近 600", 500 <= n_tick <= 700, n_tick)
        check("温度块按 1 s 节奏（此段 0~1 个）", n_temp <= 1, n_temp)

        # --- ABORT ---
        s.sendall(P.Frame(P.T.ABORT, 0, 30, b"\x00").encode())
        saw_ack = False
        for _ in range(400):
            try:
                f = recv_frame(s, 1.0)
            except (socket.timeout, RuntimeError):
                break
            if f.type == P.T.ACK and f.payload[0] == P.T.ABORT:
                saw_ack = True
                break
        check("ABORT 应答", saw_ack)
        check("ABORT 后 IDLE", dev.state == P.ST_.IDLE, dev.state)

    finally:
        try:
            s.close()
        except OSError:
            pass
        runner.stop()
        link.stop()

    print("-" * 46)
    if all(ok):
        print("链路测试通过（%d 项）" % len(ok))
        return 0
    print("有 %d 项失败" % ok.count(False))
    return 1


if __name__ == "__main__":
    sys.exit(main())
