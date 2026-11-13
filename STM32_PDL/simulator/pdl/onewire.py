# -*- coding: utf-8 -*-
"""
DS18B20 行为模拟。

三个必须模拟的坑（真机上都会遇到）：
  1. 转换需要 93.75 / 187.5 / 375 / 750 ms，期间读到的是上一次的值
  2. 上电默认值是 0x0550（85 ℃），不是真实温度
  3. 探头可能掉线（无应答），此时不能返回 85 ℃ 冒充正常
"""

from .config import temp_min_interval_ms


class Ds18b20:
    def __init__(self, idx, temp_c=25.0, res_bits=12, present=True):
        self.idx = idx
        self.temp_c = temp_c
        self.res_bits = res_bits
        self.present = present
        # 上电默认：85 ℃，这是真实 DS18B20 的 scratchpad 初值
        self._c16 = 0x0550
        self._converted = False
        self._pending = False
        self._busy_until = 0

    @property
    def conv_us(self) -> int:
        return temp_min_interval_ms(self.res_bits) * 1000

    def start_convert(self, now_us: int):
        """发 Convert T：立刻返回，750 ms 后才好"""
        self._pending = True
        self._busy_until = now_us + self.conv_us

    def _latch(self):
        step = 1 << (12 - self.res_bits)
        v = int(round(self.temp_c * 16.0 / step)) * step
        # 16 bit 有符号
        if v > 32767:
            v = 32767
        if v < -32768:
            v = -32768
        self._c16 = v & 0xFFFF
        self._converted = True
        self._pending = False

    def read(self, now_us: int):
        """返回 c16（可能是有符号的负值）；掉线返回 None"""
        if not self.present:
            return None
        if self._pending and now_us >= self._busy_until:
            self._latch()
        # 转换中 / 从未转换 -> 返回上一次结果（上电时就是 0x0550）
        v = self._c16
        return v - 0x10000 if v >= 0x8000 else v

    def is_default_85(self) -> bool:
        """是否仍是上电默认值（上位机应显示 -- 而不是 85.0 ℃）"""
        return not self._converted
