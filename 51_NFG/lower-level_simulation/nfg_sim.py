#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NFG 即时销毁存储卡 —— 下位机模拟程序
====================================================================

模拟基于 AS6C62256 (32K x 8 SRAM) 的即时销毁存储卡，用于与上位机联调。
无需真实硬件：SRAM 用内存字节数组模拟，可选持久化到文件（模拟纽扣电池保持）。

串口参数：115200 8N1
协议：上位机命令 `>命令内容<`，下位机应答 `[应答内容]`
     读流程数据包：下位机 -> `>R_[d1,d2,...,d16]<`
     读流程结束： 下位机 -> `>TX-OVER<`

存储结构：
    明文区     0x0000 ~ 0x7FEF   32752 字节 (2047 个 16 字节包)
    特征码/密码区 0x7FF0 ~ 0x7FFF      16 字节

用法示例：
    # 真实串口 (默认 COM1)
    python nfg_sim.py --port COM1 --baud 115200

    # 标准输入输出模拟串口 (无串口环境 / 自动化测试)
    python nfg_sim.py --stdio

    # 指定持久化文件与日志等级
    python nfg_sim.py --port COM1 --sram-file sram.bin --verbose
"""

import argparse
import os
import sys
import time
import threading

# ---------------------------------------------------------------------------
# 常量定义
# ---------------------------------------------------------------------------

SRAM_SIZE = 32768            # AS6C62256: 32K x 8
SRAM_ERASE_BYTE = 0xFF       # 初始/擦除填充值

PLAIN_START = 0x0000         # 明文区起始
PLAIN_END = 0x7FEF           # 明文区结束 (含)
PLAIN_SIZE = PLAIN_END - PLAIN_START + 1        # 32752

FEATURE_START = 0x7FF0       # 特征码/密码区起始
FEATURE_END = 0x7FFF         # 特征码/密码区结束 (含)
FEATURE_SIZE = FEATURE_END - FEATURE_START + 1  # 16

PACKET_SIZE = 16             # 每包字节数
DEFAULT_BAUD = 115200
DEFAULT_PORT = "COM1"
DEFAULT_SRAM_FILE = "sram.bin"
DEFAULT_RX_TIMEOUT = 2.0     # 读流程等待 [RX-OK] 超时 (秒)

PASSWORD_LEN = 6             # 六位数字密码

# 特征码算法盐值 (与 8051 端保持一致即可，模拟程序自行定义)
FEATURE_SALT = 0x5A
FEATURE_XOR_KEY = 0x3C

# ---------------------------------------------------------------------------
# 日志
# ---------------------------------------------------------------------------

class Log:
    def __init__(self, enabled=True):
        self.enabled = enabled

    def _stamp(self):
        return time.strftime("%H:%M:%S", time.localtime())

    def _emit(self, tag, msg):
        if self.enabled:
            print(f"[{self._stamp()}][{tag}] {msg}", file=sys.stderr, flush=True)

    def info(self, msg):
        self._emit("INFO", msg)

    def tx(self, msg):
        self._emit("TX->", msg)

    def rx(self, msg):
        self._emit("RX<-", msg)

    def warn(self, msg):
        self._emit("WARN", msg)

    def error(self, msg):
        self._emit("ERR ", msg)


# ---------------------------------------------------------------------------
# 特征码算法
# ---------------------------------------------------------------------------

def compute_feature(password: str) -> bytes:
    """
    由六位数字密码生成 16 字节特征码。

    算法（简单、适配 8051、资源占用小）：
        feature[i] = ((password[i % 6] + i * 0x5A) & 0xFF) ^ 0x3C
    其中 password[i%6] 为对应位的 ASCII 码。

    只要 AUTHSET 与 AUTH 使用同一算法即可保证一致。
    """
    if not isinstance(password, str):
        password = str(password)
    pw = password.encode("ascii", errors="ignore")
    if len(pw) == 0:
        pw = b"\x00"
    feat = bytearray(FEATURE_SIZE)
    for i in range(FEATURE_SIZE):
        c = pw[i % len(pw)]
        feat[i] = ((c + i * FEATURE_SALT) & 0xFF) ^ FEATURE_XOR_KEY
    return bytes(feat)


def is_feature_blank(feature: bytes) -> bool:
    """
    判断特征码区是否为“未设置”状态：
    全 0xFF 或全 0x00 视为未设置。
    """
    if not feature:
        return True
    all_ff = all(b == 0xFF for b in feature)
    all_00 = all(b == 0x00 for b in feature)
    return all_ff or all_00


# ---------------------------------------------------------------------------
# 协议解析辅助
# ---------------------------------------------------------------------------

def parse_hex_packet(payload: str):
    """
    解析如 "00,1A,FF,..." 的十六进制字节串。
    返回 (bytes, error) —— error 为 None 表示成功。
    """
    payload = payload.strip()
    if payload == "":
        return b"", None
    parts = payload.split(",")
    out = bytearray()
    for p in parts:
        p = p.strip()
        if p == "":
            continue
        try:
            v = int(p, 16)
        except ValueError:
            return None, f"非法十六进制字节: {p!r}"
        if v < 0 or v > 0xFF:
            return None, f"字节越界: {p!r}"
        out.append(v)
    return bytes(out), None


def format_hex_packet(data: bytes) -> str:
    """将字节串格式化为大写、逗号分隔的十六进制字符串。"""
    return ",".join(f"{b:02X}" for b in data)


# ---------------------------------------------------------------------------
# 存储卡核心逻辑
# ---------------------------------------------------------------------------

class NfgCard:
    """
    AS6C62256 即时销毁存储卡模拟核心（与 I/O 无关，便于单元测试）。
    """

    def __init__(self, sram_file=None, log=None, persist=True):
        self.log = log or Log(True)
        self.sram_file = sram_file
        self.persist_enabled = persist and bool(sram_file)

        # 32K SRAM
        self.sram = bytearray([SRAM_ERASE_BYTE] * SRAM_SIZE)

        # 状态
        self.authenticated = False
        self.write_mode = False
        self.read_mode = False
        self.current_addr = PLAIN_START

        self.load_sram()

    # ---------------- 持久化 ----------------

    def load_sram(self):
        """上电时从文件加载；文件不存在则用 0xFF 填充。"""
        if not self.persist_enabled:
            self.log.info("持久化未启用，SRAM 使用 0xFF 初始化")
            return
        if os.path.exists(self.sram_file):
            try:
                with open(self.sram_file, "rb") as f:
                    data = f.read()
                n = min(len(data), SRAM_SIZE)
                self.sram[0:n] = data[0:n]
                if len(data) < SRAM_SIZE:
                    self.sram[n:] = bytes([SRAM_ERASE_BYTE]) * (SRAM_SIZE - n)
                self.log.info(
                    f"已从 {self.sram_file} 加载 SRAM ({len(data)} 字节)"
                )
            except OSError as e:
                self.log.error(f"加载 {self.sram_file} 失败: {e}")
        else:
            self.log.info(f"{self.sram_file} 不存在，SRAM 使用 0xFF 初始化")

    def save_sram(self):
        """保存 SRAM 到文件（模拟纽扣电池保持）。"""
        if not self.persist_enabled:
            return
        try:
            with open(self.sram_file, "wb") as f:
                f.write(self.sram)
            self.log.info(f"已保存 SRAM 到 {self.sram_file}")
        except OSError as e:
            self.log.error(f"保存 {self.sram_file} 失败: {e}")

    # ---------------- 特征码 / 密码 ----------------

    def get_feature(self) -> bytes:
        return bytes(self.sram[FEATURE_START:FEATURE_END + 1])

    def set_feature(self, feature: bytes):
        self.sram[FEATURE_START:FEATURE_START + FEATURE_SIZE] = feature[:FEATURE_SIZE]

    def has_password(self) -> bool:
        return not is_feature_blank(self.get_feature())

    def set_password(self, password: str) -> bool:
        """设置/覆盖密码，写入特征码区。"""
        if not self._valid_password(password):
            self.log.warn(f"密码格式非法: {password!r}")
            return False
        feat = compute_feature(password)
        self.set_feature(feat)
        self.save_sram()
        self.log.info(f"密码已设置，特征码={format_hex_packet(feat)}")
        return True

    def verify_password(self, password: str) -> bool:
        """验证密码：比对特征码。"""
        if not self._valid_password(password):
            return False
        if not self.has_password():
            return False
        return compute_feature(password) == self.get_feature()

    @staticmethod
    def _valid_password(password: str) -> bool:
        return (
            isinstance(password, str)
            and len(password) == PASSWORD_LEN
            and password.isdigit()
        )

    # ---------------- 读写 ----------------

    def begin_write(self) -> bool:
        """进入写入模式。必须先认证。"""
        if not self.authenticated:
            self.log.warn("INT_W 被拒：未认证")
            return False
        self.write_mode = True
        self.read_mode = False
        self.current_addr = PLAIN_START
        self.log.info(f"进入写模式，起始地址=0x{self.current_addr:04X}")
        return True

    def begin_read(self):
        """
        进入读取模式。返回 (ok, first_packet)。
        必须先认证。读取从 0x0000 开始。
        """
        if not self.authenticated:
            self.log.warn("INT_R 被拒：未认证")
            return False, None
        self.read_mode = True
        self.write_mode = False
        self.current_addr = PLAIN_START
        self.log.info(f"进入读模式，起始地址=0x{self.current_addr:04X}")
        # 读取第一包并前移地址（与 write_packet 对称）
        first = self._read_packet_at(self.current_addr)
        self.current_addr += PACKET_SIZE
        return True, first

    def write_packet(self, data: bytes) -> bool:
        """
        写入一包数据到 current_addr。
        - 不足 16 字节补 0x00
        - 越界（超过 0x7FEF）返回 False，地址不增加
        - 成功地址 +16
        """
        if not self.write_mode:
            self.log.warn("W_ 被拒：未处于写模式")
            return False

        pkt = self._pad_packet(data)
        end_addr = self.current_addr + PACKET_SIZE - 1
        if self.current_addr > PLAIN_END or end_addr > PLAIN_END:
            self.log.warn(
                f"W_ 越界：addr=0x{self.current_addr:04X} "
                f"(明文区 0x{PLAIN_START:04X}~0x{PLAIN_END:04X})"
            )
            return False

        self.sram[self.current_addr:self.current_addr + PACKET_SIZE] = pkt
        self.log.info(
            f"写入 addr=0x{self.current_addr:04X} len={len(data)} -> {format_hex_packet(pkt)}"
        )
        self.current_addr += PACKET_SIZE
        self.save_sram()
        return True

    def read_next_packet(self):
        """
        读取 current_addr 处的一包（用于 init 之后、以及每次收到 [RX-OK] 之后调用）。
        返回 (has_more, packet)：
            has_more=False 表示已到末尾（0x7FEF 之后）
        """
        if self.current_addr > PLAIN_END:
            return False, None
        pkt = self._read_packet_at(self.current_addr)
        self.current_addr += PACKET_SIZE
        return True, pkt

    def _read_packet_at(self, addr: int) -> bytes:
        pkt = bytes(self.sram[addr:addr + PACKET_SIZE])
        if len(pkt) < PACKET_SIZE:
            pkt = pkt + bytes([0x00] * (PACKET_SIZE - len(pkt)))
        return pkt

    def _pad_packet(self, data: bytes) -> bytes:
        data = bytes(data[:PACKET_SIZE])
        if len(data) < PACKET_SIZE:
            data = data + bytes([0x00] * (PACKET_SIZE - len(data)))
        return data

    # ---------------- 中断 / 销毁 ----------------

    def abort(self):
        """中断当前读写流程，回到空闲状态。"""
        was = self.write_mode or self.read_mode
        self.write_mode = False
        self.read_mode = False
        self.current_addr = PLAIN_START
        self.log.info("已中断" + ("（原本在读写中）" if was else "（空闲）"))

    def destroy(self):
        """立即销毁：清除 SRAM 全部内容（含特征码区），重置密码状态。"""
        self.sram = bytearray([SRAM_ERASE_BYTE] * SRAM_SIZE)
        self.authenticated = False
        self.write_mode = False
        self.read_mode = False
        self.current_addr = PLAIN_START
        self.save_sram()
        self.log.warn("已执行 DESTROY，SRAM 全部清零，密码状态重置")

    def reset_transient(self):
        """错误后清理临时状态（认证保留）。"""
        self.write_mode = False
        self.read_mode = False
        self.current_addr = PLAIN_START


# ---------------------------------------------------------------------------
# 协议引擎（命令处理 + I/O 抽象）
# ---------------------------------------------------------------------------

class NfgProtocol:
    """
    将上位机命令字符串翻译为 NfgCard 操作，并产生应答。

    I/O 回调：
        send(bytes)               -> 发送原始字节
        wait_rx_ok(timeout) -> bool|None  等待上位机的 [RX-OK]
                                       True=收到, False=收到但非RX-OK, None=超时
        poll_abort() -> bool     读流程中检测是否有 ABORT 命令到来
    """

    def __init__(self, card: NfgCard, send, wait_rx_ok, poll_abort,
                 rx_timeout=DEFAULT_RX_TIMEOUT, log=None):
        self.card = card
        self._send = send
        self._wait_rx_ok = wait_rx_ok
        self._poll_abort = poll_abort
        self.rx_timeout = rx_timeout
        self.log = log or card.log
        self._running = True

    # ---------- 底层发送 ----------

    def send_bytes(self, data: bytes):
        self._send(data)

    def reply(self, text: str):
        """发送 [应答] 帧。"""
        frame = f"[{text}]"
        self.log.tx(frame)
        self._send(frame.encode("ascii", errors="replace"))

    # ---------- 命令分发 ----------

    def handle_command(self, content: str) -> bool:
        """
        处理一条命令内容（已去掉 < >）。
        返回 False 表示收到退出命令（仅用于内部）。
        """
        content = content.strip()
        if content == "":
            return True

        # 大小写不敏感，但按原样分发以保留密码
        upper = content.upper()

        # -------- 数据包 W_[...] --------
        if upper.startswith("W_"):
            payload = content[2:]
            self._cmd_write_packet(payload)
            return True

        # -------- 数据包 R_[...]（上位机一般不发，容错处理）--------
        if upper.startswith("R_"):
            # 上位机不会主动发 R_，忽略但回 RX-OK
            self.reply("RX-OK")
            return True

        # -------- 其它命令 --------
        if upper == "PING":
            self.reply("PONG")
        elif upper == "NFG_INFO":
            self.reply("NFG_1" if self.card.has_password() else "NFG_0")
        elif upper.startswith("AUTHSET_"):
            self._cmd_authset(content[len("AUTHSET_"):])
        elif upper.startswith("AUTH_"):
            self._cmd_auth(content[len("AUTH_"):])
        elif upper == "INT_W":
            self._cmd_int_write()
        elif upper == "INT_R":
            self._cmd_int_read()
        elif upper == "TX-OVER":
            self._cmd_tx_over()
        elif upper == "ABORT":
            self.card.abort()
            self.reply("ABORT-OK")
        elif upper == "DESTROY":
            self.card.destroy()
            self.reply("DESTROY-OK")
        else:
            self.log.warn(f"未知命令: {content!r}")
            self.reply("RX-ERR")
        return True

    # ---------- 各命令实现 ----------

    def _cmd_auth(self, password: str):
        password = password.strip()
        if self.card.verify_password(password):
            self.card.authenticated = True
            self.reply("AUTH_OK")
        else:
            self.card.authenticated = False
            self.reply("AUTH_ERR")

    def _cmd_authset(self, password: str):
        password = password.strip()
        if self.card.set_password(password):
            self.reply("SET-OK")
        else:
            self.reply("SET_ERR")

    def _cmd_int_write(self):
        if self.card.begin_write():
            self.reply("RX-OK")
        else:
            self.reply("RX-ERR")

    def _cmd_int_read(self):
        ok, first = self.card.begin_read()
        if not ok:
            self.reply("RX-ERR")
            return
        self.reply("RX-OK")
        # 之后由 run_read_loop 主动推包
        self._read_loop_pending = True
        self._read_first_packet = first

    def _cmd_write_packet(self, payload: str):
        data, err = parse_hex_packet(payload)
        if err is not None:
            self.log.warn(f"W_ 解析失败: {err}")
            self.reply("RX-ERR")
            return
        if not self.card.write_mode:
            self.reply("RX-ERR")
            return
        if len(data) > PACKET_SIZE:
            self.log.warn(f"W_ 包过大: {len(data)} 字节")
            self.reply("RX-ERR")
            return
        if self.card.write_packet(data):
            self.reply("RX-OK")
        else:
            self.reply("RX-ERR")

    def _cmd_tx_over(self):
        self.card.write_mode = False
        self.card.read_mode = False
        self.card.current_addr = PLAIN_START
        self.log.info("写流程结束 (TX-OVER)")
        self.reply("RX-OK")

    # ---------- 读流程（阻塞式循环）----------

    def run_read_loop(self):
        """
        读流程主循环：主动推包，等待上位机 [RX-OK]，并支持 ABORT。
        必须在 handle_command(INT_R) 后调用。
        """
        first = getattr(self, "_read_first_packet", None)
        pending = getattr(self, "_read_loop_pending", False)
        self._read_loop_pending = False
        self._read_first_packet = None
        if not pending:
            return

        packet = first
        while True:
            # 发送数据包（无论是否最后一包，格式一致）
            frame = f">R_{format_hex_packet(packet)}<"
            self.log.tx(frame)
            self._send(frame.encode("ascii", errors="replace"))

            # 等待上位机 [RX-OK]（期间检测 ABORT）
            res = self._wait_rx_ok(self.rx_timeout, self._poll_abort)
            if res == "abort":
                self.log.info("读流程被 ABORT 中断")
                self.card.abort()
                self.reply("ABORT-OK")
                return
            if res is True:
                # 继续下一包
                has_more, packet = self.card.read_next_packet()
                if not has_more:
                    self._send(b">TX-OVER<")
                    self.log.tx(">TX-OVER<")
                    self.card.read_mode = False
                    self.card.current_addr = PLAIN_START
                    self.log.info("读流程完成 (TX-OVER)")
                    return
            else:
                # 超时或收到非 RX-OK
                self.log.warn("读流程等待 [RX-OK] 超时/异常，发送 RX-ERR 并停止")
                self.reply("RX-ERR")
                self.card.reset_transient()
                return


# ---------------------------------------------------------------------------
# I/O 传输层
# ---------------------------------------------------------------------------

class Transport:
    """抽象传输层基类。"""

    def read_byte(self, timeout=None):
        raise NotImplementedError

    def write_bytes(self, data: bytes):
        raise NotImplementedError

    def close(self):
        pass


class SerialTransport(Transport):
    """基于 pyserial 的串口传输层。"""

    def __init__(self, port, baud):
        import serial  # 延迟导入，允许 --stdio 无 pyserial
        self.ser = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
        )
        self.port = port
        self.baud = baud

    def read_byte(self, timeout=None):
        # pyserial 已设 read timeout，这里做基于 deadline 的补充
        deadline = None if timeout is None else time.time() + timeout
        while True:
            b = self.ser.read(1)
            if b:
                return b
            if deadline is not None and time.time() >= deadline:
                return b""
        return b""

    def write_bytes(self, data: bytes):
        self.ser.write(data)
        self.ser.flush()

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


class StdioTransport(Transport):
    """
    用标准输入输出模拟串口字节流。
    跨平台实现（Windows 的 select 不支持管道/文件）：
    后台线程持续从 stdin 读取字节放入队列，read_byte 从队列按超时取，
    输出直接写到 stdout.buffer。
    """

    def __init__(self):
        import queue
        self.inp = sys.stdin.buffer
        self.out = sys.stdout.buffer
        self._q = queue.Queue()
        self._closed = False
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self):
        while not self._closed:
            try:
                b = self.inp.read(1)
            except Exception:
                break
            if not b:
                # EOF
                self._q.put(None)
                break
            self._q.put(b)

    def read_byte(self, timeout=None):
        try:
            if timeout is None:
                item = self._q.get()
            else:
                item = self._q.get(timeout=timeout)
        except Exception:
            # queue.Empty -> 超时
            return b""
        if item is None:
            return b""
        return item

    def write_bytes(self, data: bytes):
        self.out.write(data)
        self.out.flush()

    def close(self):
        self._closed = True


# ---------------------------------------------------------------------------
# 串口会话：字节流 <-> 帧 解析 + 协议驱动
# ---------------------------------------------------------------------------

class NfgSession:
    """
    帧解析器 + 协议驱动的串行调度。

    帧格式：
        上位机 -> 下位机：>...<
        下位机 -> 上位机：[...]   以及读流程的 >R_[...]< / >TX-OVER<
    """

    def __init__(self, card, transport, rx_timeout=DEFAULT_RX_TIMEOUT, log=None):
        self.card = card
        self.t = transport
        self.rx_timeout = rx_timeout
        self.log = log or card.log

        self._abort_flag = threading.Event()
        self._in_read_loop = False

        self.proto = NfgProtocol(
            card=card,
            send=self._raw_send,
            wait_rx_ok=self._wait_rx_ok,
            poll_abort=self._poll_abort,
            rx_timeout=rx_timeout,
            log=self.log,
        )

    # ---------- 原始收发 ----------

    def _raw_send(self, data: bytes):
        self.t.write_bytes(data)

    # ---------- 帧解析 ----------

    def _read_frame(self):
        """
        读取一个以 '>' 开头、'<' 结尾的帧。
        返回帧内容字符串，或 None（EOF/退出）。
        """
        # 寻找起始 '>'
        while True:
            b = self.t.read_byte(timeout=None)
            if not b:
                return None
            ch = b.decode("latin-1", errors="replace")
            if ch == ">":
                break
            # 忽略帧间杂散字节（如换行）
            if ch in "\r\n\t ":
                continue
            # 上位机可能直接发 [RX-OK]，在读流程中由 _wait_rx_ok 处理
            # 这里忽略其它杂散字符
        # 累积直到 '<'
        buf = []
        while True:
            b = self.t.read_byte(timeout=None)
            if not b:
                return None
            ch = b.decode("latin-1", errors="replace")
            if ch == "<":
                break
            buf.append(ch)
        content = "".join(buf)
        self.log.rx(f">{content}<")
        return content

    # ---------- 读流程等待 [RX-OK] ----------

    def _wait_rx_ok(self, timeout, poll_abort=None):
        """
        等待上位机应答 [RX-OK]。
        返回：True(received RX-OK) / False(收到非RX-OK) / "abort"(被中断) / None(超时)
        在等待期间会解析到达的完整帧：
            - [RX-OK]  -> True
            - [其它]   -> False
            - >ABORT<  -> "abort"
        """
        deadline = time.time() + timeout
        buf = ""
        while time.time() < deadline:
            b = self.t.read_byte(timeout=0.05)
            if not b:
                continue
            ch = b.decode("latin-1", errors="replace")
            buf += ch
            # 完整 [ ... ]
            if "[" in buf and "]" in buf:
                start = buf.index("[")
                end = buf.index("]", start)
                inner = buf[start + 1:end]
                buf = buf[end + 1:]
                self.log.rx(f"[{inner}]")
                if inner.strip().upper() == "RX-OK":
                    return True
                return False
            # 完整 > ... < (例如 ABORT)
            if ">" in buf and "<" in buf:
                start = buf.index(">")
                end = buf.index("<", start)
                inner = buf[start + 1:end]
                buf = buf[end + 1:]
                self.log.rx(f">{inner}<")
                if inner.strip().upper() == "ABORT":
                    return "abort"
                # 其它帧忽略，继续等待
        return None

    def _poll_abort(self):
        return self._abort_flag.is_set()

    # ---------- 主循环 ----------

    def run(self):
        self.log.info("下位机模拟已启动，等待命令 …")
        try:
            while True:
                content = self._read_frame()
                if content is None:
                    self.log.info("输入结束，退出。")
                    break

                upper = content.strip().upper()

                # 读流程中的 ABORT 需要特殊处理（异步打断）
                self.proto.handle_command(content)

                # 若刚进入读模式，执行阻塞式读循环
                if self.proto.card.read_mode and getattr(
                    self.proto, "_read_loop_pending", False
                ):
                    self._in_read_loop = True
                    self.proto.run_read_loop()
                    self._in_read_loop = False

                if upper in ("EXIT", "QUIT"):
                    self.log.info("收到退出命令，退出。")
                    break
        except KeyboardInterrupt:
            self.log.info("收到 Ctrl-C，退出。")
        finally:
            try:
                self.card.save_sram()
            except Exception:
                pass


# ---------------------------------------------------------------------------
# 命令行入口
# ---------------------------------------------------------------------------

def build_arg_parser():
    p = argparse.ArgumentParser(
        description="NFG 即时销毁存储卡 —— 下位机模拟程序 (AS6C62256)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--port", default=DEFAULT_PORT,
                   help="串口端口号，例如 COM1")
    p.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                   help="波特率")
    p.add_argument("--sram-file", default=DEFAULT_SRAM_FILE,
                   help="SRAM 持久化文件路径（模拟纽扣电池保持）")
    p.add_argument("--no-persist", action="store_true",
                   help="禁用持久化（纯内存，退出不保存）")
    p.add_argument("--rx-timeout", type=float, default=DEFAULT_RX_TIMEOUT,
                   help="读流程等待 [RX-OK] 的超时（秒）")
    p.add_argument("--stdio", action="store_true",
                   help="使用标准输入输出模拟串口（无串口环境）")
    p.add_argument("--quiet", action="store_true",
                   help="关闭控制台日志")
    return p


def main(argv=None):
    args = build_arg_parser().parse_args(argv)
    log = Log(enabled=not args.quiet)

    card = NfgCard(
        sram_file=None if args.no_persist else args.sram_file,
        log=log,
        persist=not args.no_persist,
    )

    if args.stdio:
        transport = StdioTransport()
        log.info("使用 stdio 模式模拟串口")
    else:
        try:
            transport = SerialTransport(args.port, args.baud)
        except Exception as e:
            log.error(f"打开串口 {args.port} 失败: {e}")
            log.error("提示：无串口请使用 --stdio 模式")
            return 2
        log.info(f"串口已打开 {args.port} @ {args.baud} 8N1")

    session = NfgSession(
        card=card,
        transport=transport,
        rx_timeout=args.rx_timeout,
        log=log,
    )

    try:
        session.run()
    finally:
        transport.close()
        card.save_sram()
    return 0


if __name__ == "__main__":
    sys.exit(main())
