"""协议帧定义与解析。

帧格式：>内容<  帧内不允许换行。
每帧后附带标准换行间隔 \\r\\n（发送方始终添加，接收方容错接收）。
数据帧：R_[..] / W_[..]，ASCII 十六进制，每字节两位，逗号分隔。
"""
from __future__ import annotations

FRAME_START = ">"
FRAME_END = "<"
#: 帧尾标准换行间隔
FRAME_EOL = "\r\n"

# 应答帧
ACK_RS = "R-S"
ACK_ERR = "R-ERR"
ACK_RXOK = "RX-OK"
ACK_TXOVER = "TX-OVER"
ACK_YES = "YES"

# 命令
CMD_ROUTER = "ROUTER_"
CMD_EDGE = "EDGE_"
CMD_INT_R = "INT_R"
CMD_INT_W = "INT_W"
CMD_W = "W_"
CMD_R = "R_"
CMD_TX_OVER = "TX-OVER"
CMD_ABORT = "ABORT"
CMD_TYPE_ECHO = "TYPE_ECHO"


class FrameError(Exception):
    """帧格式错误。"""


def build(frame_body: str) -> bytes:
    """把内容包成 >...< 并在末尾附加标准换行间隔 \\r\\n。"""
    return f"{FRAME_START}{frame_body}{FRAME_END}{FRAME_EOL}".encode("ascii")


def parse_data_payload(body: str) -> list[int]:
    """解析 R_[..] / W_[..] 中的 [..] 十六进制列表。

    输入为整体内容，例如 'R_[00,1A,FF]'，返回 [0, 26, 255]。
    大小写不敏感；每字节必须两位合法十六进制。
    """
    lb = body.find("[")
    rb = body.rfind("]")
    if lb < 0 or rb < 0 or rb <= lb:
        raise FrameError("missing [ ]")
    inner = body[lb + 1:rb].strip()
    if inner == "":
        return []
    values: list[int] = []
    for token in inner.split(","):
        tok = token.strip()
        if len(tok) != 2:
            raise FrameError(f"bad byte width: {token!r}")
        try:
            values.append(int(tok, 16))
        except ValueError as exc:
            raise FrameError(f"bad hex: {token!r}") from exc
    return values


def parse_hex_addr(text: str) -> int:
    """解析 0xXXXX / XXXX 形式的地址。"""
    t = text.strip()
    if t.lower().startswith("0x"):
        t = t[2:]
    if t == "":
        raise FrameError("empty addr")
    try:
        return int(t, 16)
    except ValueError as exc:
        raise FrameError(f"bad addr: {text!r}") from exc


def format_data_body(prefix: str, data: list[int]) -> str:
    """把字节列表格式化为 'P_[AA,BB,..]' 形式（大写两位）。"""
    joined = ",".join(f"{b:02X}" for b in data)
    return f"{prefix}_[{joined}]"

