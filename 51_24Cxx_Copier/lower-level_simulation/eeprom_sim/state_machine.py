"""命令解析与状态机。

状态：
  ST_IDLE        空闲
  ST_ROUTER_SET  已设置 ROUTER，等待 EDGE
  ST_EDGE_SET    已设置 EDGE，等待 INT_R / INT_W
  ST_WRITE_WAIT  写等待（已 INT_W，等待 W 包）
  ST_WRITE_CURR  正在写当前包
  ST_WRITE_OVER  当前包写完成
  ST_READ_WAIT   读等待
  ST_READ_CURR   正在读当前包
  ST_READ_OVER   当前包读完成
"""
from __future__ import annotations

import sys
import time

from . import protocol
from .devices import DEVICE_TABLE, EepromDevice
from .i2c_timing import I2CTiming
from .tm1637 import TM1637Sim
from .transport import Transport

PACKET_BYTES = 16

ST_IDLE = "ST_IDLE"
ST_ROUTER_SET = "ST_ROUTER_SET"
ST_EDGE_SET = "ST_EDGE_SET"
ST_WRITE_WAIT = "ST_WRITE_WAIT"
ST_WRITE_CURR = "ST_WRITE_CURR"
ST_WRITE_OVER = "ST_WRITE_OVER"
ST_READ_WAIT = "ST_READ_WAIT"
ST_READ_CURR = "ST_READ_CURR"
ST_READ_OVER = "ST_READ_OVER"


class Simulator:
    def __init__(self, transport: Transport, data_dir: str,
                 i2c_rate: int = 250_000, write_cycle: float = 0.020,
                 rxok_timeout: float = 2.0, display: bool = True,
                 i2c_sleep: bool = True,
                 inject_fail_addrs: set[int] | None = None,
                 flush_each_packet: bool = False,
                 log_stream=None) -> None:
        self.tp = transport
        self.data_dir = data_dir
        # 日志一律走 stderr，避免污染 stdio 模式的 stdout 帧流
        self.log_stream = log_stream if log_stream is not None else sys.stderr
        self.i2c = I2CTiming(i2c_rate, enabled=i2c_sleep)
        self.write_cycle = write_cycle
        self.rxok_timeout = rxok_timeout
        self.tm = TM1637Sim(enabled=display, log_stream=self.log_stream)
        self.inject_fail_addrs = inject_fail_addrs or set()
        self.flush_each_packet = flush_each_packet

        self.devices: dict[int, EepromDevice] = {}
        self._load_devices()

        # 状态
        self.state = ST_IDLE
        self.router: int | None = None          # 当前 7 位 I2C 芯片地址
        self.edge: int | None = None            # 末尾地址
        self.cur_addr = 0                       # 当前操作地址
        self.running = True
        self._show()

    # ---- 初始化 ----
    def _load_devices(self) -> None:
        for addr, spec in DEVICE_TABLE.items():
            self.devices[addr] = EepromDevice(
                spec, self.data_dir, inject_fail_addrs=self.inject_fail_addrs,
                flush_each_packet=self.flush_each_packet)

    def flush_all_devices(self) -> None:
        """把所有有改动的设备内存落盘（供流程结束/退出时调用）。

        落盘失败只记录告警，不抛出：内存才是主存储。
        """
        for addr, dev in self.devices.items():
            if not dev.dirty:
                continue
            if dev.flush():
                self._log(f"[SIM] flushed 0x{addr:02X} -> {dev.spec.bin_name}")
            else:
                self._log(f"[SIM] WARN: flush 0x{addr:02X} failed "
                          f"(kept in memory)")

    def _show(self) -> None:
        self.tm.show(self.state, self.router)

    def _log(self, msg: str) -> None:
        print(msg, file=self.log_stream, flush=True)

    # ---- 顶层循环 ----
    def run(self) -> None:
        self._log("[SIM] start. state=%s" % self.state)
        consecutive_errors = 0
        while self.running:
            # 读取阶段也要容错：串口抖动/异常不应导致进程退出
            try:
                frame = self.tp.read_frame(timeout=None)
            except Exception as exc:
                consecutive_errors += 1
                self._log(f"[SIM] transport read error: {exc!r}")
                if consecutive_errors >= 10:
                    self._log("[SIM] too many transport errors, exit.")
                    break
                time.sleep(0.05)
                continue
            if frame is None:
                # 仅当 stdio EOF / 串口被关闭时才结束
                if getattr(self.tp, "eof", False):
                    self._log("[SIM] input closed, exit.")
                    break
                # 否则视为一次空读，继续等待
                continue
            consecutive_errors = 0
            self._log(f"[RX] >{frame}<")
            try:
                self._handle(frame)
            except Exception as exc:  # 任何未预期异常都回 R-ERR，但不退出
                self._log(f"[SIM] internal error: {exc!r}")
                try:
                    self._reply_err()
                except Exception as exc2:
                    self._log(f"[SIM] reply after error failed: {exc2!r}")
                self._abort_to_idle()

    # ---- 应答辅助 ----
    def _reply(self, body: str) -> None:
        self.tp.write_frame(body)
        self._log(f"[TX] >{body}<")

    def _reply_err(self) -> None:
        self._reply(protocol.ACK_ERR)

    def _reply_ok(self) -> None:
        self._reply(protocol.ACK_RS)

    def _abort_to_idle(self) -> None:
        """错误后：停止任务、清空临时状态、回到 IDLE（保留已写入数据）。"""
        self.edge = None
        self.cur_addr = 0
        self.state = ST_IDLE
        self._show()

    # ---- 命令分发 ----
    def _handle(self, frame: str) -> None:
        body = frame.strip()
        if body == protocol.CMD_TYPE_ECHO:
            self._cmd_type_echo(body)
        elif body.startswith(protocol.CMD_ROUTER):
            self._cmd_router(body)
        elif body.startswith(protocol.CMD_EDGE):
            self._cmd_edge(body)
        elif body == protocol.CMD_INT_R:
            self._cmd_int_r(body)
        elif body == protocol.CMD_INT_W:
            self._cmd_int_w(body)
        elif body.startswith(protocol.CMD_W):
            self._cmd_w(body)
        elif body == protocol.CMD_TX_OVER:
            self._cmd_tx_over(body)
        elif body == protocol.ACK_RXOK:
            # 写流程中上位机不会主动发 RX-OK；这里是读流程内部消费，
            # 顶层收到单独的 RX-OK 属于协议不匹配。
            self._log("[SIM] unexpected RX-OK in top loop -> R-ERR")
            self._reply_err()
        elif body == protocol.CMD_ABORT:
            self._cmd_abort(body)
        else:
            self._log(f"[SIM] unknown command {body!r}")
            self._reply_err()

    # ---- TYPE_ECHO ----
    def _cmd_type_echo(self, body: str) -> None:
        """查询当前设备是否为指定的工作设备，直接回 YES。

        不影响任何状态，可随时调用。
        """
        self._reply(protocol.ACK_YES)

    # ---- ROUTER ----
    def _cmd_router(self, body: str) -> None:
        raw = body[len(protocol.CMD_ROUTER):]
        try:
            addr = protocol.parse_hex_addr(raw)
        except protocol.FrameError:
            self._reply_err()
            return
        if addr < 0x50 or addr > 0x57 or addr not in self.devices:
            self._reply_err()
            return
        self.router = addr
        self.edge = None
        self.cur_addr = 0
        self.state = ST_ROUTER_SET
        self._reply_ok()
        self._show()

    # ---- EDGE ----
    def _cmd_edge(self, body: str) -> None:
        # 必须先设置 ROUTER
        if self.router is None:
            self._reply_err()
            return
        raw = body[len(protocol.CMD_EDGE):]
        try:
            addr = protocol.parse_hex_addr(raw)
        except protocol.FrameError:
            self._reply_err()
            return
        spec = self.devices[self.router].spec
        if addr < 0 or addr > spec.max_addr:
            self._reply_err()
            return
        self.edge = addr
        self.cur_addr = 0
        self.state = ST_EDGE_SET
        self._reply_ok()
        self._show()

    # ---- INT_R ----
    def _cmd_int_r(self, body: str) -> None:
        if self.router is None or self.edge is None:
            self._reply_err()
            return
        self.cur_addr = 0
        self.state = ST_READ_WAIT
        self._reply_ok()
        self._show()
        self._run_read()

    # ---- INT_W ----
    def _cmd_int_w(self, body: str) -> None:
        if self.router is None or self.edge is None:
            self._reply_err()
            return
        self.cur_addr = 0
        self.state = ST_WRITE_WAIT
        self._reply_ok()
        self._show()

    # ---- 读流程 ----
    def _run_read(self) -> None:
        dev = self.devices[self.router]
        while True:
            self.state = ST_READ_CURR
            self._show()

            remaining = self.edge - self.cur_addr + 1
            n = min(PACKET_BYTES, remaining)

            # 模拟 I2C 读传输
            self.i2c.packet_transfer()

            data = list(dev.read_block(self.cur_addr, n))
            if len(data) < PACKET_BYTES:
                data += [0x00] * (PACKET_BYTES - len(data))
            body = protocol.format_data_body("R", data)
            self.tp.write_frame(body)
            self._log(f"[TX] >{body}<")

            self.state = ST_READ_OVER
            self._show()

            # 等待上位机 RX-OK
            ack = self.tp.read_frame(timeout=self.rxok_timeout)
            if ack is None or ack.strip() != protocol.ACK_RXOK:
                self._log("[SIM] read flow: RX-OK timeout/mismatch -> R-ERR")
                self._reply_err()
                self._abort_to_idle()
                return

            self.cur_addr += n

            if self.cur_addr > self.edge:
                # 最后一包已确认，发 TX-OVER
                self._reply(protocol.ACK_TXOVER)
                self.state = ST_READ_WAIT
                self._show()
                self._abort_to_idle()  # 读流程结束回到 IDLE
                return
            self.state = ST_READ_WAIT
            self._show()

    # ---- 写流程 ----
    def _cmd_w(self, body: str) -> None:
        if self.state not in (ST_WRITE_WAIT, ST_WRITE_OVER):
            self._reply_err()
            return
        try:
            values = protocol.parse_data_payload(body)
        except protocol.FrameError:
            self._reply_err()
            self._abort_to_idle()
            return
        if len(values) != PACKET_BYTES:
            self._log(f"[SIM] W packet length {len(values)} != 16 -> R-ERR")
            self._reply_err()
            self._abort_to_idle()
            return
        if any(b < 0 or b > 0xFF for b in values):
            self._reply_err()
            self._abort_to_idle()
            return

        self.state = ST_WRITE_CURR
        self._show()

        remaining = self.edge - self.cur_addr + 1
        n = min(PACKET_BYTES, remaining)

        # 1) 先回 RX-OK
        self._reply(protocol.ACK_RXOK)

        # 2) 模拟 I2C 写传输
        self.i2c.packet_transfer()

        # 3) 等待 20ms 写周期
        time.sleep(self.write_cycle)

        # 4) 只写前 N 字节，更新 bin
        dev = self.devices[self.router]
        payload = bytes(values[:n])
        ok = dev.write_block(self.cur_addr, payload)

        if not ok:
            self._log("[SIM] write failed (I2C NACK/injected) -> R-ERR")
            self._reply_err()
            # 兜底落盘：保留此前已成功写入内存的数据
            self.flush_all_devices()
            self._abort_to_idle()  # 地址不增加
            return

        self.cur_addr += n
        self.state = ST_WRITE_OVER
        self._reply_ok()
        self._show()

    # ---- TX-OVER ----
    def _cmd_tx_over(self, body: str) -> None:
        if self.state not in (ST_WRITE_OVER, ST_WRITE_WAIT):
            self._reply_err()
            return
        # 写流程正常结束：统一落盘
        self.flush_all_devices()
        self._reply_ok()
        self._abort_to_idle()

    # ---- ABORT ----
    def _cmd_abort(self, body: str) -> None:
        # 中止前兜底落盘：保留内存中已成功写入的数据
        self.flush_all_devices()
        self._abort_to_idle()
        self._reply_ok()
