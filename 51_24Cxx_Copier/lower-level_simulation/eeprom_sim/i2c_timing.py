"""模拟 I2C 总线时序。

I2C 速率默认 250kHz，每字节按 9 位（含 ACK）计算。
"""
from __future__ import annotations

import time


class I2CTiming:
    """按配置速率估算并 sleep 模拟 I2C 传输耗时。"""

    def __init__(self, rate_hz: int = 250_000, enabled: bool = True) -> None:
        self.rate_hz = rate_hz
        self.enabled = enabled

    def byte_time(self) -> float:
        """单个 I2C 字节（9 位含 ACK）耗时（秒）。"""
        return 9.0 / self.rate_hz

    def transfer(self, total_bytes: int) -> float:
        """模拟 total_bytes 个 I2C 字节的传输耗时并返回时长（秒）。

        一包 16 字节读写，含控制字节、地址字节、ACK 等，
        总时间约 20~22 字节 × 36us ≈ 0.8ms。
        """
        duration = total_bytes * self.byte_time()
        if self.enabled:
            time.sleep(duration)
        return duration

    def packet_transfer(self) -> float:
        """一包读写传输估计（约 22 字节 ≈ 0.8ms @250kHz）。"""
        return self.transfer(22)
