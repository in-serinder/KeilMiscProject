# -*- coding: utf-8 -*-
"""
PDL 协议层 —— 与 docs/02_HID通信协议.md (PDL/1.4) 一一对应。

本模块只做「字节 <-> 语义」的搬运，不含任何业务判断，
模拟器与未来的 C 固件都应保持同样的分层：帧层 -> 命令层 -> 状态机。
"""

import struct

# ---------------------------------------------------------------- 常量
SOF = 0xA5
VER = 0x01
REPORT = 64          # HID 报文定长
HDR = 8              # SOF VER TYPE FLAGS LEN(2) SEQ(2)
CRC = 2
MAX_PAYLOAD = REPORT - HDR - CRC   # 54


class T:
    """消息类型"""
    IDENTIFY = 0x10
    GET_CH_CONFIG = 0x11
    ACQUIRE = 0x20
    READ_TEMP = 0x21
    ABORT = 0x22
    GET_STATUS = 0x23
    SET_CH_CONFIG = 0x30
    ZERO = 0x31
    SAVE_CONFIG = 0x32
    TIME_SYNC = 0x40
    RESET = 0x60
    ECHO = 0x7F

    ACK = 0x80
    NACK = 0x81
    DATA = 0x90
    EVENT = 0xA0
    HEARTBEAT = 0xA1


class FF:
    """帧 FLAGS"""
    RESP = 0x01
    EVENT = 0x02
    FIRST = 0x04      # 块的首片
    MORE = 0x08       # 后面还有分片


class BF:
    """块头 flags（与帧 FLAGS 是两回事，别混）"""
    SATURATED = 0x01
    OVERRUN = 0x02
    CFG_CHANGED = 0x04
    FIRST = 0x08
    CAL_INVALID = 0x10
    TRIGGERED = 0x20
    FINAL = 0x40
    TEMP_BLOCK = 0x80


class FMT:
    RAW16 = 0          # i16 v_raw, i16 i_raw  (4 B/通道/tick)
    RAW32 = 1
    UVUA = 2
    MVMA = 3
    BIN16 = 4          # 16 B/通道/桶
    BIN32 = 5
    TEMP16 = 7         # i16 摄氏度 x16 (2 B/通道/点)


class ST:
    """状态码"""
    OK = 0x00
    UNKNOWN_CMD = 0x01
    BAD_PARAM = 0x02
    BAD_STATE = 0x03
    BUSY = 0x04
    CRC_ERROR = 0x05
    LEN_ERROR = 0x06
    UNSUPPORTED = 0x07
    NOT_CALIBRATED = 0x08
    OVERFLOW = 0x09
    NO_DATA = 0x0A
    INTERNAL = 0x0F


class ST_:
    """设备状态"""
    BOOT = 0x00
    IDLE = 0x01
    ARMED = 0x02
    RUNNING = 0x03
    DRAINING = 0x04
    ERROR = 0x05


class EV:
    """事件码"""
    OVERRUN = 0x01
    SATURATE = 0x02
    OVERTEMP = 0x03
    CAL_INVALID = 0x04
    SELF_TEST_FAIL = 0x05
    CFG_APPLIED = 0x06
    LOG_FULL = 0x07
    USB_RESET = 0x08
    HW_FAULT = 0x09
    TRIG_ARMED = 0x0A
    TRIG_STARTED = 0x0B
    TRIG_STOPPED = 0x0C
    AUTO_STOP = 0x0D
    ABORTED = 0x0E
    TEMP_LOST = 0x0F


# ---------------------------------------------------------------- CRC16
def _mk_table():
    tbl = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if (c & 0x8000) else (c << 1) & 0xFFFF
        tbl.append(c)
    return tbl


_CRC_TABLE = _mk_table()


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE, poly 0x1021, init 0xFFFF, 不反射, 无终异或"""
    c = 0xFFFF
    for b in data:
        c = ((c << 8) & 0xFFFF) ^ _CRC_TABLE[((c >> 8) ^ b) & 0xFF]
    return c


# ---------------------------------------------------------------- 帧
class Frame:
    __slots__ = ("type", "flags", "seq", "payload")

    def __init__(self, type_: int, flags: int, seq: int, payload: bytes = b""):
        self.type = type_
        self.flags = flags
        self.seq = seq
        self.payload = payload

    def __repr__(self):
        return "Frame(0x%02X, f=0x%02X, seq=%d, %dB)" % (
            self.type, self.flags, self.seq, len(self.payload))

    def encode(self) -> bytes:
        """编码成定长 64 B 报文（不足补 0）"""
        if len(self.payload) > MAX_PAYLOAD:
            raise ValueError("payload too long: %d" % len(self.payload))
        body = struct.pack("<BBBBHH", SOF, VER, self.type, self.flags,
                           len(self.payload), self.seq) + self.payload
        body += struct.pack("<H", crc16(body))
        return body + b"\x00" * (REPORT - len(body))

    def hex(self) -> str:
        return " ".join("%02X" % b for b in self.encode()[:HDR + len(self.payload) + CRC])


def parse_frame(raw: bytes):
    """解析 64 B 报文，失败返回 None"""
    if len(raw) < HDR + CRC or raw[0] != SOF:
        return None
    _, ver, type_, flags, ln, seq = struct.unpack("<BBBBHH", raw[:HDR])
    if ver != VER or ln > MAX_PAYLOAD:
        return None
    end = HDR + ln
    body, got = raw[:end], struct.unpack("<H", raw[end:end + CRC])[0]
    if crc16(body) != got:
        return None
    return Frame(type_, flags, seq, bytes(raw[HDR:end]))


class Decoder:
    """流式解码：TCP 可能一次给不足 64 B，也可能一次给 128 B"""

    def __init__(self):
        self._buf = bytearray()
        self.bad = 0

    def feed(self, chunk: bytes):
        self._buf += chunk
        out = []
        while len(self._buf) >= REPORT:
            raw = bytes(self._buf[:REPORT])
            del self._buf[:REPORT]
            f = parse_frame(raw)
            if f is None:
                self.bad += 1
                continue
            out.append(f)
        return out


class Fragmenter:
    """把一整块 payload 切成若干 54 B 以内的帧。

    首片带 FIRST，后面如果还有则带 MORE；中间片只带 MORE；末片什么都不带。
    （FIRST=1,MORE=0 单片；1,1 首片；0,1 中间；0,0 末片）
    """

    @staticmethod
    def split(payload: bytes, type_: int, next_seq) -> list:
        frames = []
        total = len(payload)
        pos = 0
        while pos < total:
            chunk = payload[pos:pos + MAX_PAYLOAD]
            pos += len(chunk)
            flags = 0
            if pos - len(chunk) == 0:
                flags |= FF.FIRST
            if pos < total:
                flags |= FF.MORE
            frames.append(Frame(type_, flags, next_seq(), chunk))
        if not frames:   # 空块也要发一帧，否则对端等不到
            frames.append(Frame(type_, FF.FIRST, next_seq(), b""))
        return frames


# ---------------------------------------------------------------- 块头
BLOCK_HDR = 14


def block_header(sample_index: int, t_ms: int, block_seq: int,
                 count: int, mask: int, flags: int, fmt: int) -> bytes:
    return struct.pack("<IIHBBBB", sample_index & 0xFFFFFFFF, t_ms & 0xFFFFFFFF,
                       block_seq & 0xFFFF, count & 0xFF, mask & 0xFF,
                       flags & 0xFF, fmt & 0xFF)


def ack_payload(cmd: int, status: int = ST.OK, detail: int = 0, extra: bytes = b"") -> bytes:
    return struct.pack("<BBH", cmd, status, detail) + extra


def event_payload(code: int, ch: int, param: int, t_ms: int) -> bytes:
    return struct.pack("<BBHI", code, ch, param & 0xFFFF, t_ms & 0xFFFFFFFF)
