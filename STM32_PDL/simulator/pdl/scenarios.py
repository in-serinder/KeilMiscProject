# -*- coding: utf-8 -*-
"""
情景库 —— 让每个通道吐出"像真事"的电压电流。

每个情景就是 f(t, cfg) -> (volts, amps)，t 是秒。
参数取自通道配置（v_set / i_set / v_min / period_s / tau_s / cap_mf / r_int），
所以不用给每个情景单独做一套 UI。

新增情景只要往 SPECS 里加一条，信号层和界面会自动认。
"""

import math


# ---------------------------------------------------------------- 情景实现
def _dc(t, c):
    """恒定输出：基线，用来核对换算与量程"""
    return c.v_set, c.i_set


def _motor(t, c):
    """电机运动曲线：加速→匀速→减速→停歇，电流跟转矩走，母线被拉低

    用占空式的速度梯形：0-20% 加速、20-50% 匀速、50-70% 减速（回馈，电流反向）、
    70-100% 停歇。母线电压按 内阻 x 电流 跌落。
    """
    period = max(c.period_s, 0.2)
    ph = (t % period) / period
    if ph < 0.20:
        i = c.i_set * 1.8          # 启动冲击
    elif ph < 0.50:
        i = c.i_set * 0.9          # 匀速
    elif ph < 0.70:
        i = -c.i_set * 0.6         # 减速回馈
    else:
        i = c.i_set * 0.04         # 停歇
    v = c.v_set - i * c.r_int
    return v, i


def _charge(t, c):
    """充电曲线：CC 恒流到 v_set，然后 CV 恒压、电流指数衰减

    tau_s 在这里表示 CC 段持续时间（秒），默认 6 s。
    """
    vmin = c.v_min if c.v_min > 0 else c.v_set * 0.72
    t_cc = max(c.tau_s, 1.0)
    if t <= t_cc:
        v = vmin + (c.v_set - vmin) * (t / t_cc)
        i = c.i_set
    else:
        v = c.v_set
        i = c.i_set * math.exp(-(t - t_cc) / max(t_cc * 0.45, 0.5))
    return v, i


def _discharge(t, c):
    """放电曲线：前段平缓、后段陡降（电池典型膝点），电流按负载恒定"""
    td = max(c.tau_s, 1.0)
    k = min(1.0, t / td)
    v = c.v_set - (c.v_set - c.v_min) * (k ** 1.7)
    i = c.i_set
    v -= i * c.r_int
    return v, i


def _cap_charge(t, c):
    """电容充电：V = Vs(1-e^-t/RC)，电流 = (Vs-V)/R，初始冲击大、随后衰减"""
    r = max(c.v_set / max(c.i_set, 1e-6), 0.01)
    cap_f = max(c.cap_mf, 0.001) / 1000.0
    tau = max(r * cap_f, 1e-4)
    v = c.v_set * (1.0 - math.exp(-t / tau))
    i = (c.v_set - v) / r
    return v, i


def _cap_discharge(t, c):
    """电容放电：V = V0 e^-t/RC，电流随电压一起衰减"""
    r = max(c.v_set / max(c.i_set, 1e-6), 0.01)
    cap_f = max(c.cap_mf, 0.001) / 1000.0
    tau = max(r * cap_f, 1e-4)
    v = c.v_set * math.exp(-t / tau)
    i = v / r
    return v, i


def _buck(t, c):
    """开关电源输出：直流 + 高频纹波；可选在 step_at 处负载跳变"""
    ripple = c.ripple_mv / 1000.0 * math.sin(2 * math.pi * 40000 * t + c.phase)
    v = c.v_set + ripple
    i = c.i_set
    if c.step_at >= 0 and t >= c.step_at:
        dt = t - c.step_at
        v -= 0.08 * c.v_set * math.exp(-dt * 30.0)
        i *= 1.0 + (c.step_ratio - 1.0) * (1.0 - math.exp(-dt * 200.0))
    return v, i


def _step_load(t, c):
    """假负载阶跃：电流在某时刻突变，用来测负载调整率与热响应"""
    i = c.i_set
    if c.step_at >= 0 and t >= c.step_at:
        i *= c.step_ratio
    return c.v_set - i * c.r_int, i


def _sweep(t, c):
    """电压慢扫：做全量程线性度检查"""
    period = max(c.period_s, 1.0)
    v = c.v_set * (0.5 + 0.5 * math.sin(2 * math.pi * t / period))
    return v, c.i_set


# ---------------------------------------------------------------- 注册表
class Spec:
    __slots__ = ("key", "name", "desc", "func")

    def __init__(self, key, name, desc, func):
        self.key, self.name, self.desc, self.func = key, name, desc, func


SPECS = {s.key: s for s in [
    Spec("motor",       "电机运动",   "加速冲击→匀速→减速回馈→停歇", _motor),
    Spec("charge",      "充电 CC-CV", "恒流升压，到点转恒压、电流指数衰减", _charge),
    Spec("discharge",   "电池放电",   "前段平缓后段膝点下降", _discharge),
    Spec("cap_charge",  "电容充电",   "e 指数上升，初始电流冲击", _cap_charge),
    Spec("cap_dis",     "电容放电",   "e 指数衰减，电流随压降", _cap_discharge),
    Spec("buck",        "开关纹波",   "直流 + 40 kHz 纹波 + 可选负载跳变", _buck),
    Spec("step_load",   "假负载阶跃", "电流在某时刻突变", _step_load),
    Spec("sweep",       "电压扫频",   "正弦慢扫，查线性度", _sweep),
    Spec("dc",          "恒定输出",   "基线，核对换算", _dc),
]}

ORDER = ["motor", "charge", "discharge", "cap_charge", "cap_dis",
         "buck", "step_load", "sweep", "dc"]

NAME_TO_KEY = {s.name: s.key for s in SPECS.values()}
KEY_TO_NAME = {s.key: s.name for s in SPECS.values()}


def evaluate(key: str, t: float, cfg):
    spec = SPECS.get(key) or SPECS["dc"]
    return spec.func(t, cfg)
