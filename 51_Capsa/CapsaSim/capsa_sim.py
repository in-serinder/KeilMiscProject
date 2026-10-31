#!/usr/bin/env python3
"""
Capsa 存储卡 · 下位机模拟器（入口）

    python capsa_sim.py                 # 默认 COM1, 115200 8N1
    python capsa_sim.py --port COM3
    python capsa_sim.py --stdio         # 用标准输入输出模拟串口（脚本联调）
    python capsa_sim.py --data-dir ./data --rx-timeout 2

帧格式：上位机 >命令<，下位机 [应答]；读流程里下位机发 >R_...< 并等待主机 [RX-OK]。
"""

from __future__ import annotations

import argparse
import logging
import queue
import sys
import threading
import time
from pathlib import Path
from typing import Callable

from capsa_devices import ARRAY_ADDRESSES, MODEL_BYTES
from capsa_machine import CapsaMachine, SimConfig

LOG = logging.getLogger("capsa.sim")


# --------------------------------------------------------------------------- #
# 帧缓冲：把字节流切成 >...< 或 [...] 单元
# --------------------------------------------------------------------------- #

class FrameBuffer:
    """字节流 -> 帧。子类负责实际的读取方式。"""

    def __init__(self, write_text: Callable[[str], None], name: str) -> None:
        self.name = name
        self._write_text = write_text
        self._buffer = bytearray()
        self._eof = False

    # -- 对外 -------------------------------------------------------------- #

    @property
    def eof(self) -> bool:
        return self._eof

    def write(self, text: str) -> None:
        self._write_text(text)

    def feed(self, chunk: bytes) -> None:
        self._buffer.extend(chunk)

    def read_frame(self, timeout: float | None) -> str | None:  # pragma: no cover - 抽象
        raise NotImplementedError

    def close(self) -> None:
        self._eof = True

    # -- 帧解析 ------------------------------------------------------------ #

    def _extract(self) -> str | None:
        buf = self._buffer

        while True:
            angle = buf.find(b">")
            bracket = buf.find(b"[")

            starts = [pos for pos in (angle, bracket) if pos >= 0]
            if not starts:
                # 丢弃明显不属于任何帧的噪声，避免缓冲无限增长
                if len(buf) > 8192:
                    del buf[: len(buf) - 1024]
                return None

            start = min(starts)
            closer = b"<" if buf[start:start + 1] == b">" else b"]"
            end = buf.find(closer, start + 1)

            if end < 0:
                if start > 0:
                    del buf[:start]
                return None

            payload = bytes(buf[start + 1:end])
            del buf[: end + 1]

            try:
                return payload.decode("utf-8")
            except UnicodeDecodeError:
                return payload.decode("utf-8", "replace")


class SerialTransport(FrameBuffer):
    """串口：后台线程按字节读取（pyserial 自带超时，可安全退出）。"""

    def __init__(self, ser, name: str) -> None:  # noqa: ANN001 - pyserial 类型
        super().__init__(lambda text: self._send(ser, text), name)
        self._ser = ser
        self._queue: queue.Queue[bytes] = queue.Queue()
        self._thread = threading.Thread(target=self._pump, name=f"{name}-reader", daemon=True)
        self._thread.start()

    @staticmethod
    def _send(ser, text: str) -> None:  # noqa: ANN001
        ser.write((text + "\r\n").encode("utf-8"))
        ser.flush()

    def _pump(self) -> None:
        while not self._eof:
            try:
                chunk = self._ser.read(1)
            except Exception as exc:  # pragma: no cover - 串口掉线
                LOG.error("serial reader stopped: %s", exc)
                self._eof = True
                return
            if chunk:
                self._queue.put(chunk)

    def read_frame(self, timeout: float | None) -> str | None:
        deadline = None if timeout is None else time.monotonic() + timeout

        while True:
            frame = self._extract()
            if frame is not None:
                return frame

            wait = 0.2 if deadline is None else deadline - time.monotonic()
            if deadline is not None and wait <= 0:
                return None

            try:
                self.feed(self._queue.get(timeout=wait))
            except queue.Empty:
                if deadline is not None:
                    return None

    def close(self) -> None:
        super().close()
        try:
            self._ser.close()
        except Exception:
            pass


class StdioTransport(FrameBuffer):
    """标准输入输出：同步阻塞读取，管道关闭时正常结束（无需后台线程）。"""

    def __init__(self) -> None:
        super().__init__(self._send, "stdio")
        self._stdin = sys.stdin.buffer

    @staticmethod
    def _send(text: str) -> None:
        sys.stdout.write(text + "\n")
        sys.stdout.flush()

    def read_frame(self, timeout: float | None) -> str | None:
        # 管道没有跨平台的超时读；这里阻塞等待，EOF 时返回 None。
        while True:
            frame = self._extract()
            if frame is not None:
                return frame

            if self._eof:
                return None

            chunk = self._stdin.read(1)
            if not chunk:
                self._eof = True
                return None

            self.feed(chunk)


class LoggingIO:
    """给状态机用的 IO：自动打印 TX/RX 日志。"""

    def __init__(self, transport: FrameBuffer) -> None:
        self.transport = transport

    def write(self, text: str) -> None:
        LOG.info("TX %s", text)
        self.transport.write(text)

    def read_frame(self, timeout: float | None) -> str | None:
        frame = self.transport.read_frame(timeout)
        if frame is None:
            LOG.warning("RX timeout (%.1fs)", timeout or 0)
            return None

        LOG.info("RX %s", frame)
        return frame


# --------------------------------------------------------------------------- #
# 打开传输层
# --------------------------------------------------------------------------- #

def open_serial(port: str, baud: int) -> SerialTransport:
    import serial  # 延迟导入，--stdio 模式无需 pyserial

    LOG.info("opening serial %s @ %d 8N1", port, baud)
    ser = serial.Serial(
        port=port,
        baudrate=baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.05,
    )
    return SerialTransport(ser, f"serial:{port}")


# --------------------------------------------------------------------------- #
# 主程序
# --------------------------------------------------------------------------- #

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="capsa_sim",
        description="Capsa 8×EEPROM 存储卡 / 烧录器 下位机模拟器",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--port", default="COM1", help="串口端口")
    parser.add_argument("--baud", type=int, default=115200, help="波特率")
    parser.add_argument("--data-dir", default="./data", help="bin 文件目录")
    parser.add_argument("--rx-timeout", type=float, default=2.0, help="读流程等待 [RX-OK] 的超时（秒）")
    parser.add_argument("--stdio", action="store_true", help="用标准输入输出模拟串口字节流")
    parser.add_argument("--no-delay", action="store_true", help="关闭 I2C 时序 sleep（仅供联调/测试）")
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"])
    return parser


def print_banner(data_dir: Path, args: argparse.Namespace) -> None:
    LOG.info("=" * 68)
    LOG.info("Capsa lower-machine simulator")
    LOG.info("  transport : %s", "stdio" if args.stdio else f"{args.port} @ {args.baud} 8N1")
    LOG.info("  data dir  : %s", data_dir.resolve())
    LOG.info("  rx timeout: %.1fs%s", args.rx_timeout, "" if not args.stdio else " (stdio 模式为阻塞等待)")
    LOG.info("  I2C1      : 8 × 24Cxx @ 0x50-0x57 (default 24C64, 8KB each)")
    LOG.info("  I2C2      : 24C02 meta @ 0x50 · PCF8574 WP @ 0x58")
    LOG.info("  models    : %s", ", ".join(f"24C{m}={b // 1024}KB" for m, b in MODEL_BYTES.items()))
    LOG.info("=" * 68)


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s [%(levelname)-5s] %(message)s",
        datefmt="%H:%M:%S",
        stream=sys.stderr if args.stdio else sys.stdout,
    )

    data_dir = Path(args.data_dir)
    data_dir.mkdir(parents=True, exist_ok=True)

    try:
        transport: FrameBuffer = StdioTransport() if args.stdio else open_serial(args.port, args.baud)
    except Exception as exc:
        LOG.error("cannot open transport: %s", exc)
        return 2

    print_banner(data_dir, args)

    io = LoggingIO(transport)
    machine = CapsaMachine(
        SimConfig(data_dir=data_dir, rx_timeout=args.rx_timeout, delays=not args.no_delay),
        io,
    )

    LOG.info("meta: %s", machine.meta.describe())
    LOG.info("array files: %s", ", ".join(machine.chips[a].filename for a in ARRAY_ADDRESSES))
    LOG.info("listening… (Ctrl+C to quit)")

    try:
        while True:
            frame = transport.read_frame(0.5)
            if frame is None:
                if transport.eof:
                    LOG.info("transport closed")
                    break
                continue

            LOG.info("RX %s", frame)
            machine.handle(frame)
            LOG.info("state: %s", machine.summary())

    except KeyboardInterrupt:
        LOG.info("interrupted by user")
    finally:
        transport.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
