# -*- coding: utf-8 -*-
"""
PDL 下位机模拟器入口。

用法：
    python run_sim.py                       # 开 GUI + TCP 监听 127.0.0.1:5555
    python run_sim.py --selftest            # 不开 GUI，跑协议自检
    python run_sim.py --headless            # 只跑协议，无界面（CI / 长跑）
    python run_sim.py --hid-pacing off      # 关掉 1 ms/64B 限速
    python run_sim.py --time-scale 100      # 时钟加速 100 倍，用来验证长跑
"""

import argparse
import os
import struct
import sys
import threading
import time

from pdl import proto as P
from pdl.config import default_power, default_temp
from pdl.device import Device
from transports.tcp import TcpLink


def us() -> int:
    return time.perf_counter_ns() // 1000


# ====================================================================== 运行器
class Runner:
    """把「设备状态机」和「时钟 / 传输」解耦。

    设备本身不碰线程、不碰 socket，所以自检可以直接喂它假时间。
    """

    def __init__(self, dev: Device, link=None, time_scale: float = 1.0):
        self.dev = dev
        self.link = link
        self.lock = threading.RLock()
        self.ts = time_scale
        self._t0 = us()
        self._running = False
        self._next_step = us()

    def now(self) -> int:
        return int((us() - self._t0) * self.ts) + 1_000_000

    # ---------------------------------------------------------------- 输入
    def on_frame(self, f: P.Frame):
        with self.lock:
            self.dev.t_us = self.now()
            self.dev.handle(f)
            frames = self.dev.drain_frames()
        if self.link:
            for fr in frames:
                self.link.send(fr)

    # ---------------------------------------------------------------- 输出
    def pump(self) -> int:
        now = self.now()
        with self.lock:
            self.dev.step(now)
            frames = self.dev.drain_frames()
        if self.link:
            for f in frames:
                self.link.send(f)
        return len(frames)

    # ---------------------------------------------------------------- 主循环
    def run_forever(self):
        """每 1 ms 推进一次。

        注意两点：
          · 用 sleep 而不是死等 —— 死等会抢 GIL，把发送线程饿死（踩过）
          · Windows 默认 sleep 粒度 15 ms，先调高定时器精度，否则数据会成批涌出
        """
        _set_timer_resolution(1)
        self._running = True
        while self._running:
            self.pump()
            time.sleep(0.001)

    def stop(self):
        self._running = False


def _set_timer_resolution(ms: int = 1):
    """Windows 上把 sleep 粒度调到 1 ms，非 Windows 静默忽略"""
    try:
        import ctypes
        ctypes.windll.winmm.timeBeginPeriod(ms)   # noqa: F821
        return True
    except Exception:
        return False


# ====================================================================== 自检
def _req(t, seq, payload=b""):
    return P.Frame(t, 0, seq, payload)


def _feed(dev, t, seq, payload=b""):
    dev.t_us = 1_000_000
    dev.handle(_req(t, seq, payload))
    return [P.parse_frame(f) for f in dev.drain_frames()]


def selftest(verbose=True) -> int:
    fails = []

    def check(name, cond, detail=""):
        if cond:
            if verbose:
                print("  [ OK ] %s" % name)
        else:
            fails.append(name)
            print("  [FAIL] %s %s" % (name, detail))

    print("PDL 模拟器自检\n" + "-" * 52)

    # ---- 1. 帧与 CRC ----
    print("[1] 帧编解码 / CRC")
    f = P.Frame(P.T.IDENTIFY, 0, 1, struct.pack("<I", 0x11223344))
    check("IDENTIFY 请求字节", f.hex() == "A5 01 10 00 04 00 01 00 44 33 22 11 47 12",
          "\n        got %s" % f.hex())
    rt = P.parse_frame(f.encode())
    check("round-trip", rt is not None and rt.payload == f.payload and rt.type == f.type)

    bad = bytearray(f.encode())
    bad[10] ^= 0xFF
    check("CRC 检错", P.parse_frame(bytes(bad)) is None)

    d = P.Decoder()
    two = f.encode() + f.encode()
    check("流式解码 2 帧", len(d.feed(two[:70])) == 1 and len(d.feed(two[70:])) == 1)

    # ---- 2. 器件 ----
    print("[2] 器件与配置")
    from pdl.config import PowerCfg
    c0 = default_power()[0]
    check("CH0 3.46 mV/count", abs(c0.lsb_uv() - 3460) < 1, "%.1f" % c0.lsb_uv())
    check("CH3 24.98 mV/count", abs(default_power()[3].lsb_uv() - 24980) < 2)
    check("CH0 满量程 14.17 V", abs(c0.full_scale_mv() / 1000 - 14.17) < 0.05)
    raw = c0.v_to_raw(12.0)
    check("12 V -> raw -> 12 V", abs(c0.raw_to_mv(raw) / 1000 - 12.0) < 0.01,
          "raw=%d mv=%d" % (raw, c0.raw_to_mv(raw)))

    # ---- 3. IDENTIFY ----
    print("[3] IDENTIFY")
    dev = Device()
    frames = _feed(dev, P.T.IDENTIFY, 1, struct.pack("<I", 0x11223344))
    check("有应答", len(frames) == 1 and frames[0].type == P.T.ACK)
    pl = frames[0].payload
    cmd, status, detail = struct.unpack("<BBH", pl[:4])
    magic = pl[4:8]
    nonce = struct.unpack("<I", pl[32:36])[0]
    check("magic=PDL1", magic == b"PDL1", magic)
    check("nonce 回显", nonce == 0x11223344)
    power_ch, temp_ch, caps, rsize = pl[16], pl[17], pl[18], pl[19]
    check("4 功率 2 温度", power_ch == 4 and temp_ch == 2)
    check("caps bit7=1", caps & 0x80 != 0, hex(caps))

    # ---- 4. GET_CH_CONFIG ----
    print("[4] GET_CH_CONFIG x6")
    ok = True
    expect_vfs = [142, 281, 759, 1023]
    for ch in range(6):
        fr = _feed(dev, P.T.GET_CH_CONFIG, 2 + ch, bytes([ch]))
        if not fr or len(fr[0].payload) < 32:
            ok = False
            break
        cfg = fr[0].payload[4:32]
        if ch < 4:
            v_fs = struct.unpack("<H", cfg[26:28])[0]
            if v_fs != expect_vfs[ch]:
                ok = False
                print("      ch%d v_fs=%d (期望 %d)" % (ch, v_fs, expect_vfs[ch]))
        else:
            res = cfg[3]
            if res != 12:
                ok = False
    check("六路配置读回正确", ok)

    # ---- 5. ACQUIRE + 收块 ----
    print("[5] ACQUIRE / 数据块")
    acq = struct.pack("<BBBBIIIIIBBBBHHiiII",
                      0x0F, 0x03, 0, 0,          # mask, temp_mask, mode=手动, tier=0
                      1000, 100, 0, 0,           # rate, interval, bin, duration
                      1000,                      # temp_interval
                      0, 0, 0x03, 12,            # trig src/ch/flags, temp_res
                      50, 50, 12000, 8000,       # holds, start, stop
                      0, 0)
    fr = _feed(dev, P.T.ACQUIRE, 10, acq)
    cmd, status, _ = struct.unpack("<BBH", fr[0].payload[:4])
    check("ACQUIRE 应答 OK", status == 0, "status=%d" % status)
    state = fr[0].payload[4]
    check("进入 RUNNING", state == P.ST_.RUNNING)

    # 推进 1 秒
    t = 1_000_000
    total_frames = 0
    for _ in range(1000):
        t += 1000
        dev.t_us = t
        dev.step(t)
        total_frames += len(dev.drain_frames())

    blocks = dev.total_blocks
    ticks = dev.tick_index
    check("1 秒 ≈ 1000 tick", 990 <= ticks <= 1010, "ticks=%d" % ticks)
    # 4ch RAW 每帧 2/3 tick -> 100ms 一块约 34 帧，10 块 + 温度块
    check("10 秒块数合理", 9 <= blocks <= 25, "blocks=%d" % blocks)
    check("有数据帧发出", total_frames > 100, "frames=%d" % total_frames)

    # ---- 6. 块头解析 ----
    print("[6] 块头")
    dev2 = Device()
    _feed(dev2, P.T.ACQUIRE, 11, acq)
    got = None
    for _ in range(400):
        dev2.t_us += 1000
        dev2.step(dev2.t_us)
        for raw in dev2.drain_frames():
            f2 = P.parse_frame(raw)
            if (f2 and f2.type == P.T.DATA and (f2.flags & P.FF.FIRST)
                    and f2.payload[13] != P.FMT.TEMP16):
                got = f2
                break
        if got:
            break
    check("拿到功率块首片", got is not None)
    if got:
        si, t_ms, bseq, cnt, mask, bflags, fmt = struct.unpack("<IIHBBBB", got.payload[:14])
        check("块头 sample_index=0", si == 0, si)
        check("块头 mask=0x0F", mask == 0x0F, hex(mask))
        check("块头 fmt=RAW16", fmt == P.FMT.RAW16)
        check("块头 count>0", cnt > 0, cnt)
        check("首片带 FIRST", bool(got.flags & P.FF.FIRST))

    # ---- 7. ABORT ----
    print("[7] ABORT")
    dev3 = Device()
    _feed(dev3, P.T.ACQUIRE, 12, acq)
    for _ in range(50):
        dev3.t_us += 1000
        dev3.step(dev3.t_us)
    dev3.drain_frames()
    fr = _feed(dev3, P.T.ABORT, 13, b"\x00")
    kinds = [f.type for f in fr]
    check("ABORT 有 ACK", P.T.ACK in kinds)
    check("ABORT 后回 IDLE", dev3.state == P.ST_.IDLE)
    ack = [f for f in fr if f.type == P.T.ACK][0]
    check("ABORT 汇总 24B", len(ack.payload) == 28, len(ack.payload))
    check("块排在 ACK 之前", P.T.DATA not in kinds or kinds.index(P.T.DATA) < kinds.index(P.T.ACK))

    # ---- 8. 运行中禁改 ----
    print("[8] 运行中禁改参数")
    dev4 = Device()
    _feed(dev4, P.T.ACQUIRE, 14, acq)
    fr = _feed(dev4, P.T.ACQUIRE, 15, acq)
    check("重入 ACQUIRE -> BAD_STATE", struct.unpack("<BBH", fr[0].payload[:4])[1] == P.ST.BAD_STATE)
    fr = _feed(dev4, P.T.SET_CH_CONFIG, 16, bytes(28))
    check("运行中改配置 -> BAD_STATE", struct.unpack("<BBH", fr[0].payload[:4])[1] == P.ST.BAD_STATE)

    # ---- 9. 触发 ----
    print("[9] 触发（消抖）")
    from pdl.trigger import TriggerCfg, TriggerEngine
    from pdl.proto import EV
    te = TriggerEngine()
    tc = TriggerCfg()
    tc.start_val, tc.stop_val = 12000, 8000
    tc.start_hold_ms, tc.stop_hold_ms = 50, 50
    te.arm(tc, 0)
    evs = []
    # 5 ms 尖峰：不该触发
    for k in range(1, 6):
        evs += te.update(k * 1000, 20000, 0, 0)
    for k in range(6, 60):
        evs += te.update(k * 1000, 5000, 0, 0)
    check("5 ms 尖峰不触发", not any(c == EV.TRIG_STARTED for c, _ in evs))
    # 持续 60 ms：应触发
    for k in range(60, 130):
        evs += te.update(k * 1000, 20000, 0, 0)
    check("保持 60 ms 触发", any(c == EV.TRIG_STARTED for c, _ in evs))

    # ---- 10. 温度 ----
    print("[10] DS18B20 行为")
    from pdl.onewire import Ds18b20
    s = Ds18b20(0, 25.0, 12)
    check("上电默认 85℃(0x0550)", s.read(0) == 0x0550)
    s.start_convert(0)
    check("750 ms 内仍是旧值", s.read(500_000) == 0x0550)
    s.read(800_000)
    check("转换完成 25.0℃", s.read(800_000) == 400)
    s.res_bits = 9
    s.start_convert(1_000_000)
    check("9 bit 转换 100 ms", s.read(1_150_000) == 400)   # 25.0℃, 0.5℃ 步进
    s.present = False
    check("掉线返回 None", s.read(2_000_000) is None)

    # ---- 11. BIN 模式 ----
    print("[11] BIN 聚合")
    dev5 = Device()
    acq_bin = struct.pack("<BBBBIIIIIBBBBHHiiII",
                          0x0F, 0, 0, 1, 1000, 100, 10, 0,
                          1000, 0, 0, 0, 12, 50, 50, 12000, 8000, 0, 0)
    fr = _feed(dev5, P.T.ACQUIRE, 20, acq_bin)
    check("BIN ACQUIRE OK", struct.unpack("<BBH", fr[0].payload[:4])[1] == 0)
    got, buf = None, None
    for _ in range(400):
        dev5.t_us += 1000
        dev5.step(dev5.t_us)
        for raw in dev5.drain_frames():
            f3 = P.parse_frame(raw)
            if not f3 or f3.type != P.T.DATA:
                continue
            if f3.flags & P.FF.FIRST:
                buf = bytearray(f3.payload)
            elif buf is not None:
                buf += f3.payload
            else:
                continue
            if not (f3.flags & P.FF.MORE) and buf is not None:
                got, buf = bytes(buf), None
                break
        if got:
            break
    check("拿到 BIN 块（含重组）", got is not None)
    if got:
        cnt = got[10]
        mask = got[11]
        fmt = got[13]
        check("BIN fmt=4", fmt == P.FMT.BIN16, fmt)
        nch = bin(mask).count("1")
        check("BIN 块长 = 14 + 桶数x通道x16",
              len(got) == 14 + cnt * nch * 16,
              "len=%d cnt=%d nch=%d" % (len(got), cnt, nch))

    # ---- 12. 情景 ----
    print("[12] 情景库")
    from pdl import scenarios as SC
    from pdl.config import PowerCfg
    mc = PowerCfg(idx=0, model="motor", v_set=12.0, i_set=2.0, period_s=6.0, r_int=0.08)
    cur = [SC.evaluate("motor", t * 0.1, mc)[1] for t in range(60)]
    check("电机：有加速冲击", max(cur) > 3.0, "%.2f" % max(cur))
    check("电机：有减速回馈（负电流）", min(cur) < 0, "%.2f" % min(cur))

    cc = PowerCfg(idx=0, model="charge", v_set=24.0, i_set=1.5, v_min=18.0, tau_s=8.0)
    v0, i0 = SC.evaluate("charge", 0.0, cc)
    v8, i8 = SC.evaluate("charge", 7.9, cc)
    v20, i20 = SC.evaluate("charge", 20.0, cc)
    check("充电：CC 段电压上升", v8 > v0 + 4, "%.1f→%.1f" % (v0, v8))
    check("充电：CV 段电压钳位", abs(v20 - 24.0) < 0.01)
    check("充电：CV 段电流衰减", i20 < i0 * 0.4, "%.2f" % i20)

    dc_ = PowerCfg(idx=0, model="discharge", v_set=48.0, i_set=0.8, v_min=42.0, tau_s=20.0)
    check("放电：电压单调下降",
          SC.evaluate("discharge", 18.0, dc_)[0] < SC.evaluate("discharge", 2.0, dc_)[0])

    cap = PowerCfg(idx=0, model="cap_charge", v_set=5.0, i_set=1.0, cap_mf=10.0)
    vc1 = SC.evaluate("cap_charge", 0.01, cap)[0]
    vc2 = SC.evaluate("cap_charge", 0.10, cap)[0]
    check("电容充电：电压上升", vc2 > vc1, "%.3f→%.3f" % (vc1, vc2))

    # ---- 13. 热模型 ----
    print("[13] 热模型（功率 -> 温度）")
    from pdl.thermal import ThermalModel
    tm = ThermalModel(t0=25.0, ambient=25.0, r_th=3.0, tau=30.0)
    check("稳态 = 环境 + P×R_th", abs(tm.steady_state(10.0) - 55.0) < 0.01,
          "%.1f" % tm.steady_state(10.0))
    for _ in range(3000):
        tm.update(0.1, 10.0)
    check("300 s 后到达稳态", 54.9 < tm.t < 55.0, "%.1f" % tm.t)
    tm3 = ThermalModel(t0=25.0, ambient=25.0, r_th=3.0, tau=30.0)
    for _ in range(300):
        tm3.update(0.1, 10.0)
    check("30 s 时约 63%", abs(tm3.t - (25.0 + 30.0 * 0.632)) < 0.5, "%.1f" % tm3.t)
    tm2 = ThermalModel(t0=25.0, ambient=25.0, r_th=3.0, tau=30.0, enabled=False)
    tm2.update(60.0, 10.0)
    check("关掉耦合则温度不动", abs(tm2.t - 25.0) < 1e-6)

    print("-" * 52)
    if fails:
        print("失败 %d 项：%s" % (len(fails), ", ".join(fails)))
        return 1
    print("全部通过。")
    return 0


# ====================================================================== main
def main(argv=None):
    ap = argparse.ArgumentParser(description="PDL 功率数据记录仪 · 下位机模拟器")
    ap.add_argument("--listen", default="127.0.0.1:5555", help="TCP 监听地址")
    ap.add_argument("--selftest", action="store_true", help="只跑协议自检，不开界面")
    ap.add_argument("--headless", action="store_true", help="无界面运行（配合上位机/CI）")
    ap.add_argument("--hid-pacing", default="on", choices=["on", "off"],
                    help="模拟 USB FS HID 的 64B/1ms 限速（默认 on）")
    ap.add_argument("--time-scale", type=float, default=1.0, help="时钟加速倍数")
    ap.add_argument("--ram", type=int, default=10240, help="模拟的 RAM 字节数（仅记录用）")
    ap.add_argument("--fault-temp-lost", type=int, default=-1, help="指定温度通道掉线 0/1")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    host, _, port = args.listen.partition(":")
    port = int(port or 5555)

    dev = Device()
    if args.fault_temp_lost >= 0:
        dev.sensors[args.fault_temp_lost].present = False
        dev.temp[args.fault_temp_lost].present = False

    link = TcpLink(host, port, hid_pacing=(args.hid_pacing == "on"))
    runner = Runner(dev, link, time_scale=args.time_scale)
    link.on_frame = runner.on_frame
    link.start()

    if args.headless:
        print("headless 模式，Ctrl+C 退出")
        try:
            runner.run_forever()
        except KeyboardInterrupt:
            pass
        return 0

    from gui.app import SimApp
    app = SimApp(dev, link, runner)
    try:
        app.mainloop()
    finally:
        link.stop()
        runner.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
