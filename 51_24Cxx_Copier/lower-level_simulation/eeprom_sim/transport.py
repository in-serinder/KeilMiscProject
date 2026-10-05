"""传输层：真实串口 或 stdio 字节流。

统一提供：
  read_frame(timeout) -> str | None   读取一个 >...< 完整帧内容
  write_frame(body)                    发送一个帧
  close()
"""
from __future__ import annotations

import queue
import sys
import threading
import time

from . import protocol


class Transport:
    #: 输入流是否已结束（stdio EOF / 串口关闭）
    eof: bool = False

    def read_frame(self, timeout: float | None = None) -> str | None:
        raise NotImplementedError

    def write_frame(self, body: str) -> None:
        raise NotImplementedError

    def close(self) -> None:
        pass


class SerialTransport(Transport):
    """真实串口，115200 8N1，无流控。"""

    def __init__(self, port: str, baudrate: int = 115200) -> None:
        import serial  # 延迟导入，stdio 模式无需 pyserial
        self.eof = False
        self.ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=2.0,
        )

    def read_frame(self, timeout: float | None = None) -> str | None:
        deadline = None if timeout is None else time.monotonic() + timeout
        buf = bytearray()
        started = False
        while True:
            # 尚未开始帧且已超时 -> 直接返回
            if deadline is not None and time.monotonic() > deadline:
                return None
            chunk = self.ser.read(1)
            if not chunk:
                # 串口本次无数据（read 已受串口自身 timeout 限制）：
                # 继续循环，由顶部 deadline 判断是否超时
                continue
            b = chunk[0]
            if not started:
                if b == ord(protocol.FRAME_START):
                    started = True
                    buf.clear()
                continue
            # 帧内
            if b in (0x0A, 0x0D):  # \n \r
                # 帧内不允许换行 -> 视为错误，丢弃重启
                started = False
                buf.clear()
                continue
            if b == ord(protocol.FRAME_END):
                return buf.decode("ascii", errors="ignore")
            buf.append(b)

    def write_frame(self, body: str) -> None:
        self.ser.write(protocol.build(body))
        self.ser.flush()

    def close(self) -> None:
        try:
            self.ser.close()
        except Exception:
            pass


class StdioTransport(Transport):
    """用标准输入输出模拟串口字节流。

    后台线程持续从 stdin 读字节放入队列，使 read_frame 的
    超时语义得以实现（stdin 阻塞读无法直接配合 timeout）。
    """

    _EOF = object()

    def __init__(self) -> None:
        self._q: "queue.Queue[object]" = queue.Queue()
        self.eof = False
        # 日志走 stderr，避免污染 stdout 的帧流
        self._log = sys.stderr
        self._thread = threading.Thread(
            target=self._reader, name="stdio-reader", daemon=True)
        self._thread.start()

    def _reader(self) -> None:
        while True:
            try:
                ch = sys.stdin.buffer.read(1)
            except Exception:
                self.eof = True
                self._q.put(self._EOF)
                return
            if ch == b"":
                self.eof = True
                self._q.put(self._EOF)
                return
            self._q.put(ch)

    def _next_byte(self, deadline: float | None) -> bytes | None:
        """取下一个字节。返回 None 表示超时/EOF 结束。"""
        while True:
            if deadline is None:
                item = self._q.get()
            else:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                try:
                    item = self._q.get(timeout=remaining)
                except queue.Empty:
                    return None
            if item is self._EOF:
                return None
            return item  # type: ignore[return-value]

    def read_frame(self, timeout: float | None = None) -> str | None:
        deadline = None if timeout is None else time.monotonic() + timeout
        started = False
        frame = bytearray()
        while True:
            b = self._next_byte(deadline)
            if b is None:
                return None
            ch = chr(b[0])
            if not started:
                if ch == protocol.FRAME_START:
                    started = True
                    frame.clear()
                continue
            if ch in ("\n", "\r"):
                started = False
                frame.clear()
                continue
            if ch == protocol.FRAME_END:
                return frame.decode("ascii", errors="ignore")
            frame.extend(b)

    def write_frame(self, body: str) -> None:
        data = protocol.build(body)
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()


def make_transport(args) -> Transport:
    if args.stdio:
        return StdioTransport()
    return SerialTransport(args.port, args.baudrate)
