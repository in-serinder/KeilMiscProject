#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PWMCTROL 下位机模拟器
=====================

模拟 STC8H1K08 三通道低端 PWM 控制器：
  - 8 位数码管（MAX7219 风格，7 段 + dp）
  - EC11 编码器（旋转 / 单击 / 双击 / 长按）
  - 显示切换开关（电压 / 占空比）
  - 模式拨码（1 本地面板 / 2 预制程序 / 3 上位机）
  - 24C02 预制程序存储（落盘到 sim_store.json）
  - 三通道实时参数 + 三路示波器
  - 串口协议（PROTO v1.1），默认 COM1 @115200

用法：
    python pwm_sim.py --port COM1
    python pwm_sim.py --port none        # 不开串口，只用面板
    python pwm_sim.py --selftest         # 不开 GUI，跑协议自检
"""

import argparse
import json
import math
import os
import random
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk

try:
    import serial
    from serial.tools import list_ports
    HAS_SERIAL = True
except Exception:                                    # pragma: no cover
    HAS_SERIAL = False
    list_ports = None

# ----------------------------------------------------------------------------
# 常量
# ----------------------------------------------------------------------------
FOSC = 22_118_400.0                 # 22.1184MHz
HW_MIN_HZ = FOSC / (16.0 * 65536.0)  # 21.12Hz 硬件引擎下限
STORE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sim_store.json")

C_BG      = "#12151a"
C_PANEL   = "#1b2027"
C_PANEL2  = "#222933"
C_LINE    = "#2e3743"
C_TEXT    = "#d6dde6"
C_DIM     = "#7b8798"
C_ACCENT  = "#3fa9f5"
C_SEG_ON  = "#ff5533"
C_SEG_OFF = "#2b1d1a"
C_OK      = "#4ec97a"
C_WARN    = "#e8b339"
C_ERR     = "#e05555"
C_CH      = ["#3fa9f5", "#4ec97a", "#e8b339"]

MODE_NAMES = {1: "本地面板", 2: "预制程序", 3: "上位机"}

# 7 段字模
DIGIT_SEG = {
    "0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc",
    "5": "afgcd", "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcdfg",
    "-": "g", " ": "",
}
SEG_KEYS = ("a", "b", "c", "d", "e", "f", "g")


# ----------------------------------------------------------------------------
# 硬件量化模型
# ----------------------------------------------------------------------------
def quantize_freq(f):
    """返回 (实际频率, ARR+1, PSC)。f<21.12Hz 走软件引擎，ARR 记为软件周期 tick 数。"""
    f = max(1.0, min(999_000.0, float(f)))
    if f < HW_MIN_HZ:
        ticks = max(1, int(round(1.0 / (f * 100e-6))))
        return f, ticks, 0
    best = None
    for psc in range(1, 17):
        arr1 = int(round(FOSC / (psc * f)))
        if arr1 < 2:
            continue
        if arr1 <= 65536:
            actual = FOSC / (psc * arr1)
            best = (actual, arr1, psc)
            break
    if best is None:
        return HW_MIN_HZ, 65536, 16
    return best


def quantize_duty(permille, arr1):
    """千分比 → 实际生效千分比（受 ARR 分辨率限制）。"""
    if permille <= 0:
        return 0
    if permille >= 1000:
        return 1000
    ccr = int(round(permille * arr1 / 1000.0))
    ccr = max(1, min(arr1, ccr))
    return int(round(ccr * 1000.0 / arr1))


def fmt_freq(f):
    """协议用：2 位小数。"""
    return "%.2f" % f


# ----------------------------------------------------------------------------
# 通道
# ----------------------------------------------------------------------------
class Channel:
    def __init__(self, idx):
        self.idx = idx
        self.req_freq = 1000.0
        self.req_duty = 0.0
        self.freq = 1000.0
        self.duty = 0.0
        self.arr1 = 22118
        self.psc = 1
        self.dur_ms = 0            # 0 = 未配置；-1 = 永久
        self.remain_ms = 0
        self.running = False
        self.state = "S"           # R / S / E / F
        self.adc_sel = 0           # 0 / 1 / ADC编号
        self.preset = None
        self.preset_t = 0.0
        self.base_duty = 50.0
        self.p1 = 0
        self.p2 = 0
        self.fault = False

    def snapshot(self):
        return dict(idx=self.idx, freq=self.freq, duty=self.duty, dur=self.dur_ms,
                    remain=max(0, self.remain_ms), running=self.running,
                    state=self.state, preset=self.preset, adc=self.adc_sel)


# ----------------------------------------------------------------------------
# 设备模型（协议核心，与 GUI 解耦，可单测）
# ----------------------------------------------------------------------------
class Device:
    def __init__(self, log=None):
        self.ch = [Channel(i) for i in range(3)]
        self.mode = 1
        self.echo_period_ms = 50
        self.echo_en = 1
        self.guard_ms = 0
        self.vmeter_mv = 0
        self.raw14 = 0
        self.same_freq = False          # 模拟 PWMA 共用时基
        self.stats = dict(rst="POR", rxovf=0, framerr=0, echoDrop=0, uptime=0.0)
        self.log = log or (lambda *a: None)
        self.presets = {}
        self._last_cmd = time.time()
        self.guard_tripped = False

    # -- 工具 ---------------------------------------------------------------
    def _err(self, code, detail=""):
        return "ERR_%s%s" % (code, ("_" + detail) if detail else "")

    def _nomode(self):
        return self.mode != 3

    # -- 主入口 -------------------------------------------------------------
    def handle(self, body):
        """body 为 `>` 与 `<` 之间的内容，返回应答列表（不含方括号）。"""
        self._last_cmd = time.time()
        self.guard_tripped = False
        body = body.strip()
        if not body:
            return []
        if body == "TYPE-PWMCTROL":
            return ["YES"]
        parts = body.split("_")
        cmd = parts[0]
        try:
            fn = getattr(self, "_cmd_" + cmd.replace("-", "_").replace("?", "Q"), None)
            if fn is None:
                self.stats["framerr"] += 1
                return ["ERR_E07"]
            res = fn(parts[1:])
            if res is None:
                return []
            if isinstance(res, str):
                return [res]
            return res
        except (ValueError, IndexError):
            self.stats["framerr"] += 1
            return ["ERR_E06"]

    # -- 基础命令 -----------------------------------------------------------
    def _cmd_PING(self, a):
        return ["PONG"]

    def _cmd_VER(self, a):
        return ["VER_1.1.0"]

    def _cmd_INFO(self, a):
        return ["INFO_PWMCTROL_3_1.1.0_22118"]

    def _cmd_STAT(self, a):
        s = self.stats
        return ["STAT_%s_%d_%d_%d_%d" % (s["rst"], s["rxovf"], s["framerr"],
                                         s["echoDrop"], int(s["uptime"]))]

    def _cmd_RESET(self, a):
        return ["OK_RESET"]

    # -- 模式 ---------------------------------------------------------------
    def _cmd_MODE(self, a):
        if not a:
            return ["ERR_E06"]
        if a[0] == "?":
            return ["MODE_%d" % self.mode]
        n = int(a[0])
        if n not in (1, 2, 3):
            return self._err("E01")
        self.mode = n
        self.log("模式切换 → Mode%d %s" % (n, MODE_NAMES[n]))
        return ["OK_MODE_%d" % n]

    def _cmd_MODEQ(self, a):
        return ["MODE_%d" % self.mode]

    # -- 配置 ---------------------------------------------------------------
    def _cmd_CONFIG(self, a):
        if len(a) != 5:
            return ["ERR_E06"]
        idx = int(a[0])
        if idx not in (0, 1, 2):
            return ["ERR_E01"]
        ch = self.ch[idx]
        if self._nomode():
            return self._err("E13", "MODE")
        if ch.fault:
            return self._err("E09", "FAULT")

        freq = float(a[1])
        if freq < 1.0 or freq > 999_000.0:
            return ["ERR_E02"]
        duty = float(a[2])
        if duty < 0.0 or duty > 100.0:
            return ["ERR_E03"]

        if a[3] == "F":
            dur = -1
        else:
            dur = int(a[3])
            if dur < 1:
                return ["ERR_E04"]

        adc_field = a[4]
        if adc_field == "0":
            adc_sel = 0
        elif adc_field == "1":
            adc_sel = 1
        elif adc_field.startswith("A"):
            n = int(adc_field[1:])
            if n < 0 or n > 15 or (2 <= n <= 7):
                return self._err("E05", "ADC")
            adc_sel = n
        else:
            return ["ERR_E05"]

        actual, arr1, psc = quantize_freq(freq)
        if arr1 < 8:
            return self._err("E12", "RES")
        permille = int(round(duty * 10))
        permille = quantize_duty(permille, arr1)

        ch.req_freq, ch.req_duty = freq, duty
        ch.arr1, ch.psc = arr1, psc
        self._apply(ch, actual, permille / 10.0)
        ch.dur_ms = dur
        ch.remain_ms = dur if dur > 0 else 0
        ch.adc_sel = adc_sel
        ch.preset = None
        ch.running = True
        ch.state = "R"
        ch.base_duty = ch.duty
        if self.same_freq:
            for o in self.ch:
                if o is not ch and o.running:
                    o.freq = actual
                    o.arr1 = arr1
                    o.duty = quantize_duty(int(round(o.req_duty * 10)), arr1) / 10.0
        self.log("CONFIG ch%d f=%.1fHz d=%.1f%% dur=%s" %
                 (idx, actual, ch.duty, "F" if dur < 0 else "%dms" % dur))
        adc_back = "0" if adc_sel == 0 else ("1" if adc_sel == 1 else "A%d" % adc_sel)
        return ["OK_%d_%s_%.1f_%s" % (idx, fmt_freq(ch.freq), ch.duty, adc_back)]

    def _apply(self, ch, freq, duty):
        ch.freq = freq
        ch.duty = duty
        self._write_ccr(ch)

    def _write_ccr(self, ch):
        # 仅用于示波器/显示，硬件这里是写 PWMA_CCR
        ch.ccr = int(round(ch.duty / 100.0 * ch.arr1)) if ch.arr1 else 0

    # -- 停止 ---------------------------------------------------------------
    def _cmd_STOP(self, a):
        if a and a[0] != "":
            idx = int(a[0])
            if idx not in (0, 1, 2):
                return ["ERR_E01"]
            self._stop(self.ch[idx], clear_fault=True)
            return ["OK_STOP_%d" % idx]
        for c in self.ch:
            self._stop(c, clear_fault=True)
        self.log("STOP 全部通道")
        return ["OK_STOP"]

    def _stop(self, ch, clear_fault=False):
        ch.running = False
        ch.duty = 0.0
        ch.preset = None
        ch.preset_t = 0.0
        ch.remain_ms = 0
        ch.state = "S"
        if clear_fault:
            ch.fault = False
        self._write_ccr(ch)

    # -- 预制程序 -----------------------------------------------------------
    def _cmd_PROG(self, a):
        if len(a) < 2:
            return ["ERR_E06"]
        idx = int(a[0])
        if idx not in (0, 1, 2):
            return ["ERR_E01"]
        n = int(a[1])
        if n < 0 or n > 9:
            return self._err("E08", str(n))
        ch = self.ch[idx]
        if self._nomode():
            return self._err("E13", "MODE")
        if ch.fault:
            return self._err("E09", "FAULT")
        ch.preset = n
        ch.preset_t = 0.0
        if len(a) >= 3:
            ch.p1 = int(a[2])
        if len(a) >= 4:
            ch.p2 = int(a[3])
        ch.base_duty = ch.duty if ch.duty > 0 else 50.0
        ch.running = True
        ch.state = "R"
        ch.remain_ms = ch.dur_ms if ch.dur_ms > 0 else 0
        self.log("PROG ch%d → %d 号程序" % (idx, n))
        return ["OK_PROG_%d_%d" % (idx, n)]

    # -- 参数类 -------------------------------------------------------------
    def _cmd_RATE(self, a):
        if not a:
            return ["ERR_E06"]
        v = int(a[0])
        if v < 10 or v > 1000:
            return self._err("E07", "RATE")
        self.echo_period_ms = v
        return ["OK_RATE_%d" % v]

    def _cmd_ECHO(self, a):
        if not a:
            return ["ERR_E06"]
        v = int(a[0])
        if v not in (0, 1, 2):
            return ["ERR_E07"]
        self.echo_en = v
        return ["OK_ECHO_%d" % v]

    def _cmd_GUARD(self, a):
        if not a:
            return ["ERR_E06"]
        v = int(a[0])
        self.guard_ms = v
        return ["OK_GUARD_%d" % v]

    def _cmd_GET(self, a):
        if not a:
            return ["ERR_E06"]
        idx = int(a[0])
        if idx not in (0, 1, 2):
            return ["ERR_E01"]
        return [self.echo_body(self.ch[idx])]

    def _cmd_ADCREAD(self, a):
        idx = int(a[0]) if a else 0
        if idx not in (0, 1, 2):
            return ["ERR_E01"]
        return ["ADC_%d_%d_%d" % (idx, self.raw14, self.vmeter_mv)]

    def _cmd_SAVE(self, a):
        return ["OK_SAVE"]

    def _cmd_LOAD(self, a):
        return ["OK_LOAD"]

    # -- ECHO ---------------------------------------------------------------
    def echo_body(self, ch):
        adc = str(self.vmeter_mv) if ch.adc_sel else "null"
        return "ECHO_%d_%s_%.1f_%s_%s" % (ch.idx, fmt_freq(ch.freq), ch.duty, adc, ch.state)

    # -- 面板操作（Mode1/2 本地改参数） -------------------------------------
    def panel_set_freq(self, idx, freq):
        ch = self.ch[idx]
        actual, arr1, psc = quantize_freq(freq)
        if arr1 < 8:
            return
        ch.req_freq = actual
        ch.arr1, ch.psc = arr1, psc
        ch.freq = actual
        permille = quantize_duty(int(round(ch.req_duty * 10)), arr1)
        ch.duty = permille / 10.0
        self._write_ccr(ch)

    def panel_set_duty(self, idx, duty):
        ch = self.ch[idx]
        duty = max(0.0, min(100.0, duty))
        ch.req_duty = duty
        ch.base_duty = duty
        ch.duty = quantize_duty(int(round(duty * 10)), ch.arr1) / 10.0
        self._write_ccr(ch)

    def panel_toggle_run(self, idx):
        ch = self.ch[idx]
        if ch.running:
            self._stop(ch)
        else:
            ch.running = True
            ch.state = "R"
            if ch.duty <= 0:
                ch.duty = ch.base_duty
            ch.remain_ms = ch.dur_ms if ch.dur_ms > 0 else 0

    # -- 时间推进 -----------------------------------------------------------
    def tick(self, dt):
        self.stats["uptime"] += dt
        for ch in self.ch:
            if not ch.running:
                continue
            if ch.dur_ms > 0:
                ch.remain_ms -= int(dt * 1000)
                if ch.remain_ms <= 0:
                    self._stop(ch)
                    ch.state = "E"
                    continue
            if ch.preset is not None:
                ch.preset_t += dt
                d = self.preset_duty(ch)
                ch.duty = quantize_duty(int(round(d * 10)), ch.arr1) / 10.0
                self._write_ccr(ch)
        if self.guard_ms > 0 and (time.time() - self._last_cmd) * 1000 > self.guard_ms:
            if not self.guard_tripped:
                self.guard_tripped = True
                for c in self.ch:
                    self._stop(c)
                    c.state = "E"
                self.log("看护超时 → 全部停机")

    def preset_duty(self, ch):
        n, t = ch.preset, ch.preset_t
        base = ch.base_duty if ch.base_duty > 0 else 50.0
        if n == 0:
            return base
        if n == 1:
            ph = (t % 3.0) / 3.0
            return 100.0 * (ph * 2 if ph < 0.5 else (1 - ph) * 2)
        if n == 2:
            return base * min(1.0, t / 1.0)
        if n == 3:
            return base * max(0.0, 1.0 - t / 1.0)
        if n == 4:
            return 50.0 if (t * 2.0) % 1.0 < 0.5 else 0.0
        if n == 5:
            return base if (t % 10.0) < 5.0 else 0.0
        if n == 6:
            tt = t - ch.idx * 0.5
            return base * max(0.0, min(1.0, tt))
        if n == 7:
            ph = (t % 3.0) / 3.0
            v = ph * 2 if ph < 0.5 else (1 - ph) * 2
            return 100.0 * v
        if n == 8:
            target = ch.p1 or 2500
            err = (target - self.vmeter_mv) / max(1.0, target)
            nd = ch.duty + err * 12.0
            return max(0.0, min(100.0, nd))
        if n == 9:
            hi = ch.p1 or 2000
            hy = ch.p2 or 100
            if self.vmeter_mv < hi:
                return 100.0
            if self.vmeter_mv > hi + hy:
                return 0.0
            return ch.duty
        return base


# ----------------------------------------------------------------------------
# 帧解析
# ----------------------------------------------------------------------------
class FrameParser:
    MAXLEN = 64

    def __init__(self, on_frame):
        self.on_frame = on_frame
        self.buf = bytearray()
        self.active = False
        self.bad = 0

    def feed(self, data):
        for b in data:
            if b == ord(">"):
                self.active = True
                self.buf = bytearray()
            elif b == ord("<"):
                if self.active:
                    self.active = False
                    try:
                        body = self.buf.decode("ascii", "ignore")
                    except Exception:
                        body = ""
                    if body:
                        self.on_frame(body)
                self.buf = bytearray()
            elif self.active:
                if len(self.buf) >= self.MAXLEN:
                    self.active = False
                    self.buf = bytearray()
                    self.bad += 1
                else:
                    self.buf.append(b)


# ----------------------------------------------------------------------------
# 串口链路
# ----------------------------------------------------------------------------
class Link:
    def __init__(self, port, baud, on_bytes, on_state):
        self.port = port
        self.baud = baud
        self.on_bytes = on_bytes
        self.on_state = on_state
        self.ser = None
        self.alive = False
        self.lock = threading.Lock()

    def start(self):
        if not HAS_SERIAL:
            self.on_state(False, "未安装 pyserial")
            return
        if not self.port or self.port.lower() in ("none", "-"):
            self.on_state(False, "未启用串口")
            return
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.05,
                                     write_timeout=0.3)
            self.alive = True
            self.on_state(True, "%s @%d" % (self.port, self.baud))
        except Exception as e:
            self.on_state(False, "打开失败: %s" % e)
            return
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while self.alive and self.ser:
            try:
                data = self.ser.read(256)
                if data:
                    self.on_bytes(data)
            except Exception:
                self.alive = False
                self.on_state(False, "读取中断")
                break

    def write(self, text):
        if not self.alive or not self.ser:
            return False
        try:
            with self.lock:
                self.ser.write(text.encode("ascii", "ignore"))
            return True
        except Exception:
            self.alive = False
            self.on_state(False, "写入失败")
            return False

    def stop(self):
        self.alive = False
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass
        self.ser = None


# ----------------------------------------------------------------------------
# 数码管控件（7 段）
# ----------------------------------------------------------------------------
class SevenSegPanel(tk.Canvas):
    DIGIT_W = 46
    STEP = 52
    H = 88

    def __init__(self, master, digits=8):
        w = digits * self.STEP - (self.STEP - self.DIGIT_W) + 24
        super().__init__(master, width=w, height=self.H, bg="#0d1014",
                         highlightthickness=0)
        self.n = digits
        self.items = []
        self._build()

    def _build(self):
        self.create_rectangle(4, 4, int(self["width"]) - 4, self.H - 4,
                              outline="#2a3240", width=1)
        for i in range(self.n):
            x0 = 12 + i * self.STEP
            y0 = 12
            segs = {}
            coords = {
                "a": (x0 + 7, y0 + 6, x0 + 33, y0 + 6),
                "f": (x0 + 5, y0 + 10, x0 + 5, y0 + 30),
                "b": (x0 + 35, y0 + 10, x0 + 35, y0 + 30),
                "g": (x0 + 7, y0 + 34, x0 + 33, y0 + 34),
                "e": (x0 + 5, y0 + 38, x0 + 5, y0 + 58),
                "c": (x0 + 35, y0 + 38, x0 + 35, y0 + 58),
                "d": (x0 + 7, y0 + 62, x0 + 33, y0 + 62),
            }
            for k, c in coords.items():
                segs[k] = self.create_line(*c, width=5, capstyle="round",
                                           fill=C_SEG_OFF)
            dp = self.create_oval(x0 + 39, y0 + 58, x0 + 45, y0 + 64,
                                  fill=C_SEG_OFF, outline="")
            self.items.append((segs, dp))

    def set_digit(self, i, char, dp=False, level=1.0):
        segs, dp_item = self.items[i]
        on = DIGIT_SEG.get(char, "")
        on_col = self._scale(C_SEG_ON, level)
        for k in SEG_KEYS:
            self.itemconfig(segs[k], fill=on_col if k in on else C_SEG_OFF)
        self.itemconfig(dp_item, fill=on_col if dp else C_SEG_OFF)

    @staticmethod
    def _scale(hexcol, level):
        level = max(0.25, min(1.0, level))
        r = int(hexcol[1:3], 16)
        g = int(hexcol[3:5], 16)
        b = int(hexcol[5:7], 16)
        return "#%02x%02x%02x" % (int(r * level), int(g * level), int(b * level))

    def set_all(self, chars, dps, level=1.0):
        for i in range(self.n):
            self.set_digit(i, chars[i] if i < len(chars) else " ",
                           dps[i] if i < len(dps) else False, level)


# ----------------------------------------------------------------------------
# EC11 旋钮控件
# ----------------------------------------------------------------------------
class Knob(tk.Canvas):
    def __init__(self, master, size=116, on_step=None, on_click=None,
                 on_double=None, on_long=None):
        super().__init__(master, width=size, height=size, bg=C_PANEL,
                         highlightthickness=0)
        self.size = size
        self.on_step = on_step or (lambda d: None)
        self.on_click = on_click or (lambda: None)
        self.on_double = on_double or (lambda: None)
        self.on_long = on_long or (lambda: None)

        self.angle = 0.0
        self.drag_start = None
        self.dragged = False
        self.press_t = 0.0
        self.click_timer = None
        self.long_timer = None
        self.pending_click = False
        self.acc = 0.0

        c = size / 2
        self.create_oval(4, 4, size - 4, size - 4, fill="#252c36",
                         outline="#39424f", width=2)
        for k in range(24):
            a = math.radians(k * 15)
            r1, r2 = c - 3, c - 9
            self.create_line(c + r1 * math.sin(a), c - r1 * math.cos(a),
                             c + r2 * math.sin(a), c - r2 * math.cos(a),
                             fill="#46505e", width=1)
        self.create_oval(16, 16, size - 16, size - 16, fill="#1d232b",
                         outline="#4a5563", width=1)
        self.ptr = self.create_line(c, c, c, 26, fill=C_ACCENT, width=4,
                                    capstyle="round")
        self.hub = self.create_oval(c - 6, c - 6, c + 6, c + 6,
                                    fill="#39424f", outline="")

        self.bind("<ButtonPress-1>", self._press)
        self.bind("<B1-Motion>", self._motion)
        self.bind("<ButtonRelease-1>", self._release)
        self.bind("<MouseWheel>", self._wheel)

    def _press(self, e):
        self.drag_start = e.y
        self.dragged = False
        self.acc = 0.0
        self.press_t = time.time()
        if self.long_timer:
            self.after_cancel(self.long_timer)
        self.long_timer = self.after(1200, self._fire_long)

    def _motion(self, e):
        if self.drag_start is None:
            return
        dy = self.drag_start - e.y
        if abs(dy) > 4:
            self.dragged = True
        self.acc += dy
        self.drag_start = e.y
        while self.acc >= 7:
            self.acc -= 7
            self._step(+1)
        while self.acc <= -7:
            self.acc += 7
            self._step(-1)

    def _release(self, e):
        dur = time.time() - self.press_t
        if self.long_timer:
            self.after_cancel(self.long_timer)
            self.long_timer = None
        if self.dragged:
            self.drag_start = None
            return
        self.drag_start = None
        if dur >= 1.2:
            return                                   # 长按已在计时器里触发
        if self.pending_click:
            self.pending_click = False
            if self.click_timer:
                self.after_cancel(self.click_timer)
                self.click_timer = None
            self.on_double()
        else:
            self.pending_click = True
            self.click_timer = self.after(350, self._fire_single)

    def _fire_single(self):
        if self.pending_click:
            self.pending_click = False
            self.click_timer = None
            self.on_click()

    def _fire_long(self):
        self.long_timer = None
        self.pending_click = False
        if self.click_timer:
            self.after_cancel(self.click_timer)
            self.click_timer = None
        self.on_long()

    def _wheel(self, e):
        self._step(1 if e.delta > 0 else -1)

    def _step(self, d):
        self.angle = (self.angle + d * 12) % 360
        c = self.size / 2
        a = math.radians(self.angle)
        self.coords(self.ptr, c, c, c + (c - 26) * math.sin(a),
                    c - (c - 26) * math.cos(a))
        self.on_step(d)


# ----------------------------------------------------------------------------
# 示波器（时基 / 触发 / 单周期放大）
# ----------------------------------------------------------------------------
TB_STEPS = [1.0, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001,
            0.0005, 0.0002, 0.0001, 0.00005, 0.00002, 0.00001,
            0.000005, 0.000002, 0.000001, 0.0000005, 0.0000002, 0.0000001]


def fmt_time(s):
    if s >= 0.9995:
        return "%.4g s" % s
    if s >= 0.9995e-3:
        return "%.4g ms" % (s * 1e3)
    if s >= 0.9995e-6:
        return "%.4g µs" % (s * 1e6)
    return "%.4g ns" % (s * 1e9)


def auto_timebase(f):
    """让 10 格刚好装下约 3 个周期。"""
    if not f or f <= 0:
        return 0.001
    want = 3.0 / (f * 10.0)
    best = TB_STEPS[0]
    for tb in TB_STEPS:
        if abs(math.log(tb / want)) < abs(math.log(best / want)):
            best = tb
    return best


class ScopePanel(tk.Frame):
    """三通道主视图：10 格时基 + 触发对齐。"""

    def __init__(self, master, height=240):
        super().__init__(master, bg=C_PANEL)
        self.h = height
        self.canvas = tk.Canvas(self, height=height, bg="#0d1014",
                                highlightthickness=0, bd=0)
        self.canvas.pack(fill="both", expand=True)
        self._last = None
        self.canvas.bind("<Configure>", self._on_conf)

    def _on_conf(self, _e):
        if self._last:
            self.redraw(*self._last)

    def redraw(self, chans, tb, trig, up=True, paused=False):
        self._last = (chans, tb, trig, up, paused)
        cv = self.canvas
        cv.delete("all")
        w = cv.winfo_width()
        h = cv.winfo_height()
        if w < 80 or h < 60 or not chans:
            return

        for i in range(11):
            x = i * w / 10.0
            cv.create_line(x, 0, x, h, fill="#161e27")
        for i in range(1, 10):
            y = i * h / 10.0
            cv.create_line(0, y, w, y, fill="#141b23")

        lane_h = h / 3.0
        tspan = 10.0 * tb
        tri = chans[trig]
        off = 0.0
        if tri["freq"] > 0:
            per = 1.0 / tri["freq"]
            off = 0.0 if up else tri["duty"] / 100.0 * per
        t0 = off - 0.2 * tspan
        step_t = tspan / float(w)

        def level(c, t):
            f = c["freq"]
            if f <= 0:
                return 0
            return 1 if ((t * f) % 1.0) * 100.0 < c["duty"] else 0

        for lane in range(3):
            c = chans[lane]
            col = C_CH[lane]
            y_top = lane * lane_h + 22
            y_base = (lane + 1) * lane_h - 12
            cv.create_line(0, y_base, w, y_base, fill="#243040")
            cv.create_line(0, y_top, w, y_top, fill="#1b2430")
            cv.create_text(6, lane * lane_h + 6, anchor="nw", text="CH%d" % lane,
                           fill=col, font=("Consolas", 8, "bold"))
            cv.create_text(40, lane * lane_h + 6, anchor="nw",
                           text="%s / %.1f%%" % (_fq(c["freq"]), c["duty"]),
                           fill=C_DIM, font=("Consolas", 8))
            if not c["running"] or c["duty"] <= 0:
                cv.create_line(0, y_base, w, y_base, fill=col, width=1)
                continue
            pts = []
            last = None
            for x in range(0, w + 1, 2):
                y = y_top if level(c, t0 + x * step_t) else y_base
                if last is None:
                    pts.extend([x, y])
                elif y != last:
                    pts.extend([x, last, x, y])
                last = y
            if pts:
                pts.extend([w, last if last is not None else y_base])
                cv.create_line(*pts, fill=col, width=2)

        xt = 0.2 * w
        cv.create_polygon(xt - 5, 0, xt + 5, 0, xt, 9, fill="#e8b339", outline="")
        cv.create_text(w - 6, h - 4, anchor="se",
                       text="%s/div · 触发 CH%d%s · %s"
                            % (fmt_time(tb), trig, "↑" if up else "↓",
                               "暂停" if paused else "运行"),
                       fill=C_DIM, font=("Consolas", 8))


class PeriodView(tk.Canvas):
    """单周期放大：一个周期内的形状 + T / Ton 标注。"""

    def __init__(self, master, width=330, height=240):
        super().__init__(master, width=width, height=height, bg="#0d1014",
                         highlightthickness=0, bd=0)
        self._last = None
        self.bind("<Configure>", self._on_conf)

    def _on_conf(self, _e):
        if self._last:
            self.redraw(*self._last)

    def redraw(self, c, idx):
        self._last = (c, idx)
        self.delete("all")
        w = self.winfo_width()
        h = self.winfo_height()
        if w < 120 or h < 100:
            return
        col = C_CH[idx]
        self.create_text(10, 10, anchor="nw", text="CH%d 单周期" % idx,
                         fill=C_TEXT, font=("Microsoft YaHei UI", 9, "bold"))
        if not c["running"] or c["duty"] <= 0 or c["freq"] <= 0:
            self.create_text(w / 2, h / 2, anchor="center", text="无输出",
                             fill=C_DIM, font=("Microsoft YaHei UI", 10))
            return

        f = c["freq"]
        per = 1.0 / f
        duty = c["duty"]
        ton = per * duty / 100.0

        x0, x1 = 54.0, w - 28.0
        y_hi, y_lo = 74.0, h - 58.0
        hi_w = (x1 - x0) * duty / 100.0
        xm = x0 + hi_w

        self.create_line(x0, y_hi + 6, x0, y_lo + 6, fill="#2b3644", dash=(2, 3))
        self.create_line(xm, y_hi - 10, xm, y_lo + 6, fill="#2b3644", dash=(2, 3))
        self.create_line(x1, y_hi + 6, x1, y_lo + 6, fill="#2b3644", dash=(2, 3))
        self.create_line(x0 - 8, y_lo, x1 + 8, y_lo, fill="#243040")

        self.create_line(x0, y_lo, x0, y_hi, xm, y_hi, xm, y_lo, x1, y_lo,
                         fill=col, width=2)

        self.create_line(x0, y_hi - 16, xm, y_hi - 16, fill=C_WARN, width=1)
        self.create_line(x0, y_hi - 20, x0, y_hi - 12, fill=C_WARN, width=1)
        self.create_line(xm, y_hi - 20, xm, y_hi - 12, fill=C_WARN, width=1)
        self.create_text((x0 + xm) / 2, y_hi - 26, anchor="s",
                         text="Ton " + fmt_time(ton), fill=C_WARN,
                         font=("Consolas", 8))

        self.create_line(x0, y_lo + 16, x1, y_lo + 16, fill=C_ACCENT, width=1)
        self.create_line(x0, y_lo + 12, x0, y_lo + 20, fill=C_ACCENT, width=1)
        self.create_line(x1, y_lo + 12, x1, y_lo + 20, fill=C_ACCENT, width=1)
        self.create_text((x0 + x1) / 2, y_lo + 22, anchor="n",
                         text="T " + fmt_time(per), fill=C_ACCENT,
                         font=("Consolas", 8))

        self.create_text(10, 30, anchor="nw",
                         text="f  %s" % _fq(f), fill=C_DIM,
                         font=("Consolas", 8))
        self.create_text(w - 10, 30, anchor="ne",
                         text="duty  %.1f%%" % duty, fill=C_DIM,
                         font=("Consolas", 8))
        self.create_text(10, h - 18, anchor="nw",
                         text="Toff %s" % fmt_time(per - ton), fill=C_DIM,
                         font=("Consolas", 8))


def _fq(f):
    return ("%.2f kHz" % (f / 1000.0)) if f >= 1000 else ("%.0f Hz" % f)


# ----------------------------------------------------------------------------
# 主界面
# ----------------------------------------------------------------------------
class App:
    def __init__(self, root, args):
        self.root = root
        self.args = args
        self.dev = Device(log=self.log_dev)
        self.parser = FrameParser(self.on_frame)
        self.link = None
        self.edit = None                 # None / "F" / "D"
        self.cur_ch = 0
        self.show_voltage = False
        self.blink_on = True
        self.blink_t = 0.0
        self.ch_hint_until = 0.0
        self.echo_next = 0.0
        self.echo_idx = 0
        self.phase = 0.0
        self.paused = False
        self.last_t = time.time()
        self.tx_count = 0
        self.rx_count = 0
        self._build()
        self.load_store()
        self.open_link()
        self.root.after(30, self.tick)

    # -- UI ----------------------------------------------------------------
    def _build(self):
        r = self.root
        r.title("PWMCTROL 下位机模拟器 — STC8H1K08 三通道低端 PWM")
        r.geometry("1480x920")
        r.configure(bg=C_BG)
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except Exception:
            pass
        style.configure("TLabel", background=C_BG, foreground=C_TEXT, font=("Microsoft YaHei UI", 9))
        style.configure("TButton", font=("Microsoft YaHei UI", 9))
        style.configure("TCheckbutton", background=C_BG, foreground=C_TEXT)
        style.configure("TRadiobutton", background=C_PANEL, foreground=C_TEXT)
        style.configure("TCombobox", fieldbackground=C_PANEL2, background=C_PANEL2)

        # 顶栏
        top = tk.Frame(r, bg=C_PANEL, height=46)
        top.pack(side="top", fill="x")
        tk.Label(top, text="PWMCTROL 模拟器", bg=C_PANEL, fg=C_TEXT,
                 font=("Microsoft YaHei UI", 11, "bold")).pack(side="left", padx=12, pady=10)

        tk.Label(top, text="端口", bg=C_PANEL, fg=C_DIM).pack(side="left", padx=(16, 4))
        self.port_var = tk.StringVar(value=self.args.port)
        ports = [self.args.port]
        if HAS_SERIAL and list_ports:
            for p in list_ports.comports():
                ports.append(p.device)
        ports = list(dict.fromkeys(ports))
        self.cb_port = ttk.Combobox(top, textvariable=self.port_var, values=ports,
                                    width=10, state="normal")
        self.cb_port.pack(side="left")
        ttk.Button(top, text="连接", command=self.open_link).pack(side="left", padx=6)
        self.lbl_link = tk.Label(top, text="● 未连接", bg=C_PANEL, fg=C_WARN,
                                 font=("Microsoft YaHei UI", 9))
        self.lbl_link.pack(side="left", padx=8)
        self.lbl_stat = tk.Label(top, text="RX 0 / TX 0", bg=C_PANEL, fg=C_DIM,
                                 font=("Consolas", 9))
        self.lbl_stat.pack(side="right", padx=12)

        # 主体
        body = tk.Frame(r, bg=C_BG)
        body.pack(side="top", fill="both", expand=True, padx=10, pady=8)
        left = tk.Frame(body, bg=C_BG)
        left.pack(side="left", fill="y")
        right = tk.Frame(body, bg=C_BG)
        right.pack(side="left", fill="both", expand=True, padx=(10, 0))

        self._build_left(left)
        self._build_right(right)

    def _build_left(self, p):
        card = tk.Frame(p, bg=C_PANEL, padx=10, pady=10)
        card.pack(fill="x")

        self.seg = SevenSegPanel(card, 8)
        self.seg.pack(pady=(0, 8))

        row = tk.Frame(card, bg=C_PANEL)
        row.pack(fill="x", pady=2)
        tk.Label(row, text="模式拨码", bg=C_PANEL, fg=C_DIM,
                 font=("Microsoft YaHei UI", 9)).pack(side="left")
        self.mode_var = tk.IntVar(value=self.dev.mode)
        for m in (1, 2, 3):
            tk.Radiobutton(row, text=str(m), variable=self.mode_var, value=m,
                           command=self.on_mode, bg=C_PANEL, fg=C_TEXT,
                           selectcolor=C_PANEL2, activebackground=C_PANEL,
                           font=("Consolas", 10, "bold")).pack(side="left", padx=2)
        self.lbl_mode = tk.Label(row, text=MODE_NAMES[1], bg=C_PANEL, fg=C_ACCENT,
                                 font=("Microsoft YaHei UI", 9))
        self.lbl_mode.pack(side="left", padx=8)

        tool = tk.Frame(card, bg=C_PANEL)
        tool.pack(fill="x", pady=8)
        self.knob = Knob(tool, 116, on_step=self.on_knob, on_click=self.on_knob_click,
                         on_double=self.on_knob_double, on_long=self.on_knob_long)
        self.knob.pack(side="left")
        side = tk.Frame(tool, bg=C_PANEL)
        side.pack(side="left", padx=10, fill="y")
        tk.Label(side, text="EC11 旋钮", bg=C_PANEL, fg=C_TEXT,
                 font=("Microsoft YaHei UI", 9, "bold")).pack(anchor="w")
        tk.Label(side, text="拖拽/滚轮 = 旋转\n单击 = 切频率/占空比\n双击 = 切通道\n长按 = 启停当前通道",
                 bg=C_PANEL, fg=C_DIM, justify="left",
                 font=("Microsoft YaHei UI", 8)).pack(anchor="w", pady=(2, 6))
        self.btn_switch = tk.Button(side, text="显示：占空比", command=self.on_switch,
                                    bg=C_PANEL2, fg=C_TEXT, relief="flat",
                                    activebackground=C_LINE,
                                    font=("Microsoft YaHei UI", 9), width=14)
        self.btn_switch.pack(anchor="w")
        self.lbl_edit = tk.Label(side, text="编辑焦点：无", bg=C_PANEL, fg=C_WARN,
                                 font=("Microsoft YaHei UI", 8))
        self.lbl_edit.pack(anchor="w", pady=(6, 0))

        card2 = tk.Frame(p, bg=C_PANEL, padx=10, pady=8)
        card2.pack(fill="x", pady=(8, 0))
        tk.Label(card2, text="模拟电压探头（ADC 引出脚）", bg=C_PANEL, fg=C_DIM,
                 font=("Microsoft YaHei UI", 9)).pack(anchor="w")
        self.volt_var = tk.DoubleVar(value=0.0)
        sc = tk.Scale(card2, from_=0, to=5000, orient="horizontal",
                      variable=self.volt_var, command=self.on_volt,
                      bg=C_PANEL, fg=C_TEXT, highlightthickness=0, troughcolor=C_PANEL2,
                      length=300, showvalue=False)
        sc.pack(fill="x")
        self.lbl_volt = tk.Label(card2, text="0 mV", bg=C_PANEL, fg=C_TEXT,
                                 font=("Consolas", 10))
        self.lbl_volt.pack(anchor="e")

        card3 = tk.Frame(p, bg=C_PANEL, padx=10, pady=8)
        card3.pack(fill="x", pady=(8, 0))
        row3 = tk.Frame(card3, bg=C_PANEL)
        row3.pack(fill="x")
        tk.Label(row3, text="亮度", bg=C_PANEL, fg=C_DIM).pack(side="left")
        self.bright = tk.DoubleVar(value=0.9)
        tk.Scale(row3, from_=0.3, to=1.0, resolution=0.05, orient="horizontal",
                 variable=self.bright, bg=C_PANEL, fg=C_TEXT, highlightthickness=0,
                 troughcolor=C_PANEL2, length=150, showvalue=False).pack(side="left", padx=6)
        self.same_var = tk.IntVar(value=0)
        ttk.Checkbutton(card3, text="三通道同频（模拟 PWMA 共用时基）",
                        variable=self.same_var,
                        command=self.on_same).pack(anchor="w", pady=(6, 0))

        help_card = tk.Frame(p, bg=C_PANEL, padx=10, pady=8)
        help_card.pack(fill="both", expand=True, pady=(8, 0))
        tk.Label(help_card, text="数码管速查", bg=C_PANEL, fg=C_TEXT,
                 font=("Microsoft YaHei UI", 9, "bold")).pack(anchor="w")
        txt = ("D1-D3  频率   无小数点 = Hz\n"
               "       1.50 = 1.5kHz   100. = 100kHz\n"
               "D4     Mode   1 本地 / 2 程序 / 3 上位机\n"
               "D5-D7  电压 X.XXV 或 占空比 0-100\n"
               "       占空比右对齐，首位消隐（50 → \" 50\"）\n"
               "D8     程序号，dp 亮 = 通道号\n"
               "\n硬件约束\n"
               "三路 PWM 同属 PWMA，只能同频；\n"
               "P1.0 兼作 MAX7219 LOAD，PWM1P 须关闭。")
        tk.Label(help_card, text=txt, bg=C_PANEL, fg=C_DIM, justify="left",
                 font=("Consolas", 8)).pack(anchor="w", pady=(4, 0))

    def _build_right(self, p):
        cards = tk.Frame(p, bg=C_BG)
        cards.pack(fill="x")
        self.ch_labels = []
        for i in range(3):
            f = tk.Frame(cards, bg=C_PANEL, padx=8, pady=8)
            f.pack(side="left", fill="both", expand=True, padx=(0 if i == 0 else 8, 0))
            tk.Frame(f, bg=C_CH[i], height=3).pack(fill="x")
            tk.Label(f, text="CH%d" % i, bg=C_PANEL, fg=C_CH[i],
                     font=("Consolas", 11, "bold")).pack(anchor="w", pady=(4, 2))
            lab = {}
            for key, text in (("freq", "频率"), ("duty", "占空比"),
                              ("dur", "剩余"), ("state", "状态"), ("adc", "电压")):
                row = tk.Frame(f, bg=C_PANEL)
                row.pack(fill="x")
                tk.Label(row, text=text, bg=C_PANEL, fg=C_DIM, width=4, anchor="w",
                         font=("Microsoft YaHei UI", 8)).pack(side="left")
                v = tk.Label(row, text="—", bg=C_PANEL, fg=C_TEXT,
                             font=("Consolas", 9), anchor="w")
                v.pack(side="left")
                lab[key] = v
            btns = tk.Frame(f, bg=C_PANEL)
            btns.pack(fill="x", pady=(4, 0))
            tk.Button(btns, text="启动", bg=C_PANEL2, fg=C_TEXT, relief="flat",
                      font=("Microsoft YaHei UI", 8),
                      command=lambda i=i: self.dev.panel_toggle_run(i)).pack(side="left")
            tk.Button(btns, text="停止", bg=C_PANEL2, fg=C_TEXT, relief="flat",
                      font=("Microsoft YaHei UI", 8),
                      command=lambda i=i: self.dev._stop(self.dev.ch[i])).pack(side="left", padx=4)
            self.ch_labels.append(lab)

        log_card = tk.Frame(p, bg=C_PANEL, padx=8, pady=6)
        log_card.pack(side="bottom", fill="x", pady=(8, 0))
        tk.Label(log_card, text="串口 / 事件日志", bg=C_PANEL, fg=C_TEXT,
                 font=("Microsoft YaHei UI", 9, "bold")).pack(anchor="w")
        self.logtxt = tk.Text(log_card, height=7, bg="#0d1014", fg=C_TEXT,
                              insertbackground=C_TEXT, relief="flat",
                              font=("Consolas", 9), wrap="none")
        self.logtxt.pack(fill="x", pady=(4, 0))

        sc_card = tk.Frame(p, bg=C_PANEL, padx=8, pady=6)
        sc_card.pack(fill="both", expand=True, pady=(8, 0))
        head = tk.Frame(sc_card, bg=C_PANEL)
        head.pack(fill="x")
        tk.Label(head, text="示波器", bg=C_PANEL, fg=C_TEXT,
                 font=("Microsoft YaHei UI", 9, "bold")).pack(side="left")
        self.lbl_tb = tk.Label(head, text="", bg=C_PANEL, fg=C_DIM,
                               font=("Consolas", 8))
        self.lbl_tb.pack(side="right")

        bar = tk.Frame(sc_card, bg=C_PANEL)
        bar.pack(fill="x", pady=(5, 4))

        tk.Label(bar, text="时基", bg=C_PANEL, fg=C_DIM,
                 font=("Microsoft YaHei UI", 8)).pack(side="left")
        self.tb_map = {"自动": None}
        for t in TB_STEPS:
            self.tb_map[fmt_time(t)] = t
        self.tb_var = tk.StringVar(value="自动")
        ttk.Combobox(bar, textvariable=self.tb_var, values=list(self.tb_map),
                     width=7, state="readonly").pack(side="left", padx=(4, 10))

        tk.Label(bar, text="触发", bg=C_PANEL, fg=C_DIM,
                 font=("Microsoft YaHei UI", 8)).pack(side="left")
        self.trig_var = tk.StringVar(value="CH0")
        ttk.Combobox(bar, textvariable=self.trig_var, values=["CH0", "CH1", "CH2"],
                     width=5, state="readonly").pack(side="left", padx=(4, 8))

        self.slope_up = tk.BooleanVar(value=True)
        self.btn_slope = tk.Button(bar, text="↑ 上升沿", bg=C_PANEL2, fg=C_TEXT,
                                   relief="flat", font=("Microsoft YaHei UI", 8),
                                   command=self.on_slope)
        self.btn_slope.pack(side="left", padx=(0, 8))

        self.btn_pause = tk.Button(bar, text="暂停", bg=C_PANEL2, fg=C_TEXT,
                                   relief="flat", font=("Microsoft YaHei UI", 8),
                                   command=self.on_pause)
        self.btn_pause.pack(side="left")

        tk.Label(bar, text="单周期视图跟随触发通道", bg=C_PANEL, fg=C_DIM,
                 font=("Microsoft YaHei UI", 8)).pack(side="right")

        body = tk.Frame(sc_card, bg=C_PANEL)
        body.pack(fill="both", expand=True)
        self.scope = ScopePanel(body, 240)
        self.scope.pack(side="left", fill="both", expand=True)
        self.period = PeriodView(body, 340, 240)
        self.period.pack(side="right", fill="y", padx=(8, 0))

    # -- 串口 --------------------------------------------------------------
    def open_link(self):
        if self.link:
            self.link.stop()
        self.parser = FrameParser(self.on_frame)
        self.link = Link(self.port_var.get(), 115200, self.parser.feed, self.on_link_state)
        self.link.start()

    def on_link_state(self, ok, msg):
        def upd():
            self.lbl_link.config(text=("● " if ok else "○ ") + msg,
                                 fg=C_OK if ok else C_WARN)
        try:
            self.root.after(0, upd)
        except Exception:
            pass

    def send(self, text):
        frame = "[" + text + "]"
        if self.link and self.link.write(frame):
            self.tx_count += 1
            self.append_log("<< " + frame)

    def on_frame(self, body):
        replies = self.dev.handle(body)
        self.rx_count += 1
        self.append_log(">> " + ">" + body + "<")
        for rep in replies:
            self.send(rep)

    def append_log(self, text):
        ts = time.strftime("%H:%M:%S")
        line = "%s %s\n" % (ts, text)
        def upd():
            self.logtxt.insert("end", line)
            if int(self.logtxt.index("end-1c").split(".")[0]) > 400:
                self.logtxt.delete("1.0", "80.0")
            self.logtxt.see("end")
        try:
            self.root.after(0, upd)
        except Exception:
            pass

    def log_dev(self, msg):
        self.append_log("-- " + msg)

    # -- 面板事件 ----------------------------------------------------------
    def on_mode(self):
        self.dev.mode = self.mode_var.get()
        self.lbl_mode.config(text=MODE_NAMES[self.dev.mode])
        self.send("EVT_MODE_%d" % self.dev.mode)
        self.append_log("-- 拨码 → Mode%d" % self.dev.mode)

    def on_same(self):
        self.dev.same_freq = bool(self.same_var.get())

    def on_volt(self, _=None):
        mv = int(self.volt_var.get())
        self.dev.vmeter_mv = mv
        self.dev.raw14 = int(mv * 1023 / 5000) if mv else 0
        self.lbl_volt.config(text="%d mV" % mv)

    def on_switch(self):
        self.show_voltage = not self.show_voltage
        self.btn_switch.config(text="显示：电压" if self.show_voltage else "显示：占空比")

    def on_slope(self):
        self.slope_up.set(not self.slope_up.get())
        self.btn_slope.config(text="↑ 上升沿" if self.slope_up.get() else "↓ 下降沿")

    def on_pause(self):
        self.paused = not self.paused
        self.btn_pause.config(text="继续" if self.paused else "暂停")

    def on_knob(self, d):
        dev, m = self.dev, self.dev.mode
        if dev.mode == 3:
            self.ch_hint_until = time.time() + 2.0
            return
        if m == 1:
            ch = dev.ch[self.cur_ch]
            if self.edit == "F":
                step = self._freq_step(ch.req_freq)
                dev.panel_set_freq(self.cur_ch, ch.req_freq + d * step)
            elif self.edit == "D":
                step = 1.0
                dev.panel_set_duty(self.cur_ch, ch.req_duty + d * step)
            else:
                dev.panel_set_duty(self.cur_ch, ch.req_duty + d * 1.0)
        elif m == 2:
            ch = dev.ch[self.cur_ch]
            n = (ch.preset if ch.preset is not None else 0) + d
            n = max(0, min(9, n))
            ch.preset = n
            ch.preset_t = 0.0
            ch.base_duty = ch.duty if ch.duty > 0 else 50.0
            ch.running = True
            ch.state = "R"

    @staticmethod
    def _freq_step(f):
        if f < 100:
            return 1.0
        if f < 1000:
            return 10.0
        if f < 10000:
            return 100.0
        if f < 100000:
            return 1000.0
        return 10000.0

    def on_knob_click(self):
        if self.dev.mode == 2:
            self.dev.panel_toggle_run(self.cur_ch)
            return
        if self.dev.mode == 3:
            self.on_switch()
            return
        self.edit = {"F": "D", "D": None, None: "F"}[self.edit]

    def on_knob_double(self):
        self.cur_ch = (self.cur_ch + 1) % 3
        self.ch_hint_until = time.time() + 2.0

    def on_knob_long(self):
        if self.dev.mode in (1, 2):
            self.dev.panel_toggle_run(self.cur_ch)

    # -- 显示刷新 ----------------------------------------------------------
    def build_display(self):
        dev = self.dev
        ch = dev.ch[self.cur_ch]
        now = time.time()
        blink = self.blink_on

        # 频率 3 位
        fchars, dot = self._freq_chars(ch.freq if ch.running else ch.req_freq)
        chars = list(fchars)
        dps = [False] * 8
        if dot is not None:
            dps[dot] = True
        # 编辑闪烁
        if self.edit == "F" and not blink:
            chars = [" "] * 3

        chars.append(str(dev.mode) if dev.mode in (1, 2, 3) else "-")
        dps[3] = True

        # D5-D7
        if self.show_voltage:
            mv = dev.vmeter_mv
            v10 = int(round(mv / 10.0))
            s = "%03d" % min(999, v10)
            pchars = list(s)
            pdps = [False, True, False]
            if v10 >= 1000:
                pchars = ["9", "9", "9"]
        else:
            d = ch.duty if ch.running else ch.req_duty
            pchars = self._duty_chars(d)
            pdps = [False, False, False]
        if self.edit == "D" and not blink:
            pchars = [" "] * 3

        chars += pchars
        dps += pdps

        # D8：程序号 / 通道号
        if now < self.ch_hint_until:
            chars.append(str(self.cur_ch))
            dps.append(True)
        else:
            p = ch.preset
            chars.append(str(p) if p is not None else "-")
            dps.append(False)

        return chars[:8], dps[:8]

    @staticmethod
    def _duty_chars(d):
        """占空比 3 位显示：右对齐，首位消隐。50 → " 50"，5 → "  5"，100 → "100"。"""
        p = int(round(max(0.0, min(100.0, float(d)))))
        if p >= 100:
            return ["1", "0", "0"]
        return list("%3d" % p)

    @staticmethod
    def _freq_chars(f):
        """返回 (3 个字符, 小数点位置索引或 None)。"""
        f = max(1.0, float(f))
        if f < 999.5:
            return "%3d" % int(round(f)), None
        k = f / 1000.0
        if k < 9.995:
            s = "%.2f" % k                     # 1.50
            return s.replace(".", ""), 0
        if k < 99.95:
            s = "%.1f" % k                     # 20.0
            return s.replace(".", ""), 1
        return "%3d" % int(round(k)), 2        # 100.

    def refresh_cards(self):
        for i, c in enumerate(self.dev.ch):
            lab = self.ch_labels[i]
            lab["freq"].config(text=("%.2f kHz" % (c.freq / 1000.0)) if c.freq >= 1000
                               else ("%.0f Hz" % c.freq))
            lab["duty"].config(text="%.1f %%" % c.duty)
            lab["dur"].config(text="F 永久" if c.dur_ms < 0 else
                              ("%d ms" % max(0, c.remain_ms) if c.dur_ms > 0 else "—"))
            st = {"R": ("运行", C_OK), "S": ("停止", C_DIM),
                  "E": ("到期", C_WARN), "F": ("故障", C_ERR)}.get(c.state, ("—", C_DIM))
            lab["state"].config(text=st[0], fg=st[1])
            lab["adc"].config(text=("%d mV" % self.dev.vmeter_mv) if c.adc_sel else "null")

    # -- 主循环 ------------------------------------------------------------
    def tick(self):
        now = time.time()
        dt = min(0.1, now - self.last_t)
        self.last_t = now
        self.phase += dt

        self.dev.tick(dt)

        self.blink_t += dt
        if self.blink_t >= 0.25:
            self.blink_t = 0.0
            self.blink_on = not self.blink_on

        # ECHO 节拍：一个周期内错开 3 帧
        if self.dev.echo_en and now >= self.echo_next:
            slot = self.dev.echo_period_ms / 3.0 / 1000.0
            self.echo_next = now + slot
            ch = self.dev.ch[self.echo_idx]
            self.echo_idx = (self.echo_idx + 1) % 3
            if ch.running or ch.state != "S":
                self.send(self.dev.echo_body(ch))

        chars, dps = self.build_display()
        self.seg.set_all(chars, dps, self.bright.get())
        self.refresh_cards()

        self.lbl_edit.config(text="编辑焦点：" + {"F": "频率", "D": "占空比",
                                                 None: "无（旋转调占空比）"}[self.edit])
        self.lbl_stat.config(text="RX %d / TX %d" % (self.rx_count, self.tx_count))
        snaps = [c.snapshot() for c in self.dev.ch]
        trig = int(self.trig_var.get()[2])
        tb = self.tb_map.get(self.tb_var.get())
        if tb is None:
            tb = auto_timebase(snaps[trig]["freq"])
        self.lbl_tb.config(text="%s/div" % fmt_time(tb))
        if not self.paused:
            self.scope.redraw(snaps, tb, trig, self.slope_up.get(), self.paused)
            self.period.redraw(snaps[trig], trig)
        self.root.after(30, self.tick)

    # -- 存档 --------------------------------------------------------------
    def load_store(self):
        try:
            with open(STORE_FILE, "r", encoding="utf-8") as f:
                d = json.load(f)
            self.dev.mode = int(d.get("mode", 1))
            self.mode_var.set(self.dev.mode)
            self.lbl_mode.config(text=MODE_NAMES[self.dev.mode])
            for i, c in enumerate(d.get("channels", [])):
                if i < 3:
                    ch = self.dev.ch[i]
                    ch.req_freq = ch.freq = float(c.get("freq", 1000))
                    ch.req_duty = ch.base_duty = float(c.get("duty", 0))
                    ch.dur_ms = int(c.get("dur", 0))
            self.dev.echo_period_ms = int(d.get("rate", 50))
            self.dev.same_freq = bool(d.get("same_freq", False))
            self.same_var.set(1 if self.dev.same_freq else 0)
        except Exception:
            pass

    def save_store(self):
        d = dict(mode=self.dev.mode, rate=self.dev.echo_period_ms,
                 same_freq=self.dev.same_freq,
                 channels=[dict(freq=c.req_freq, duty=c.req_duty, dur=c.dur_ms)
                           for c in self.dev.ch])
        try:
            with open(STORE_FILE, "w", encoding="utf-8") as f:
                json.dump(d, f, ensure_ascii=False, indent=2)
        except Exception:
            pass


# ----------------------------------------------------------------------------
# 自检（不开 GUI）
# ----------------------------------------------------------------------------
def selftest():
    d = Device(log=lambda m: None)
    cases = [
        ("TYPE-PWMCTROL", None), ("PING", None), ("VER", None), ("INFO", None),
        ("MODE_3", None),
        ("CONFIG_0_1000_50.0_5000_1", None),
        ("CONFIG_1_5_75_F_0", None),
        ("CONFIG_2_20000_12.5_F_A12", None),
        ("CONFIG_3_1000_50_5000_0", None),
        ("CONFIG_0_1000_150_5000_0", None),
        ("CONFIG_0_1200000_50_F_0", None),
        ("FOO", None),
        ("GET_0", None), ("GET_1", None), ("GET_2", None),
        ("ADCREAD_1", None),
        ("RATE_50", None), ("ECHO_1", None), ("GUARD_0", None),
        ("MODQ_?", None),
        ("PROG_0_1", None), ("PROG_1_2_2500_0", None),
        ("STOP_1", None), ("STOP", None),
        ("MODE_1", None), ("CONFIG_0_1000_50_F_0", None),
    ]
    print("=" * 62)
    print("PWMCTROL 模拟器协议自检")
    print("=" * 62)
    for body, _ in cases:
        if body == "MODQ_?":
            body = "MODE_?"
        out = d.handle(body)
        print("%-32s → %s" % (">" + body + "<", " ".join("[" + o + "]" for o in out)))
    print("-" * 62)
    print("量化检查：")
    for f in (1, 5, 21.12, 50, 1000, 20000, 500000, 999000):
        a, arr1, psc = quantize_freq(f)
        print("  目标 %8.0f Hz → 实际 %10.2f Hz  ARR+1=%-6d PSC=%-2d 偏差 %+.3f%%"
              % (f, a, arr1, psc, (a - f) / f * 100))
    print("-" * 62)
    print("显示格式检查：")
    for f in (1, 50, 999, 1500, 20000, 100000, 999000):
        chars, dot = App._freq_chars(f)
        s = list(chars)
        if dot is not None:
            s.insert(dot + 1, ".")
        print("  %8.0f Hz → %-6s  (小数点位置=%s)" % (f, "".join(s), dot))
    print("占空比显示检查：")
    for d in (0, 5, 9.9, 50, 50.5, 99, 100):
        print("  %6.1f %% → [%s]" % (d, "".join(App._duty_chars(d))))
    print("=" * 62)


# ----------------------------------------------------------------------------
def enable_dpi_awareness():
    """让 Tk 拿到真实像素，避免 125%/150% 缩放下字体发虚。"""
    try:
        from ctypes import windll
        windll.shcore.SetProcessDpiAwareness(1)
        return
    except Exception:
        pass
    try:
        from ctypes import windll
        windll.user32.SetProcessDPIAware()
    except Exception:
        pass


def main():
    ap = argparse.ArgumentParser(description="PWMCTROL 下位机模拟器")
    ap.add_argument("--port", default="COM1", help="串口，默认 COM1；none 表示不开串口")
    ap.add_argument("--selftest", action="store_true", help="只跑协议自检")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return

    enable_dpi_awareness()
    root = tk.Tk()
    app = App(root, args)
    try:
        root.mainloop()
    finally:
        app.save_store()
        if app.link:
            app.link.stop()


if __name__ == "__main__":
    main()
