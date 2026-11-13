# -*- coding: utf-8 -*-
"""
信号层：情景输出 -> 噪声 -> ADC 量化。

情景本体在 pdl/scenarios.py，这里只负责"把理想值变成带噪的码值"。
"""

import random

from . import scenarios


class SignalSource:
    def __init__(self, cfg, rng=None):
        self.cfg = cfg
        self.rng = rng or random.Random(1234 + cfg.idx)
        self._t0 = None

    def sample(self, t: float):
        c = self.cfg
        if self._t0 is None:
            self._t0 = t
        tt = max(0.0, t - self._t0)

        v, i = scenarios.evaluate(c.model, tt, c)

        if c.noise_mv > 0:
            v += self.rng.gauss(0, c.noise_mv / 1000.0)
            i += self.rng.gauss(0, c.noise_mv / 2000.0)

        return v, i


class Sampler32:
    """把一组通道在同一 tick 上量化成 raw count"""

    def __init__(self, power_cfgs):
        self.cfgs = power_cfgs
        self.sources = {c.idx: SignalSource(c) for c in power_cfgs}

    def tick(self, t: float, mask: int):
        """返回 [(cfg, v_raw, i_raw, saturated)]，只含 mask 内的通道"""
        out = []
        for c in self.cfgs:
            if not (mask >> c.idx) & 1:
                continue
            v, i = self.sources[c.idx].sample(t)
            vr = c.v_to_raw(v)
            ir = c.i_to_raw(i)
            sat = vr <= 0 or vr >= 4095 or ir <= 0 or ir >= 4095
            out.append((c, vr, ir, sat))
        return out
