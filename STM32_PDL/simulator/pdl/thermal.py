# -*- coding: utf-8 -*-
"""
热模型：功率 -> 温度。

一阶热等效：
    T_target = T_ambient + P x R_th
    T <- T + (T_target - T) x (1 - e^(-dt/tau))

物理含义：
  · R_th（℃/W）决定"稳态能升到多少度"——功率翻倍，温升翻倍
  · tau（秒）决定"多久升上去"——越大越像一大块铝壳，越小越像裸露的 MOS

为什么要做这个：放电和假负载场景下，功率一变温度跟着变，这才是真实设备的行为。
上位机的温度曲线和功率曲线能不能对得上，直接靠它验证。
"""

import math


class ThermalModel:
    __slots__ = ("t", "ambient", "r_th", "tau", "enabled")

    def __init__(self, t0: float = 25.0, ambient: float = 25.0,
                 r_th: float = 3.0, tau: float = 30.0, enabled: bool = True):
        self.t = t0
        self.ambient = ambient
        self.r_th = max(0.0, r_th)
        self.tau = max(0.5, tau)
        self.enabled = enabled

    def update(self, dt: float, power_w: float):
        if not self.enabled or dt <= 0:
            return
        target = self.ambient + max(0.0, power_w) * self.r_th
        alpha = 1.0 - math.exp(-dt / self.tau)
        self.t += (target - self.t) * alpha
        # 限幅，别让参数填错时飞到离谱的值
        if self.t < -40.0:
            self.t = -40.0
        elif self.t > 150.0:
            self.t = 150.0

    def steady_state(self, power_w: float) -> float:
        return self.ambient + max(0.0, power_w) * self.r_th
