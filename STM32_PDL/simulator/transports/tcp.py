# -*- coding: utf-8 -*-
"""
TCP 传输 —— 让上位机在没有 USB 硬件时也能连。

注意：TCP 是字节流，一次 recv 可能不足 64 B，也可能一次来 128 B，
所以必须用 pdl.proto.Decoder 做「凑满 64 B 再交上去」。
"""

import socket
import threading
import time
from queue import Queue, Empty

from pdl import proto as P


class TcpLink:
    def __init__(self, host="127.0.0.1", port=5555, hid_pacing=True, log=print):
        self.host, self.port = host, port
        self.hid_pacing = hid_pacing
        self.log = log
        self._srv = None
        self._cli = None
        self._tx = Queue(maxsize=8192)
        self._running = False
        self._threads = []
        self.on_frame = None          # callable(Frame)
        self.rx_frames = 0
        self.tx_frames = 0
        self.connected = False

    # ---------------------------------------------------------------- 生命周期
    def start(self):
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind((self.host, self.port))
        self._srv.listen(1)
        self._srv.settimeout(0.5)
        self._running = True
        t = threading.Thread(target=self._accept_loop, name="pdl-accept", daemon=True)
        t.start()
        self._threads.append(t)
        self.log("监听 %s:%d" % (self.host, self.port))

    def stop(self):
        self._running = False
        for s in (self._cli, self._srv):
            try:
                if s:
                    s.close()
            except OSError:
                pass
        self._cli = None

    def _accept_loop(self):
        while self._running:
            try:
                cli, addr = self._srv.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            self._cli = cli
            self._cli.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.connected = True
            self.log("上位机已连接 %s" % (addr,))
            threading.Thread(target=self._rx_loop, args=(cli,), daemon=True).start()
            threading.Thread(target=self._tx_loop, args=(cli,), daemon=True).start()

    # ---------------------------------------------------------------- 收
    def _rx_loop(self, cli):
        dec = P.Decoder()
        while self._running:
            try:
                chunk = cli.recv(4096)
            except OSError:
                break
            if not chunk:
                break
            for f in dec.feed(chunk):
                self.rx_frames += 1
                if self.on_frame:
                    self.on_frame(f)
        self.connected = False
        self.log("上位机断开（CRC 错 %d 帧）" % dec.bad)

    # ---------------------------------------------------------------- 发
    def send(self, raw: bytes):
        try:
            self._tx.put_nowait(raw)
        except Exception:
            pass          # 队列满 = 链路拥塞，模拟真实的阻塞

    def _tx_loop(self, cli):
        next_slot = 0.0
        while self._running:
            try:
                raw = self._tx.get(timeout=0.1)
            except Empty:
                continue
            if self.hid_pacing:
                # 模拟 USB FS HID：1 ms 一个 64 B 事务
                now = time.perf_counter()
                if next_slot > now:
                    time.sleep(min(next_slot - now, 0.002))
                next_slot = max(now, next_slot) + 0.001
            try:
                cli.sendall(raw)
                self.tx_frames += 1
            except OSError:
                break
