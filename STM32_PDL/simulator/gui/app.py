# -*- coding: utf-8 -*-
"""
模拟器面板 —— 视觉参考 51_PWMCtrol/sim：深色底、左侧控制列、右侧通道卡、底部日志。

它只做两件事：
  1. 让你手动制造场景（改电压/电流/纹波/负载跳变、让探头掉线）
  2. 让你看见模拟器此刻在干什么（状态、tick、块数、丢帧、协议日志）
"""

import tkinter as tk
from collections import deque
from tkinter import ttk

from pdl import proto as P
from pdl.config import ADC_FULL

BG = "#16181D"
PANEL = "#1E2127"
CARD = "#23272F"
BORDER = "#2E333D"
TEXT = "#E6E8EC"
SUB = "#8B93A1"
RED = "#E24B4A"
GREEN = "#43D17A"
AMBER = "#FFB020"

CH_COLORS = ["#4A9EFF", "#43D17A", "#FFB020", "#B07CFF"]
TH_COLORS = ["#FF8A65", "#FFD54F"]
MONO = ("Consolas", 10)
MONO_BIG = ("Consolas", 15, "bold")
UI = ("Microsoft YaHei UI", 9)

STATE_TEXT = {
    P.ST_.BOOT: ("BOOT", SUB),
    P.ST_.IDLE: ("空闲", SUB),
    P.ST_.ARMED: ("待触发", AMBER),
    P.ST_.RUNNING: ("采集中", GREEN),
    P.ST_.DRAINING: ("收尾中", AMBER),
    P.ST_.ERROR: ("故障", RED),
}

from pdl.scenarios import KEY_TO_NAME, NAME_TO_KEY, ORDER

MODEL_NAMES = [KEY_TO_NAME[k] for k in ORDER]
MODELS = ORDER


def fmt_si(v, unit):
    a = abs(v)
    if a >= 1000:
        return "%.2f k%s" % (v / 1000.0, unit)
    if a >= 1:
        return "%.3f %s" % (v, unit)
    if a >= 0.001:
        return "%.1f m%s" % (v * 1000, unit)
    return "%.0f µ%s" % (v * 1e6, unit)


class SimApp(tk.Tk):
    def __init__(self, dev, link, runner):
        super().__init__()
        self.dev, self.link, self.runner = dev, link, runner
        self.title("PDL 下位机模拟器 · 4 功率 + 2 温度")
        self.geometry("1260x820")
        self.minsize(1120, 740)
        self.configure(bg=BG)

        self.hist = {i: deque(maxlen=900) for i in range(4)}
        self._editors = []
        self._build_style()
        self._build_ui()

        self.protocol("WM_DELETE_WINDOW", self._on_close)
        import threading
        threading.Thread(target=self.runner.run_forever, name="pdl-run", daemon=True).start()
        self.after(50, self._refresh)

    # ---------------------------------------------------------------- 样式
    def _build_style(self):
        st = ttk.Style(self)
        try:
            st.theme_use("clam")
        except tk.TclError:
            pass
        st.configure("Dark.TEntry", fieldbackground=CARD, background=CARD,
                     foreground=TEXT, insertcolor=TEXT, bordercolor=BORDER,
                     lightcolor=BORDER, darkcolor=BORDER)
        st.configure("Dark.TCombobox", fieldbackground=CARD, background=CARD,
                     foreground=TEXT, arrowcolor=SUB, bordercolor=BORDER,
                     lightcolor=BORDER, darkcolor=BORDER)
        st.map("Dark.TCombobox", fieldbackground=[("readonly", CARD)],
               foreground=[("readonly", TEXT)])
        self.option_add("*TCombobox*Listbox.background", CARD)
        self.option_add("*TCombobox*Listbox.foreground", TEXT)

    # ---------------------------------------------------------------- 界面
    def _build_ui(self):
        self._topbar().pack(fill="x", padx=10, pady=(10, 6))

        # 状态栏先占位，否则 body 的 expand 会把它挤成 0 高（踩过）
        self.status = tk.Label(self, text="", bg=PANEL, fg=SUB, font=UI,
                               anchor="w", padx=10, pady=4)
        self.status.pack(fill="x", side="bottom", padx=10, pady=(0, 8))

        body = tk.Frame(self, bg=BG)
        body.pack(fill="both", expand=True, padx=10, pady=0)

        left = tk.Frame(body, bg=PANEL, width=264)
        left.pack(side="left", fill="y", padx=(0, 8))
        left.pack_propagate(False)
        self._left_panel(left)

        right = tk.Frame(body, bg=BG)
        right.pack(side="left", fill="both", expand=True)
        self._cards(right)

    # ---------------------------------------------------------------- 顶栏
    def _topbar(self):
        bar = tk.Frame(self, bg=PANEL, height=46)
        bar.pack_propagate(False)

        tk.Label(bar, text="PDL", bg=PANEL, fg=TEXT,
                 font=("Consolas", 13, "bold")).pack(side="left", padx=(12, 8))
        tk.Label(bar, text="功率数据记录仪 · 下位机模拟器", bg=PANEL, fg=SUB,
                 font=UI).pack(side="left")

        self.lbl_link = tk.Label(bar, text="● 未连接", bg=PANEL, fg=SUB, font=UI)
        self.lbl_link.pack(side="right", padx=12)

        self.lbl_state = tk.Label(bar, text="空闲", bg=PANEL, fg=SUB,
                                  font=("Microsoft YaHei UI", 10, "bold"))
        self.lbl_state.pack(side="right", padx=8)

        tk.Label(bar, text="127.0.0.1:5555", bg=PANEL, fg=SUB,
                 font=MONO).pack(side="right", padx=10)
        return bar

    # ---------------------------------------------------------------- 左栏
    def _left_panel(self, root):
        def section(title):
            f = tk.Frame(root, bg=PANEL)
            f.pack(fill="x", padx=12, pady=(12, 0))
            tk.Label(f, text=title, bg=PANEL, fg=SUB, font=UI,
                     anchor="w").pack(fill="x")
            line = tk.Frame(f, bg=BORDER, height=1)
            line.pack(fill="x", pady=(3, 6))
            return f

        # 采集参数（由上位机下发，这里只读显示）
        f = section("采集参数（上位机下发）")
        self.lbl_param = tk.Label(f, text="—", bg=PANEL, fg=TEXT, font=MONO,
                                  anchor="w", justify="left")
        self.lbl_param.pack(fill="x")

        # 流量
        f = section("链路")
        self.lbl_traffic = tk.Label(f, text="RX 0 帧   TX 0 帧", bg=PANEL,
                                    fg=TEXT, font=MONO, anchor="w")
        self.lbl_traffic.pack(fill="x")
        self.lbl_queue = tk.Label(f, text="缓冲 0 块", bg=PANEL, fg=SUB,
                                  font=MONO, anchor="w")
        self.lbl_queue.pack(fill="x")

        # 温度与热模型（功率 -> 温度）
        f = section("温度 · 热模型")
        self._temp_widgets = []
        for t in self.dev.temp:
            row = tk.Frame(f, bg=PANEL)
            row.pack(fill="x", pady=(2, 0))
            tk.Label(row, text=t.label, bg=PANEL, fg=TH_COLORS[t.idx],
                     font=MONO, width=4, anchor="w").pack(side="left")
            live = tk.Label(row, text="-- ℃", bg=PANEL, fg=TEXT, font=MONO,
                            width=8, anchor="w")
            live.pack(side="left")
            heat = tk.BooleanVar(value=t.coupling)
            tk.Checkbutton(row, text="发热", variable=heat, bg=PANEL, fg=SUB,
                           selectcolor=CARD, font=UI, bd=0,
                           activebackground=PANEL, activeforeground=TEXT,
                           command=lambda tt=t, vv=heat: self._apply_coupling(tt, vv)
                           ).pack(side="right")

            row2 = tk.Frame(f, bg=PANEL)
            row2.pack(fill="x", pady=(0, 3))
            tk.Label(row2, text="  R_th", bg=PANEL, fg=SUB,
                     font=("Microsoft YaHei UI", 8)).pack(side="left")
            rv = tk.StringVar(value="%.1f" % t.r_th)
            e1 = ttk.Entry(row2, textvariable=rv, width=5, style="Dark.TEntry",
                           font=MONO, justify="right")
            e1.pack(side="left", padx=2)
            e1.bind("<Return>", lambda _e, tt=t, v=rv: self._apply_thermal(tt, v, None))
            tk.Label(row2, text="℃/W  τ", bg=PANEL, fg=SUB,
                     font=("Microsoft YaHei UI", 8)).pack(side="left")
            tv = tk.StringVar(value="%.0f" % t.tau_s)
            e2 = ttk.Entry(row2, textvariable=tv, width=4, style="Dark.TEntry",
                           font=MONO, justify="right")
            e2.pack(side="left", padx=2)
            e2.bind("<Return>", lambda _e, tt=t, v=tv: self._apply_thermal(tt, None, v))
            tk.Label(row2, text="s", bg=PANEL, fg=SUB,
                     font=("Microsoft YaHei UI", 8)).pack(side="left")
            self._temp_widgets.append((t, live))

        row = tk.Frame(f, bg=PANEL)
        row.pack(fill="x", pady=(2, 0))
        tk.Label(row, text="环境温度", bg=PANEL, fg=SUB, font=UI).pack(side="left")
        av = tk.StringVar(value="%.1f" % self.dev.temp[0].ambient_c)
        e = ttk.Entry(row, textvariable=av, width=6, style="Dark.TEntry",
                      font=MONO, justify="right")
        e.pack(side="right")
        e.bind("<Return>", lambda _e: self._apply_ambient(av))
        tk.Label(row, text="℃", bg=PANEL, fg=SUB, font=UI).pack(side="right", padx=3)

        # 故障注入
        f = section("故障注入")
        self.var_lost = []
        for t in self.dev.temp:
            v = tk.BooleanVar(value=not t.present)
            cb = tk.Checkbutton(f, text="%s 探头掉线" % t.label, variable=v,
                                bg=PANEL, fg=TEXT, selectcolor=CARD, font=UI,
                                activebackground=PANEL, activeforeground=TEXT,
                                command=lambda tt=t, vv=v: self._apply_lost(tt, vv))
            cb.pack(anchor="w")
            self.var_lost.append((t, v))

        row = tk.Frame(f, bg=PANEL)
        row.pack(fill="x", pady=(6, 0))
        tk.Label(row, text="输出队列上限", bg=PANEL, fg=SUB, font=UI).pack(side="left")
        self.var_q = tk.StringVar(value=str(self.dev.out_limit))
        e = ttk.Entry(row, textvariable=self.var_q, width=7, style="Dark.TEntry",
                      font=MONO, justify="right")
        e.pack(side="right")
        e.bind("<Return>", lambda _e: self._apply_queue())

        tk.Label(f, text="队列满 = 模拟 RAM 不够，会丢最旧的数据块",
                 bg=PANEL, fg=SUB, font=("Microsoft YaHei UI", 8),
                 wraplength=250, justify="left", anchor="w").pack(fill="x", pady=(4, 0))

        # 日志
        f = section("协议日志")
        self.log = tk.Text(f, height=8, bg=CARD, fg=SUB, font=("Consolas", 8),
                           bd=0, highlightthickness=0, wrap="none")
        self.log.pack(fill="both", expand=True)
        self.log.configure(state="disabled")

    # ---------------------------------------------------------------- 右侧卡片
    def _cards(self, root):
        grid = tk.Frame(root, bg=BG)
        grid.pack(fill="x")

        for i, c in enumerate(self.dev.power):
            r, col = divmod(i, 2)
            card = tk.Frame(grid, bg=CARD, highlightbackground=BORDER,
                            highlightthickness=1)
            card.grid(row=r, column=col, sticky="nsew", padx=4, pady=4)
            grid.columnconfigure(col, weight=1, uniform="card")
            self._power_card(card, c)

        # 温度条
        strip = tk.Frame(root, bg=CARD, highlightbackground=BORDER, highlightthickness=1)
        strip.pack(fill="x", padx=4, pady=(4, 4))
        self._temp_labels = []
        tk.Label(strip, text="温度", bg=CARD, fg=SUB, font=UI).pack(side="left", padx=10)
        for t in self.dev.temp:
            lbl = tk.Label(strip, text="%s --" % t.label, bg=CARD,
                           fg=TH_COLORS[t.idx], font=MONO_BIG)
            lbl.pack(side="left", padx=14)
            self._temp_labels.append(lbl)
        self.lbl_board = tk.Label(strip, text="板载 --", bg=CARD, fg=SUB, font=MONO)
        self.lbl_board.pack(side="right", padx=12)

        # 示波器
        sc = tk.Frame(root, bg=CARD, highlightbackground=BORDER, highlightthickness=1)
        sc.pack(fill="both", expand=True, padx=4, pady=(0, 4))
        head = tk.Frame(sc, bg=CARD)
        head.pack(fill="x", padx=10, pady=(8, 0))
        tk.Label(head, text="电压波形（近 5 秒）", bg=CARD, fg=SUB, font=UI).pack(side="left")
        self.lbl_span = tk.Label(head, text="", bg=CARD, fg=SUB, font=MONO)
        self.lbl_span.pack(side="right")
        self.scope = tk.Canvas(sc, bg=CARD, highlightthickness=0, height=158)
        self.scope.pack(fill="both", expand=True, padx=10, pady=(4, 10))
        self.scope.bind("<Configure>", lambda _e: self._draw_scope())

    def _power_card(self, parent, cfg):
        head = tk.Frame(parent, bg=CARD)
        head.pack(fill="x", padx=9, pady=(6, 1))
        tk.Label(head, text=cfg.label, bg=CARD, fg=CH_COLORS[cfg.idx],
                 font=("Consolas", 11, "bold")).pack(side="left")
        tk.Label(head, text="%.1f V 档" % (cfg.v_fs_dv / 10.0), bg=CARD, fg=SUB,
                 font=("Microsoft YaHei UI", 8)).pack(side="left", padx=6)

        var_model = tk.StringVar(value=KEY_TO_NAME.get(cfg.model, cfg.model))
        cb = ttk.Combobox(head, textvariable=var_model, values=MODEL_NAMES, width=10,
                          style="Dark.TCombobox", state="readonly", font=UI)
        cb.pack(side="right")
        cb.bind("<<ComboboxSelected>>", lambda _e, c=cfg, v=var_model: self._apply_model(c, v))

        # 三个读数
        vals = tk.Frame(parent, bg=CARD)
        vals.pack(fill="x", padx=9)
        lbl_v = tk.Label(vals, text="0.000 V", bg=CARD, fg=TEXT, font=MONO_BIG, anchor="w")
        lbl_v.pack(side="left")
        lbl_i = tk.Label(vals, text="0.000 A", bg=CARD, fg=SUB, font=MONO, anchor="e")
        lbl_i.pack(side="right")

        p_row = tk.Frame(parent, bg=CARD)
        p_row.pack(fill="x", padx=9, pady=(0, 2))
        lbl_p = tk.Label(p_row, text="0.00 W", bg=CARD, fg=SUB, font=MONO, anchor="w")
        lbl_p.pack(side="left")

        bar = tk.Canvas(parent, bg=CARD, height=5, highlightthickness=0)
        bar.pack(fill="x", padx=9, pady=(0, 4))

        # 编辑行
        ed = tk.Frame(parent, bg=CARD)
        ed.pack(fill="x", padx=9, pady=(0, 6))

        def mk(label, value, width, setter, unit=""):
            cell = tk.Frame(ed, bg=CARD)
            cell.pack(side="left", padx=(0, 6))
            tk.Label(cell, text=label, bg=CARD, fg=SUB,
                     font=("Microsoft YaHei UI", 8)).pack(anchor="w")
            box = tk.Frame(cell, bg=CARD)
            box.pack()
            var = tk.StringVar(value=value)
            e = ttk.Entry(box, textvariable=var, width=width,
                          style="Dark.TEntry", font=MONO, justify="right")
            e.pack(side="left")
            if unit:
                tk.Label(box, text=unit, bg=CARD, fg=SUB,
                         font=("Microsoft YaHei UI", 8)).pack(side="left", padx=(2, 0))
            e.bind("<Return>", lambda _e: setter(var))
            e.bind("<FocusOut>", lambda _e: setter(var))
            return var

        v_var = mk("电压 V", "%.2f" % cfg.v_set, 5, lambda v: self._apply_vi(cfg, v, None), "")
        i_var = mk("电流 A", "%.2f" % cfg.i_set, 5, lambda v: self._apply_vi(cfg, None, v), "")

        # 第 4 格随情景变义：运动→周期，充放→时长，电容→容量，其余→纹波
        cell4 = tk.Frame(ed, bg=CARD)
        cell4.pack(side="left", padx=(0, 6))
        lbl4 = tk.Label(cell4, text="纹波 mV", bg=CARD, fg=SUB,
                        font=("Microsoft YaHei UI", 8))
        lbl4.pack(anchor="w")
        box4 = tk.Frame(cell4, bg=CARD)
        box4.pack()
        p_var = tk.StringVar(value="%.0f" % cfg.ripple_mv)
        e4 = ttk.Entry(box4, textvariable=p_var, width=4, style="Dark.TEntry",
                       font=MONO, justify="right")
        e4.pack(side="left")
        e4.bind("<Return>", lambda _e, c=cfg, v=p_var: self._apply_param4(c, v))
        e4.bind("<FocusOut>", lambda _e, c=cfg, v=p_var: self._apply_param4(c, v))

        ed_item = dict(cfg=cfg, v=v_var, i=i_var, p4=p_var, lbl4=lbl4,
                       lbl_v=lbl_v, lbl_i=lbl_i, lbl_p=lbl_p, bar=bar)
        self._editors.append(ed_item)
        self._sync_param4(ed_item)

    # ---------------------------------------------------------------- 应用
    def _f(self, var, default=0.0):
        try:
            return float(var.get())
        except (ValueError, AttributeError):
            return default

    def _apply_vi(self, cfg, v_var, i_var):
        if v_var is not None:
            cfg.v_set = self._f(v_var, cfg.v_set)
        if i_var is not None:
            cfg.i_set = self._f(i_var, cfg.i_set)

    def _apply_ripple(self, cfg, var):
        cfg.ripple_mv = self._f(var, cfg.ripple_mv)

    def _apply_model(self, cfg, var):
        key = NAME_TO_KEY.get(var.get())
        if not key:
            return
        cfg.model = key
        for it in self._editors:
            if it["cfg"] is cfg:
                self._sync_param4(it)

    # 第 4 格：标签与字段随情景切换
    _P4_FIELD = {
        "motor": ("周期 s", "period_s"),
        "sweep": ("周期 s", "period_s"),
        "charge": ("CC 时长 s", "tau_s"),
        "discharge": ("放电时长 s", "tau_s"),
        "cap_charge": ("容量 mF", "cap_mf"),
        "cap_dis": ("容量 mF", "cap_mf"),
        "buck": ("纹波 mV", "ripple_mv"),
        "step_load": ("阶跃倍数", "step_ratio"),
        "dc": ("纹波 mV", "ripple_mv"),
    }

    def _sync_param4(self, item):
        label, field = self._P4_FIELD.get(item["cfg"].model, ("参数", "ripple_mv"))
        item["lbl4"].configure(text=label)
        item["p4"].set("%.2f" % getattr(item["cfg"], field) if field == "step_ratio"
                       else "%.0f" % getattr(item["cfg"], field))
        item["field4"] = field

    def _apply_param4(self, cfg, var):
        field = getattr(self, "_last_field4", {}).get(cfg.idx)
        for it in self._editors:
            if it["cfg"] is cfg:
                field = it.get("field4", field)
        if not field:
            return
        old = getattr(cfg, field, 0.0)
        setattr(cfg, field, self._f(var, old))

    def _apply_coupling(self, t, var):
        t.coupling = var.get()
        for cfg, model in zip(self.dev.temp, self.dev.thermal):
            if cfg is t:
                model.enabled = t.coupling

    def _apply_thermal(self, t, r_var, tau_var):
        if r_var is not None:
            t.r_th = max(0.0, self._f(r_var, t.r_th))
        if tau_var is not None:
            t.tau_s = max(1.0, self._f(tau_var, t.tau_s))
        for cfg, model in zip(self.dev.temp, self.dev.thermal):
            if cfg is t:
                model.r_th = t.r_th
                model.tau = t.tau_s

    def _apply_ambient(self, var):
        a = self._f(var, 25.0)
        for cfg, model in zip(self.dev.temp, self.dev.thermal):
            cfg.ambient_c = a
            model.ambient = a

    def _apply_temp(self, t, var):
        t.temp_c = self._f(var, t.temp_c)
        self.dev.sensors[t.idx].temp_c = t.temp_c

    def _apply_lost(self, t, var):
        t.present = not var.get()
        self.dev.sensors[t.idx].present = t.present

    def _apply_queue(self):
        try:
            self.dev.out_limit = max(16, int(self.var_q.get()))
        except ValueError:
            pass

    # ---------------------------------------------------------------- 刷新
    def _refresh(self):
        snap = None
        try:
            with self.runner.lock:
                snap = self.dev.snapshot()
        except Exception:
            pass

        if snap:
            st, color = STATE_TEXT.get(snap["state"], ("?", SUB))
            self.lbl_state.configure(text=st, fg=color)

            self.lbl_param.configure(
                text="mask 0x%02X   %d Hz\n间隔 %d ms\ntick %d   块 %d"
                     % (snap["mask"], snap["rate"], snap["interval_ms"],
                        snap["total_ticks"], snap["total_blocks"]))
            self.lbl_queue.configure(text="缓冲 %d / %d 块"
                                     % (snap["out_len"], self.dev.out_limit))

            for i, (v_mv, i_ma, p_mw, sat) in enumerate(snap["latest"]):
                e = self._editors[i]
                e["lbl_v"].configure(text=fmt_si(v_mv / 1000.0, "V"),
                                     fg=RED if sat else TEXT)
                e["lbl_i"].configure(text=fmt_si(i_ma / 1000.0, "A"))
                e["lbl_p"].configure(text=fmt_si(p_mw / 1000.0, "W"))
                self.hist[i].append(v_mv / 1000.0)
                self._draw_bar(e["bar"], v_mv, self.dev.power[i].full_scale_mv(), i)

            for idx, c16 in enumerate(snap["temp"]):
                if idx >= len(self._temp_labels):
                    break
                if c16 is None or c16 == 0x7FFF:
                    txt = "%s --" % self.dev.temp[idx].label
                    col = RED
                elif c16 == 0x0550 and self.dev.sensors[idx].is_default_85():
                    txt = "%s --" % self.dev.temp[idx].label
                    col = AMBER
                else:
                    txt = "%s %.1f ℃" % (self.dev.temp[idx].label, c16 / 16.0)
                    col = TH_COLORS[idx]
                self._temp_labels[idx].configure(text=txt, fg=col)

            for i, (t, live) in enumerate(getattr(self, "_temp_widgets", [])):
                if i < len(snap.get("temp_now", [])):
                    live.configure(text="%.1f ℃" % snap["temp_now"][i])

            for line in snap["log"][-6:]:
                self._append_log(line)

            self.lbl_traffic.configure(
                text="RX %d 帧   TX %d 帧" % (self.link.rx_frames, self.link.tx_frames))
            self.lbl_link.configure(
                text="● 已连接" if self.link.connected else "● 等待上位机",
                fg=GREEN if self.link.connected else SUB)
            self.status.configure(
                text="状态 %s   tick %d   块 %d   丢帧 %d   HID 限速 %s"
                     % (st, snap["total_ticks"], snap["total_blocks"],
                        self.dev.dropped_frames,
                        "开" if self.link.hid_pacing else "关"))

        self._draw_scope()
        self.after(50, self._refresh)

    def _append_log(self, line):
        self.log.configure(state="normal")
        self.log.insert("end", line + "\n")
        if int(self.log.index("end-1c").split(".")[0]) > 200:
            self.log.delete("1.0", "40.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _draw_bar(self, canvas, v_mv, fs_mv, idx=0):
        canvas.delete("all")
        w = max(canvas.winfo_width(), 10)
        h = 5
        canvas.create_rectangle(0, 0, w, h, fill=BORDER, outline="")
        ratio = 0 if fs_mv <= 0 else min(1.0, max(0.0, v_mv / fs_mv))
        canvas.create_rectangle(0, 0, w * ratio, h, fill=CH_COLORS[idx], outline="")

    def _draw_scope(self):
        """四路量程不同（14.2 / 28.1 / 75.9 / 102.3 V），
        所以按各自满量程归一化，否则 12 V 的曲线会被 96 V 的压成一条直线。"""
        c = self.scope
        c.delete("all")
        w = max(c.winfo_width(), 100)
        h = max(c.winfo_height(), 60)
        pad_l, pad_r, pad_t, pad_b = 40, 10, 8, 14
        x0, y0, x1, y1 = pad_l, pad_t, w - pad_r, h - pad_b

        c.create_line(x0, y1, x1, y1, fill=BORDER)
        c.create_line(x0, y0, x0, y1, fill=BORDER)

        for k in (1, 2, 3):
            yy = y1 - (y1 - y0) * k / 4
            c.create_line(x0, yy, x1, yy, fill=BORDER, dash=(2, 4))
            c.create_text(x0 - 5, yy, text="%d%%" % (k * 25),
                          fill=SUB, font=("Consolas", 7), anchor="e")

        n = max((len(d) for d in self.hist.values()), default=0)
        if n < 2:
            return
        for i, d in enumerate(self.hist.values()):
            if len(d) < 2:
                continue
            fs = self.dev.power[i].full_scale_mv() / 1000.0 or 1.0
            pts = []
            for k, v in enumerate(d):
                xx = x0 + (x1 - x0) * k / (n - 1)
                yy = y1 - (y1 - y0) * max(0.0, min(1.0, v / fs))
                pts += [xx, yy]
            c.create_line(*pts, fill=CH_COLORS[i], width=1.4)

        for i, cfg in enumerate(self.dev.power):
            c.create_text(x0 + 6 + i * 96, y0 + 7,
                          text="%s 0–%.0fV" % (cfg.label, cfg.v_fs_dv / 10.0),
                          fill=CH_COLORS[i], font=("Consolas", 7), anchor="w")
        self.lbl_span.configure(text="各通道按本路满量程归一化")

    # ---------------------------------------------------------------- 退出
    def _on_close(self):
        try:
            self.runner.stop()
            self.link.stop()
        finally:
            self.destroy()
