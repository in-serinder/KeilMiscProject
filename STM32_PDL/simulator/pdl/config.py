# -*- coding: utf-8 -*-
"""
通道配置与单位换算。

系数全部是「每 count 多少 mV / mA」，用有理数 num/den 表示 ——
因为真机 F103 没有 FPU，固件里也是整数运算，模拟器必须和它一致。
"""

from dataclasses import dataclass, field

# ---------------------------------------------------------------- 硬件事实
VREF_MV = 3300
ADC_FULL = 4096          # 12 bit 满量程计数

# 四路分压比（见 docs/01 第 3.2 节）
#   CH0  12 V 档  4.3 : 1   FS 14.19 V   3.46 mV/count
#   CH1  24 V 档  8.5 : 1   FS 28.05 V   6.85 mV/count
#   CH2  64 V 档 23   : 1   FS 75.9  V  18.53 mV/count
#   CH3 100 V 档 31   : 1   FS 102.3 V  24.98 mV/count
_POWER_DEFAULT = [
    # CH0 电机运动：母线 12 V，额定 2 A，6 s 一个循环
    dict(label="CH0", ratio=4.3,  v_fs_dv=142,  v_num=346,  v_den=100,
         fs_ma=5000,  i_num=2441, i_den=1000,
         model="motor", v_set=12.0, i_set=2.0, period_s=6.0, r_int=0.08),
    # CH1 充电 CC-CV：18 V 起充到 24 V，CC 段 8 s
    dict(label="CH1", ratio=8.5,  v_fs_dv=281,  v_num=685,  v_den=100,
         fs_ma=5000,  i_num=2441, i_den=1000,
         model="charge", v_set=24.0, i_set=1.5, v_min=18.0, tau_s=8.0),
    # CH2 电池放电：48 V 放到 42 V，全程 20 s
    dict(label="CH2", ratio=23.0, v_fs_dv=759,  v_num=1853, v_den=100,
         fs_ma=5000,  i_num=2441, i_den=1000,
         model="discharge", v_set=48.0, i_set=0.8, v_min=42.0, tau_s=20.0, r_int=0.1),
    # CH3 开关电源：96 V，80 mV 纹波，20 s 时负载翻倍
    dict(label="CH3", ratio=31.0, v_fs_dv=1023, v_num=2498, v_den=100,
         fs_ma=20000, i_num=9766, i_den=1000,
         model="buck", v_set=96.0, i_set=3.0, ripple_mv=80.0,
         step_at=20.0, step_ratio=2.0),
]


@dataclass
class PowerCfg:
    idx: int
    label: str = "CH"
    enabled: bool = True
    sensor_type: int = 0          # 0 = 分压 + Hall
    # 电压换算
    v_fs_dv: int = 1023           # 满量程 x0.1 V
    v_num: int = 2498
    v_den: int = 100
    v_off: int = 0
    # 电流换算
    fs_current_ma: int = 5000
    i_num: int = 2441
    i_den: int = 1000
    i_off: int = 2048             # 运放把 0 A 搬到中点
    # 信号模型（模拟器专用，不发给上位机）
    model: str = "dc"             # 见 pdl/scenarios.py 的 SPECS
    v_set: float = 12.0           # 标称电压 / 满电电压 / 母线电压
    i_set: float = 2.0            # 额定电流 / 恒流值
    v_min: float = 0.0            # 放电终止电压、充电起始电压
    ripple_mv: float = 0.0        # 纹波幅度
    noise_mv: float = 1.5         # 高斯噪声
    period_s: float = 6.0         # 运动周期 / 扫频周期
    tau_s: float = 8.0            # 充电 CC 时长 / 放电全程时长
    cap_mf: float = 10.0          # 电容容量（mF）
    r_int: float = 0.05           # 源内阻（Ω），电流大时母线跌落
    step_at: float = -1.0         # 阶跃时刻（秒），<0 不阶跃
    step_ratio: float = 1.0       # 阶跃倍数
    phase: float = 0.0

    # ---- 换算 ----
    def v_to_raw(self, volts: float) -> int:
        mv = volts * 1000.0
        raw = round(mv * self.v_den / self.v_num) + self.v_off
        return max(0, min(ADC_FULL - 1, raw))

    def i_to_raw(self, amps: float) -> int:
        ma = amps * 1000.0
        raw = round(ma * self.i_den / self.i_num) + self.i_off
        return max(0, min(ADC_FULL - 1, raw))

    def raw_to_mv(self, raw: int) -> int:
        return int((raw - self.v_off) * self.v_num / self.v_den)

    def raw_to_ma(self, raw: int) -> int:
        return int((raw - self.i_off) * self.i_num / self.i_den)

    def full_scale_mv(self) -> int:
        return int(ADC_FULL * self.v_num / self.v_den)

    def lsb_uv(self) -> float:
        return self.v_num * 1000.0 / self.v_den


@dataclass
class TempCfg:
    idx: int
    label: str
    enabled: bool = True
    sensor_type: int = 1          # 1 = DS18B20
    res_bits: int = 12
    c16_offset: int = 0
    # 模拟器专用
    temp_c: float = 25.0          # 当前温度（由热模型驱动）
    present: bool = True
    ambient_c: float = 25.0       # 环境温度
    r_th: float = 3.0             # 热阻 ℃/W：功率每 1 W 抬高多少度
    tau_s: float = 30.0           # 热时间常数（秒），越大升得越慢
    coupling: bool = True         # 是否跟随功率发热
    bound_mask: int = 0           # 哪几路功率给它加热（0 = 本通道）


def default_power() -> list:
    out = []
    for i, d in enumerate(_POWER_DEFAULT):
        kw = {k: v for k, v in d.items() if k in PowerCfg.__dataclass_fields__}
        out.append(PowerCfg(idx=i, phase=i * 0.7, **kw))
    return out


def default_temp() -> list:
    th0 = TempCfg(idx=0, label="TH0", temp_c=25.0, ambient_c=25.0,
                  r_th=1.0, tau_s=20.0, bound_mask=0b0001)
    th1 = TempCfg(idx=1, label="TH1", temp_c=26.0, ambient_c=25.0,
                  r_th=0.8, tau_s=35.0, bound_mask=0b0010)
    return [th0, th1]


# DS18B20 转换时间（毫秒），决定 temp_interval_ms 的下限
TEMP_CONV_MS = {9: 100, 10: 200, 11: 400, 12: 800}


def temp_min_interval_ms(res_bits: int) -> int:
    return TEMP_CONV_MS.get(res_bits, 800)
