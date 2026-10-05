"""简易上位机客户端（用于联调/测试）。

可驱动真实串口或 stdio 子进程中的模拟下位机。
"""
from __future__ import annotations

import subprocess
import sys
import time


class HostClient:
    """与下位机帧协议对接的上位机。"""

    def __init__(self, reader, writer, timeout: float = 2.0) -> None:
        self._reader = reader      # 可调用: read 1 byte -> bytes
        self._writer = writer      # 可调用: write bytes
        self.timeout = timeout

    # ---- 底层 ----
    def _send_raw(self, body: str) -> None:
        # 帧尾附加标准换行间隔 \r\n
        self._writer(f">{body}<\r\n".encode("ascii"))

    def _read_frame(self, timeout: float | None = None) -> str:
        timeout = self.timeout if timeout is None else timeout
        deadline = time.monotonic() + timeout
        started = False
        buf = bytearray()
        while time.monotonic() < deadline:
            ch = self._reader()
            if ch is None or ch == b"":
                continue
            c = ch.decode("ascii", errors="ignore")
            if not started:
                if c == ">":
                    started = True
                continue
            if c == "<":
                return buf.decode("ascii")
            buf.append(ch[0])
        raise TimeoutError("no frame within %.2fs" % timeout)

    # ---- 命令 ----
    def cmd(self, body: str, timeout: float | None = None) -> str:
        self._send_raw(body)
        return self._read_frame(timeout)

    def router(self, chip: int) -> str:
        return self.cmd(f"ROUTER_0x{chip:02X}")

    def edge(self, addr: int) -> str:
        return self.cmd(f"EDGE_0x{addr:04X}")

    def type_echo(self) -> str:
        """查询当前设备是否为指定工作设备，下位机回 YES。"""
        return self.cmd("TYPE_ECHO")

    def int_r(self) -> str:
        return self.cmd("INT_R")

    def int_w(self) -> str:
        return self.cmd("INT_W")

    def send_w(self, data: list[int]) -> tuple[str, str]:
        """发送一包 W，返回 (RX-OK 应答, R-S/R-ERR 应答)。"""
        body = "W_[" + ",".join(f"{b:02X}" for b in data) + "]"
        rxok = self.cmd(body)
        rs = self._read_frame()
        return rxok, rs

    def rx_ok(self) -> None:
        self._send_raw("RX-OK")

    def tx_over(self) -> str:
        return self.cmd("TX-OVER")

    def read_all(self) -> list[int]:
        """执行完整读流程，返回收到的所有有效字节（按 N 拼接）。"""
        result: list[int] = []
        while True:
            frame = self._read_frame()
            if frame == "TX-OVER":
                break
            if not frame.startswith("R_["):
                raise RuntimeError("unexpected frame: %r" % frame)
            inner = frame[frame.find("[") + 1:frame.rfind("]")]
            pkt = [int(x, 16) for x in inner.split(",")] if inner else []
            result.extend(pkt)
            self.rx_ok()
        return result

    def write_stream(self, data: list[int]) -> None:
        """执行完整写流程，按 16 字节分包发送。"""
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            if len(chunk) < 16:
                chunk = chunk + [0x00] * (16 - len(chunk))
            self.send_w(chunk)
        self.tx_over()


class SubprocessHost(HostClient):
    """把模拟下位机作为子进程启动，用管道做 stdio 字节流。"""

    def __init__(self, args: list[str], timeout: float = 2.0,
                 cwd: str | None = None) -> None:
        cmd = [sys.executable, "-m", "eeprom_sim", "--stdio",
               "--no-display", *args]
        self.proc = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, cwd=cwd)

        def reader():
            return self.proc.stdout.read(1)

        def writer(b: bytes):
            self.proc.stdin.write(b)
            self.proc.stdin.flush()

        super().__init__(reader, writer, timeout)

    def close(self) -> None:
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=3)
        except Exception:
            self.proc.kill()
