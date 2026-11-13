using System;
using PDL_upper.Models.Protocol;

namespace PDL_upper.Models
{
    /// <summary>
    /// 把设备发来的块解码成工程单位，喂给曲线缓冲和统计。
    /// 全部在 IO 线程回调里跑，不做任何 UI 操作。
    /// </summary>
    public sealed class AcquisitionService
    {
        public readonly ChannelConfig[] Power = new ChannelConfig[4];
        public readonly ChannelLive[] Live = { new(), new(), new(), new() };
        public readonly TempLive[] Temp = { new(), new() };
        public readonly ChartBuffer Chart = new();

        public long TotalTicks;
        public long TotalBlocks;
        public long Gaps;
        public long DroppedTicks;
        public uint LastSampleIndex = uint.MaxValue;
        public double ChartDiv = 4.0;         // 曲线抽稀：每 N 个 tick 取 1 个点
        public double ChartTickMs = 4.0;      // 曲线节拍（ms）——ACQUIRE 时按实际速率刷新
        public double TempPeriodMs = 1000;    // 温度采样周期（协议默认 1 s）
        /// <summary>
        /// 全程历史门控：触发模式下由 TRIG_STARTED/TRIG_STOPPED 事件开关，
        /// 只有触发窗口内的数据才进 FullHistory（触发前/等待期不记录）。
        /// 手动/定时模式恒 true。原始块仍照常写 .pdl spool，不丢数据。
        /// </summary>
        public bool GateFullHistory = true;
        public readonly FullHistory[] FullV = NewHistory();
        public readonly FullHistory[] FullI = NewHistory();

        static FullHistory[] NewHistory()
            => new[] { new FullHistory(), new FullHistory(),
                       new FullHistory(), new FullHistory() };

        public void SetConfig(ChannelConfig[] cfgs)
        {
            for (int i = 0; i < Math.Min(4, cfgs.Length); i++)
                Power[i] = cfgs[i];
        }

        public void HandleBlock(byte[] payload)
        {
            if (payload.Length < Pdl.BlockHdr) return;
            var h = BlockHeader.Parse(payload);

            if ((h.Flags & Pdl.BF.TempBlock) != 0)
            {
                HandleTemp(h, payload);
                return;
            }

            // sample_index 连续性：跳跃就是丢数据
            if (LastSampleIndex != uint.MaxValue)
            {
                uint expect = LastSampleIndex + 1;
                if (h.SampleIndex != expect)
                    Gaps += h.SampleIndex > expect ? (h.SampleIndex - expect) : 1;
            }
            LastSampleIndex = h.SampleIndex + h.Count;
            TotalBlocks++;
            TotalTicks += h.Count;

            if (h.Fmt == Pdl.Fmt.Raw16)
                HandleRaw(h, payload);
            else if (h.Fmt == Pdl.Fmt.Bin16)
                HandleBin(h, payload);
        }

        private void HandleRaw(BlockHeader h, byte[] p)
        {
            int chans = PopCount(h.ChannelMask);
            int rec = chans * 4;
            int off = Pdl.BlockHdr;

            for (int t = 0; t < h.Count; t++)
            {
                int ci = 0;
                for (int ch = 0; ch < 4; ch++)
                {
                    if ((h.ChannelMask >> ch & 1) != 0)
                    {
                        if (off + 4 > p.Length) return;
                        short vRaw = Pdl.I16(p, off);
                        short iRaw = Pdl.I16(p, off + 2);
                        off += 4;
                        FeedChannel(ch, vRaw, iRaw);
                        ci++;
                    }
                }
                if (ci != chans) return;
            }
        }

        private void HandleBin(BlockHeader h, byte[] p)
        {
            int off = Pdl.BlockHdr;
            for (int ch = 0; ch < 4; ch++)
            {
                if ((h.ChannelMask >> ch & 1) == 0) continue;
                if (off + 16 > p.Length) return;
                // 桶只给 min/max/avg/rms，曲线用 avg 铺底、min/max 画包络更真实；
                // MVP 里先画 avg，包络留给下一轮。
                double vAvgMv = Pdl.I16(p, off + 4) * LsbMv(ch);
                double iAvgMa = Pdl.I16(p, off + 12) * LsbMa(ch);
                off += 16;
                Live[ch].OnTick(vAvgMv, iAvgMa);
                if (TotalTicks % ChartDiv == 0)
                    Chart.Add(ch, vAvgMv / 1000.0, iAvgMa / 1000.0);
                if (GateFullHistory)
                {
                    FullV[ch].Add(vAvgMv / 1000.0);
                    FullI[ch].Add(iAvgMa / 1000.0);
                }
                TotalTicks++;
            }
        }

        private void FeedChannel(int ch, short vRaw, short iRaw)
        {
            var cfg = Power[ch] ?? new ChannelConfig();
            double vMv = (vRaw - cfg.VOff) * cfg.LsbMv;
            double iMa = (iRaw - cfg.IOff) * cfg.LsbMa;
            Live[ch].OnTick(vMv, iMa);
            if (TotalTicks % ChartDiv == 0)
                Chart.Add(ch, vMv / 1000.0, iMa / 1000.0);
            if (GateFullHistory)
            {
                FullV[ch].Add(vMv / 1000.0);
                FullI[ch].Add(iMa / 1000.0);
            }
        }

        private void HandleTemp(BlockHeader h, byte[] p)
        {
            int off = Pdl.BlockHdr;
            for (int ch = 0; ch < 2; ch++)
            {
                if ((h.ChannelMask >> ch & 1) == 0) continue;
                if (off + 2 > p.Length) return;
                short c16 = Pdl.I16(p, off);
                off += 2;
                // 块内点间隔按协议温度周期（默认 1 s）递推；0x7FFF 会标成"探头不存在"
                Temp[ch].Set(c16, h.TFirstMs);   // 每块 1 点，时间 = 块头 t_first_ms
            }
        }

        private double LsbMv(int ch) => Power[ch]?.LsbMv ?? 1.0;
        private double LsbMa(int ch) => Power[ch]?.LsbMa ?? 1.0;

        private static int PopCount(int v)
        {
            int n = 0;
            while (v != 0) { n += v & 1; v >>= 1; }
            return n;
        }

        public void ResetStats()
        {
            TotalTicks = TotalBlocks = Gaps = DroppedTicks = 0;
            LastSampleIndex = uint.MaxValue;
            Chart.Clear();
            foreach (var l in Live) l.Reset();
            foreach (var f in FullV) f.Clear();     // 新会话 = 全程历史从零开始
            foreach (var f in FullI) f.Clear();
        }
    }
}
