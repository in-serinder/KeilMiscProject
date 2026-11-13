# -*- coding: utf-8 -*-
"""
设备状态机 —— 与未来 C 固件一一对应。

约束（照抄进固件）：
  · handle() 与 step() 都不做 IO、不阻塞，输出只是往 out 队列塞帧
  · ACQUIRE 的 ACK 一定在第一个数据块之前
  · ABORT 先排空数据块再回 ACK（上位机超时给 2 s）
  · 运行中收到 ACQUIRE / SET_CH_CONFIG 一律 BAD_STATE
"""

import struct
from collections import deque

from . import proto as P
from .config import default_power, default_temp, temp_min_interval_ms, ADC_FULL
from .onewire import Ds18b20
from .sampler import Sampler
from .signals import Sampler32
from .thermal import ThermalModel
from .trigger import TriggerCfg, TriggerEngine

CAPS = 0xBF          # RAW|BIN|TIER|TEMP|TRIGGER|TIMED|(bit6=0)|per-channel-range
MAX_RATE = 20000
MAX_BLOCK_TICKS = 1024
FS_MAGIC = b"PDL1"


class Device:
    def __init__(self, power=None, temp=None, out_limit=4000):
        self.power = power or default_power()
        self.temp = temp or default_temp()

        self.state = P.ST_.IDLE
        self.seq = 0
        self.out = deque()          # 待发送的 64 B 报文
        self.out_limit = out_limit

        self.t_us = 0
        self.bind = 0               # 当前绑定的上位机（0=无）
        self.session_start_us = 0
        self.tick_index = 0
        self.next_tick_us = 0
        self.period_us = 1000
        self.temp_next_us = 0

        # 采集参数
        self.mask = 0x0F
        self.temp_mask = 0x03
        self.rate = 1000
        self.interval_ms = 100
        self.bin_ms = 0
        self.tier = 0
        self.duration_s = 0
        self.temp_interval_ms = 1000
        self.temp_res_bits = 12
        self.record_mode = 0

        self.sampler = None
        self.sig = Sampler32(self.power)
        self.trigger = TriggerEngine()
        self.trigger_on = False
        self.sensors = [Ds18b20(t.idx, t.temp_c, t.res_bits, t.present) for t in self.temp]
        self.thermal = [ThermalModel(t.temp_c, t.ambient_c, t.r_th, t.tau_s, t.coupling)
                        for t in self.temp]
        self._last_th_us = 0

        # 统计
        self.block_seq = 0
        self.total_blocks = 0
        self.dropped_ticks = 0
        self.overrun_blocks = 0
        self.err_flags = 0
        self.stop_reason = 0
        self.trig_count = 0
        self.dropped_frames = 0

        self.latest = [(0, 0, 0, False) for _ in self.power]   # (v_mv, i_ma, p_mw, sat)
        self.temp_c16 = [None, None]
        self.log = deque(maxlen=300)

    # ================================================================ 发送
    def _emit(self, frame: P.Frame):
        if len(self.out) >= self.out_limit:
            self.out.popleft()
            self.dropped_frames += 1
            self.overrun_blocks += 1
            self.err_flags |= 0x01
        self.out.append(frame.encode())

    def _next_seq(self) -> int:
        s = self.seq
        self.seq = (self.seq + 1) & 0xFFFF
        return s

    def _ack(self, req: P.Frame, status=P.ST.OK, detail=0, extra=b"", cmd=None):
        self._emit(P.Frame(P.T.ACK, P.FF.RESP, req.seq,
                           P.ack_payload(cmd if cmd is not None else req.type,
                                         status, detail, extra)))

    def _event(self, code: int, ch=0xFF, param=0):
        self._emit(P.Frame(P.T.EVENT, P.FF.EVENT, self._next_seq(),
                           P.event_payload(code, ch, param, self.t_us // 1000)))

    def drain_frames(self):
        out = list(self.out)
        self.out.clear()
        return out

    # ================================================================ 命令
    def handle(self, f: P.Frame):
        p = f.payload
        try:
            fn = {
                P.T.IDENTIFY: self._cmd_identify,
                P.T.GET_CH_CONFIG: self._cmd_get_ch_config,
                P.T.SET_CH_CONFIG: self._cmd_set_ch_config,
                P.T.ACQUIRE: self._cmd_acquire,
                P.T.ABORT: self._cmd_abort,
                P.T.GET_STATUS: self._cmd_get_status,
                P.T.READ_TEMP: self._cmd_read_temp,
                P.T.ZERO: self._cmd_zero,
                P.T.SAVE_CONFIG: self._cmd_save,
                P.T.TIME_SYNC: self._cmd_time_sync,
                P.T.RESET: self._cmd_reset,
                P.T.ECHO: self._cmd_echo,
            }.get(f.type)
            if fn is None:
                self._ack(f, P.ST.UNKNOWN_CMD)
                return
            fn(f, p)
        except struct.error:
            self._ack(f, P.ST.LEN_ERROR)

    # ------------------------------------------------------------ 0x10
    def _cmd_identify(self, f, p):
        nonce = struct.unpack("<I", p[:4])[0] if len(p) >= 4 else 0
        extra = struct.pack("<4sHHHHBBBBHHHHHHI",
                            FS_MAGIC, 1, 4, 0x0100, 1,
                            len(self.power), len(self.temp), CAPS, P.REPORT,
                            MAX_RATE, MAX_BLOCK_TICKS,
                            *[c.fs_current_ma for c in self.power],
                            nonce)
        self._ack(f, extra=extra)
        self.log.append("IDENTIFY nonce=%08X" % nonce)

    # ------------------------------------------------------------ 0x11 / 0x30
    def _cfg_bytes(self, ch: int) -> bytes:
        if ch < len(self.power):
            c = self.power[ch]
            return struct.pack("<BBBBIIIIhhHH", ch, int(c.enabled), c.sensor_type, 0,
                               c.v_num, c.v_den, c.i_num, c.i_den,
                               c.v_off, c.i_off, c.fs_current_ma, c.v_fs_dv)
        t = self.temp[ch - len(self.power)]
        return struct.pack("<BBBBIIIIhhHH", ch, int(t.enabled), t.sensor_type,
                           t.res_bits, 0, 0, 0, 0, 0, t.c16_offset, 0, 0)

    def _cmd_get_ch_config(self, f, p):
        ch = p[0] if p else 0
        if ch >= len(self.power) + len(self.temp):
            self._ack(f, P.ST.BAD_PARAM, 0)
            return
        self._ack(f, extra=self._cfg_bytes(ch))

    def _cmd_set_ch_config(self, f, p):
        if self.state in (P.ST_.RUNNING, P.ST_.ARMED, P.ST_.DRAINING):
            self._ack(f, P.ST.BAD_STATE)
            return
        if len(p) < 28:
            self._ack(f, P.ST.LEN_ERROR)
            return
        (ch, en, stype, res, v_num, v_den, i_num, i_den,
         v_off, i_off, fs_ma, v_fs) = struct.unpack("<BBBBIIIIhhHH", p[:28])
        if ch < len(self.power):
            c = self.power[ch]
            c.enabled, c.sensor_type = bool(en), stype
            c.v_num, c.v_den, c.i_num, c.i_den = v_num, v_den, i_num, i_den
            c.v_off, c.i_off = v_off, i_off
            c.fs_current_ma, c.v_fs_dv = fs_ma, v_fs
        elif ch < len(self.power) + len(self.temp):
            t = self.temp[ch - len(self.power)]
            t.enabled, t.res_bits, t.c16_offset = bool(en), res, i_off
            self.sensors[t.idx].res_bits = res
        else:
            self._ack(f, P.ST.BAD_PARAM, 0)
            return
        self._ack(f)

    # ------------------------------------------------------------ 0x20
    def _cmd_acquire(self, f, p):
        if self.state != P.ST_.IDLE:
            self._ack(f, P.ST.BAD_STATE)
            return
        if len(p) < 48:
            self._ack(f, P.ST.LEN_ERROR)
            return

        (pmask, tmask, mode, tier,
         rate, interval, bin_ms, duration,
         temp_iv, tsrc, tch, tflags, tres,
         hold_s, hold_e, start_v, stop_v,
         post_ms, pre_ms) = struct.unpack("<BBBBIIIIIBBBBHHiiII", p[:48])

        err = 0
        if pmask == 0 or (pmask & 0xF0):
            err = 0
        elif tmask & 0xFC:
            err = 1
        elif not (1 <= rate <= MAX_RATE):
            err = 4
        elif not (1 <= interval <= 60000):
            err = 8
        elif rate * interval // 1000 > MAX_BLOCK_TICKS or rate * interval // 1000 < 1:
            err = 8
        elif mode not in (0, 1, 2):
            err = 2
        elif mode == 2 and (tflags & 0x03) == 0:
            err = 26
        elif mode == 2 and tch >= len(self.power):
            err = 25
        elif tres not in (0, 9, 10, 11, 12):
            err = 27
        elif tmask and temp_iv < temp_min_interval_ms(tres or 12):
            err = 20
        if err:
            self._ack(f, P.ST.BAD_PARAM, err)
            return

        self.mask = pmask
        self.temp_mask = tmask & 0x03
        self.record_mode = mode
        self.tier = tier
        self.rate = rate
        self.interval_ms = interval
        self.bin_ms = bin_ms
        self.duration_s = duration
        self.temp_interval_ms = temp_iv
        self.temp_res_bits = tres or 12
        for s in self.sensors:
            s.res_bits = self.temp_res_bits

        self.period_us = max(1, int(1_000_000 / rate))
        self.session_start_us = self.t_us
        self.tick_index = 0
        self.next_tick_us = self.t_us
        # 温度第一次上报要等一个间隔（DS18B20 转换需要 750 ms，不能立刻出数）
        self.temp_next_us = self.t_us + self.temp_interval_ms * 1000
        self.block_seq = 0
        self.total_blocks = 0
        self.dropped_ticks = 0
        self.overrun_blocks = 0
        self.trig_count = 0
        self.stop_reason = 0

        self.sampler = Sampler(self.power, pmask, rate, interval, tier, bin_ms)

        # 触发
        self.trigger_on = bool(tflags & 0x03)
        if self.trigger_on:
            tc = TriggerCfg()
            tc.source, tc.ch = tsrc, tch
            tc.start_en = bool(tflags & 0x01)
            tc.stop_en = bool(tflags & 0x02)
            tc.autorearm = bool(tflags & 0x04)
            tc.start_val, tc.stop_val = start_v, stop_v
            tc.start_hold_ms, tc.stop_hold_ms = hold_s, hold_e
            self.trigger.arm(tc, self.t_us)

        if mode == 2 and self.trigger_on:
            self.state = P.ST_.ARMED
        else:
            self.state = P.ST_.RUNNING
            self.sampler.start(0)
        # 先启动一次温度转换（750 ms 后才有效）
        for s in self.sensors:
            if s.present:
                s.start_convert(self.t_us)

        extra = struct.pack("<BBHIIIHBB",
                            self.state, tier,
                            self.bin_ms or {0: 0, 1: 10, 2: 1000, 3: 60000}.get(tier, 0),
                            rate, interval, 0, temp_iv, self.temp_res_bits, 0)
        self._ack(f, extra=extra)     # 必须先于第一个数据块
        self.log.append("ACQUIRE mask=%02X mode=%d %dHz/%dms" % (pmask, mode, rate, interval))
        if self.state == P.ST_.ARMED:
            self._event(P.EV.TRIG_ARMED, tch, 0)

    # ------------------------------------------------------------ 0x22
    def _cmd_abort(self, f, p):
        if self.state not in (P.ST_.RUNNING, P.ST_.ARMED, P.ST_.DRAINING):
            self._ack(f, P.ST.BAD_STATE)
            return
        discard = bool(p[0] & 0x01) if p else False
        summary = self._stop(0, discard)
        self._emit(P.Frame(P.T.ACK, P.FF.RESP, f.seq,
                           P.ack_payload(P.T.ABORT, P.ST.OK, 0, summary)))
        self.log.append("ABORT total_ticks=%d" % (struct.unpack("<I", summary[4:8])[0]))

    def _stop(self, reason: int, discard=False) -> bytes:
        """收尾：停采样 -> 排空 -> 返回 24 B 汇总"""
        if self.state != P.ST_.DRAINING:
            duration_ms = (self.t_us - self.session_start_us) // 1000
        else:
            duration_ms = 0
        self.stop_reason = reason

        if not discard and self.sampler is not None and self.state == P.ST_.RUNNING:
            payload = self.sampler.pop_block(self._take_block_seq(), final=True)
            if payload:
                self._emit_block(payload)

        total_ticks = self.tick_index
        self.state = P.ST_.IDLE
        self.trigger.disarm()
        self._event(P.EV.ABORTED if reason == 0 else P.EV.AUTO_STOP, 0xFF, reason)
        return struct.pack("<BBHIIIII", self.state, reason, 0,
                           total_ticks, self.total_blocks, self.dropped_ticks,
                           duration_ms, self.trig_count)

    # ------------------------------------------------------------ 0x23
    def _cmd_get_status(self, f, p):
        cap = self.out_limit
        extra = struct.pack("<BBBBIIIIIIHHhIHhh",
                            self.state, self.mask, self.temp_mask, self.err_flags,
                            self.rate, self.interval_ms,
                            self.tick_index, self.total_blocks,
                            self.overrun_blocks, self.dropped_ticks,
                            len(self.out), cap,
                            int(self.temp_c16[0] or 0) // 10,
                            self.t_us // 1_000_000,
                            self.trigger.phase if self.trigger_on else 0,
                            self.temp_c16[0] if self.temp_c16[0] is not None else 0x7FFF,
                            self.temp_c16[1] if self.temp_c16[1] is not None else 0x7FFF)
        self._ack(f, extra=extra)

    # ------------------------------------------------------------ 0x21
    def _cmd_read_temp(self, f, p):
        vals = []
        for s in self.sensors:
            if not s.present:
                vals.append(0x7FFF)
                continue
            s.start_convert(self.t_us - s.conv_us)   # 立刻可用（单次读取不等 750ms）
            v = s.read(self.t_us)
            vals.append(v if v is not None else 0x7FFF)
        self._ack(f, extra=struct.pack("<hh", *vals))

    # ------------------------------------------------------------ 0x31 / 0x32
    def _cmd_zero(self, f, p):
        if self.state != P.ST_.IDLE:
            self._ack(f, P.ST.BAD_STATE)
            return
        mask = p[0] if p else 0x0F
        what = p[1] if len(p) > 1 else 0x03
        extra = b""
        for c in self.power:
            if not (mask >> c.idx) & 1:
                continue
            # 模拟：把当前读数当作零点
            v, i = self.sig.sources[c.idx].sample(self.t_us / 1e6)
            if what & 0x01:
                c.v_off = c.v_to_raw(v)
            if what & 0x02:
                c.i_off = c.i_to_raw(i)
            extra += struct.pack("<hh", c.v_off, c.i_off)
        self._ack(f, extra=extra)

    def _cmd_save(self, f, p):
        self._ack(f, detail=28 * (len(self.power) + len(self.temp)))

    # ------------------------------------------------------------ 0x40
    def _cmd_time_sync(self, f, p):
        host_us = struct.unpack("<Q", p[:8])[0] if len(p) >= 8 else 0
        self._ack(f, extra=struct.pack("<QQ", host_us, self.t_us))

    # ------------------------------------------------------------ 0x60 / 0x7F
    def _cmd_reset(self, f, p):
        mode = p[0] if p else 0
        self._ack(f)
        if mode == 1:
            self.power = default_power()
            self.temp = default_temp()
            self.sig = Sampler32(self.power)
            self.sensors = [Ds18b20(t.idx, t.temp_c, t.res_bits, t.present) for t in self.temp]
            self.thermal = [ThermalModel(t.ambient_c, t.ambient_c, t.r_th, t.tau_s, t.coupling)
                            for t in self.temp]
        self.state = P.ST_.IDLE
        self.trigger.disarm()
        self.log.append("RESET mode=%d" % mode)

    def _cmd_echo(self, f, p):
        self._ack(f, extra=p[:P.MAX_PAYLOAD - 4])

    # ================================================================ 定时推进
    def step(self, now_us: int):
        self.t_us = now_us
        self._thermal_step(now_us)
        if self.state == P.ST_.RUNNING:
            guard = 0
            while now_us >= self.next_tick_us and guard < 4000:
                self._do_tick(self.next_tick_us)
                self.next_tick_us += self.period_us
                guard += 1
                if self.state != P.ST_.RUNNING:
                    break
        elif self.state == P.ST_.ARMED:
            guard = 0
            while now_us >= self.next_tick_us and guard < 4000:
                self._eval_trigger(self.next_tick_us)
                self.next_tick_us += self.period_us
                guard += 1
                if self.state != P.ST_.ARMED:
                    break
        if self.state in (P.ST_.RUNNING, P.ST_.ARMED):
            self._temp_step(now_us)

    # ---------------------------------------------------------------- 热模型
    def _thermal_step(self, now_us: int):
        """功率 -> 温度。放电/假负载场景靠它让温度跟着功率走。"""
        dt = (now_us - self._last_th_us) / 1e6 if self._last_th_us else 0.0
        self._last_th_us = now_us
        if dt <= 0 or dt > 5.0:      # 首次 / 挂起太久，跳过一次
            dt = 0.0
        running = self.state == P.ST_.RUNNING
        for cfg, model, sensor in zip(self.temp, self.thermal, self.sensors):
            mask = cfg.bound_mask or (1 << cfg.idx)
            p_w = 0.0
            if running:
                for i in range(len(self.power)):
                    if (mask >> i) & 1:
                        p_w += self.latest[i][2] / 1000.0
            model.update(dt, p_w)
            sensor.temp_c = model.t

    # ---------------------------------------------------------------- tick
    def _do_tick(self, t_us: int):
        idx = self.tick_index
        self.tick_index += 1
        samples = self.sig.tick((t_us - self.session_start_us) / 1e6, self.mask)
        self._update_latest(samples)
        sat = any(s[3] for s in samples)
        self.sampler.add_tick(idx, t_us, samples, sat)

        if self.trigger_on and self.trigger.phase != TriggerEngine.DONE:
            v, i, pw = self._trig_values(samples)
            for code, param in self.trigger.update(t_us, v, i, pw):
                self.trig_count = self.trigger.trig_count
                self._event(code, self.trigger.cfg.ch, param)
            if self.trigger.phase == TriggerEngine.DONE and self.trigger.cfg.stop_en:
                self._auto_stop(2)
                return

        if self.duration_s > 0 and idx + 1 >= self.duration_s * self.rate:
            self._auto_stop(1)
            return

        while True:
            payload = self.sampler.pop_block(self._take_block_seq(),
                                             extra_flags=P.BF.TRIGGERED if self.trigger_on else 0)
            if payload is None:
                break
            self._emit_block(payload)

    def _eval_trigger(self, t_us: int):
        idx = self.tick_index
        self.tick_index += 1
        samples = self.sig.tick((t_us - self.session_start_us) / 1e6, self.mask)
        self._update_latest(samples)
        v, i, pw = self._trig_values(samples)
        for code, param in self.trigger.update(t_us, v, i, pw):
            self.trig_count = self.trigger.trig_count
            self._event(code, self.trigger.cfg.ch, param)
            if code == P.EV.TRIG_STARTED:
                # 触发命中：从此开始计块，时间轴从这一刻起
                self.state = P.ST_.RUNNING
                self.session_start_us = t_us
                self.tick_index = 0
                self.sampler.start(0)
                break

    def _trig_values(self, samples):
        ch = self.trigger.cfg.ch
        for _c, vr, ir, _s in samples:
            if _c.idx == ch:
                v_mv = self.power[ch].raw_to_mv(vr)
                i_ma = self.power[ch].raw_to_ma(ir)
                return v_mv, i_ma, int(v_mv * i_ma / 1000)
        return 0, 0, 0

    def _update_latest(self, samples):
        for c, vr, ir, sat in samples:
            v_mv = c.raw_to_mv(vr)
            i_ma = c.raw_to_ma(ir)
            self.latest[c.idx] = (v_mv, i_ma, int(v_mv * i_ma / 1000), sat)

    def _take_block_seq(self) -> int:
        s = self.block_seq
        self.block_seq = (self.block_seq + 1) & 0xFFFF
        return s

    def _emit_block(self, payload: bytes):
        for fr in P.Fragmenter.split(payload, P.T.DATA, self._next_seq):
            self._emit(fr)
        self.total_blocks += 1

    # ---------------------------------------------------------------- 温度
    def _temp_step(self, now_us: int):
        if not self.temp_mask:
            return
        iv_us = self.temp_interval_ms * 1000
        if now_us < self.temp_next_us:
            return
        self.temp_next_us = now_us + iv_us

        vals = []
        for t in self.temp:
            s = self.sensors[t.idx]
            if not (self.temp_mask >> t.idx) & 1:
                continue
            v = s.read(now_us)
            if v is None:
                self.err_flags |= 0x10
                self._event(P.EV.TEMP_LOST, t.idx, 0)
                if not s.present:
                    continue
                v = 0x7FFF
            vals.append((t.idx, v))
            s.start_convert(now_us)

        if not vals:
            return
        for i, (idx, v) in enumerate(vals):
            if i < len(self.temp_c16):
                self.temp_c16[idx] = v

        data = b""
        # 一个点：mask 内每个通道一个 i16
        by_idx = {i: v for i, v in vals}
        mask = 0
        for t in self.temp:
            if t.idx in by_idx:
                mask |= 1 << t.idx
        for t in self.temp:
            if t.idx in by_idx:
                data += struct.pack("<h", by_idx[t.idx])

        payload = P.block_header(self.tick_index, now_us // 1000, self._take_block_seq(),
                                 1, mask, P.BF.TEMP_BLOCK | P.BF.FIRST, P.FMT.TEMP16) + data
        self._emit_block(payload)

    def _auto_stop(self, reason: int):
        if self.state != P.ST_.RUNNING:
            return
        self.state = P.ST_.DRAINING
        self._stop(reason)

    # ================================================================ GUI 快照
    def snapshot(self):
        return {
            "state": self.state,
            "out_len": len(self.out),
            "rate": self.rate,
            "interval_ms": self.interval_ms,
            "mask": self.mask,
            "total_ticks": self.tick_index,
            "total_blocks": self.total_blocks,
            "dropped_ticks": self.dropped_ticks,
            "trig_phase": self.trigger.phase if self.trigger_on else -1,
            "latest": list(self.latest),
            "temp": [self.temp_c16[0], self.temp_c16[1]],
            "temp_now": [m.t for m in self.thermal],
            "models": [c.model for c in self.power],
            "log": list(self.log)[-12:],
        }
