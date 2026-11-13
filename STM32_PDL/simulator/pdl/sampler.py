# -*- coding: utf-8 -*-
"""
采样与块组装。

两种上报粒度：
  RAW  —— 每个 tick 的原始码值全发（4 B / 通道 / tick）
  BIN  —— 每个桶给 min/max/avg/rms（16 B / 通道 / 桶）

块被攒满（或 ABORT 收尾）时才产出一个 payload，交给 device 分片发送。
"""

import math
import struct

from .proto import BF, BLOCK_HDR, FMT, block_header


class Sampler:
    def __init__(self, cfgs, mask: int, rate_hz: int, interval_ms: int,
                 tier: int = 0, bin_ms: int = 0, fmt: int = FMT.RAW16):
        self.cfgs = cfgs
        self.mask = mask
        self.rate = rate_hz
        self.interval_ms = interval_ms
        self.tier = tier
        self.fmt = fmt

        self.bin_ms = bin_ms if bin_ms > 0 else {0: 0, 1: 10, 2: 1000, 3: 60000}.get(tier, 0)
        self._raw_mode = (self.bin_ms == 0)
        if self._raw_mode:
            self.fmt = FMT.RAW16

        self.ticks_per_block = max(1, rate_hz * interval_ms // 1000)
        self.ticks_per_bin = max(1, rate_hz * self.bin_ms // 1000) if not self._raw_mode else 0
        self.bins_per_block = max(1, interval_ms // max(1, self.bin_ms)) if not self._raw_mode else 0

        self.active_mask = 0
        for c in cfgs:
            if (mask >> c.idx) & 1:
                self.active_mask |= 1 << c.idx
        self._order = [c for c in cfgs if (self.active_mask >> c.idx) & 1]

        self.reset()

    # ---------------------------------------------------------------- 状态
    def reset(self):
        self._data = bytearray()
        self._count = 0                     # 已攒 tick 数（RAW）/ 桶数（BIN）
        self._first_idx = 0
        self._first_us = 0
        self._have_first = False
        self.block_seq = 0
        self.saturated = False
        self._bin = None
        self.total_ticks = 0
        self.dropped_ticks = 0

    def start(self, sample_index: int):
        self.reset()
        self._first_idx = sample_index
        self.total_ticks = 0

    # ---------------------------------------------------------------- 喂 tick
    def add_tick(self, sample_index: int, t_us: int, samples, saturated=False):
        """samples: [(cfg, v_raw, i_raw), ...]，顺序与 mask 一致"""
        if not self._have_first:
            self._first_idx = sample_index
            self._first_us = t_us
            self._have_first = True
        self.total_ticks += 1
        if saturated:
            self.saturated = True

        if self._raw_mode:
            for s in samples:
                self._data += struct.pack("<hh", _s16(s[1]), _s16(s[2]))
            self._count += 1
        else:
            self._bin_accumulate(samples)

    def _bin_accumulate(self, samples):
        if self._bin is None:
            self._bin = {}
            for s in samples:
                self._bin[s[0].idx] = [4095, 0, 0, 0, 4095, 0, 0, 0, 0]  # vmin,vmax,vsum,vsumsq,...
        for c, vr, ir, _sat in samples:
            b = self._bin[c.idx]
            b[0] = min(b[0], vr); b[1] = max(b[1], vr)
            b[2] += vr;          b[3] += vr * vr
            b[4] = min(b[4], ir); b[5] = max(b[5], ir)
            b[6] += ir;          b[7] += ir * ir
            b[8] += 1

        n = next(iter(self._bin.values()))[8]
        if n >= self.ticks_per_bin:
            for c in self._order:
                vmin, vmax, vsum, vsq, imin, imax, isum, isq, _n = self._bin[c.idx]
                _n = max(1, _n)
                self._data += struct.pack(
                    "<hhhhhhhh",
                    _s16(vmin), _s16(vmax), _s16(vsum // _n),
                    _s16(int(math.sqrt(vsq / _n))),
                    _s16(imin), _s16(imax), _s16(isum // _n),
                    _s16(int(math.sqrt(isq / _n))))
            self._count += 1
            self._bin = None

    # ---------------------------------------------------------------- 出块
    def ready(self) -> bool:
        target = self.ticks_per_block if self._raw_mode else self.bins_per_block
        return self._count >= target

    def pop_block(self, seq: int, final=False, extra_flags: int = 0):
        """攒够一块（或 final）就产出 payload；否则返回 None"""
        target = self.ticks_per_block if self._raw_mode else self.bins_per_block
        if not final and self._count < target:
            return None
        if self._count == 0 and not final:
            return None

        flags = extra_flags
        if self.saturated:
            flags |= BF.SATURATED
        if final:
            flags |= BF.FINAL
        fmt = FMT.RAW16 if self._raw_mode else FMT.BIN16
        count = self._count

        payload = block_header(self._first_idx, self._first_us // 1000, seq,
                               count, self.active_mask, flags, fmt) + bytes(self._data)

        self._data = bytearray()
        self._count = 0
        self._have_first = False
        self.saturated = False
        self._bin = None
        return payload

    def pending_bytes(self) -> int:
        return len(self._data)


def _s16(v: int) -> int:
    v = int(v)
    if v > 32767:
        return 32767
    if v < -32768:
        return -32768
    return v
