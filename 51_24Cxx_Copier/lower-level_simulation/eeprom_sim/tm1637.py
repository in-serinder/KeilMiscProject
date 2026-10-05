from __future__ import annotations

import sys

# 状态码 -> 两位显示字符
STATUS_CODE = {
    "ST_IDLE": "ID",
    "ST_ROUTER_SET": "RO",
    "ST_EDGE_SET": "ED",
    "ST_WRITE_WAIT": "WW",
    "ST_WRITE_CURR": "WC",
    "ST_WRITE_OVER": "WO",
    "ST_READ_WAIT": "RW",
    "ST_READ_CURR": "RC",
    "ST_READ_OVER": "RO",
}


class TM1637Sim:
    """把状态 + 路由地址打印为 [TM1637] XXYY 形式。"""

    def __init__(self, enabled: bool = True, log_stream=None) -> None:
        self.enabled = enabled
        self.log_stream = log_stream if log_stream is not None else sys.stderr
        self._last: str | None = None

    def show(self, status: str, router_addr: int | None) -> None:
        if not self.enabled:
            return
        prefix = STATUS_CODE.get(status, "??")
        if router_addr is None:
            # 空闲无路由时显示 IDLE
            frame = "IDLE"
        else:
            frame = f"{prefix}{router_addr:02X}"
        if frame == self._last:
            return
        self._last = frame
        print(f"[TM1637] {frame}", file=self.log_stream, flush=True)
