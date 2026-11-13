# -*- coding: utf-8 -*-
"""
触发引擎：电压 / 电流 / 功率 三选一，开/关阈值分离 + 电平保持消抖。

与真机固件一致的地方（很重要）：
  · 判定用 tick 累加，不用 delay，不阻塞采样
  · 回落立即清零保持计数 —— 这就是消抖，尖峰永远打不响
  · 开阈值 > 关阈值时天然形成回差，避免在阈值附近反复抖
"""

from .proto import EV

SRC_V, SRC_I, SRC_P = 0, 1, 2


class TriggerCfg:
    def __init__(self):
        self.source = SRC_V
        self.ch = 0
        self.start_en = True
        self.stop_en = True
        self.autorearm = False
        self.stop_by_duration = True
        self.start_val = 12000        # mV / mA / mW
        self.stop_val = 8000
        self.start_hold_ms = 50
        self.stop_hold_ms = 50


class TriggerEngine:
    WAIT_START = 0
    WAIT_STOP = 1
    DONE = 2

    def __init__(self):
        self.cfg = TriggerCfg()
        self.phase = self.WAIT_START
        self._hold_us = 0
        self._last_us = 0
        self.trig_count = 0

    def arm(self, cfg: TriggerCfg, now_us: int):
        self.cfg = cfg
        self.phase = self.WAIT_START
        self._hold_us = 0
        self._last_us = now_us
        self.trig_count = 0

    def disarm(self):
        self.phase = self.DONE
        self._hold_us = 0

    def _value(self, v_mv, i_ma, p_mw):
        s = self.cfg.source
        return v_mv if s == SRC_V else (i_ma if s == SRC_I else p_mw)

    def update(self, now_us: int, v_mv: int, i_ma: int, p_mw: int):
        """返回 [(event_code, param)]"""
        out = []
        if self.phase == self.DONE:
            return out

        dt = max(0, now_us - self._last_us)
        self._last_us = now_us
        val = self._value(v_mv, i_ma, p_mw)
        c = self.cfg

        if self.phase == self.WAIT_START:
            if val >= c.start_val:
                self._hold_us += dt
                if self._hold_us >= c.start_hold_ms * 1000:
                    self._hold_us = 0
                    self.trig_count += 1
                    self.phase = self.WAIT_STOP if c.stop_en else self.DONE
                    out.append((EV.TRIG_STARTED, val & 0xFFFF))
            else:
                self._hold_us = 0          # 关键：回落清零

        elif self.phase == self.WAIT_STOP:
            if val <= c.stop_val:
                self._hold_us += dt
                if self._hold_us >= c.stop_hold_ms * 1000:
                    self._hold_us = 0
                    out.append((EV.TRIG_STOPPED, val & 0xFFFF))
                    self.phase = self.WAIT_START if c.autorearm else self.DONE
            else:
                self._hold_us = 0

        return out
