using System;

namespace PDL_upper.Models.Protocol
{
    /// <summary>采集参数 —— 对应 ACQUIRE (0x20) 的 48 B 载荷。</summary>
    public sealed class AcquireParams
    {
        public byte PowerMask = 0x0F;
        public byte TempMask = 0x03;
        public byte RecordMode = Pdl.RecordMode.Manual;
        public byte Tier = 0;
        public uint SampleRateHz = 1000;
        public uint ReportIntervalMs = 100;
        public uint BinMs = 0;
        public uint DurationS = 0;
        public uint TempIntervalMs = 1000;
        public byte TrigSource = 0;          // 0=V 1=I 2=P
        public byte TrigCh = 0;
        public bool TrigStartEn = true;
        public bool TrigStopEn = true;
        public bool TrigAutoRearm = false;
        public ushort StartHoldMs = 50;
        public ushort StopHoldMs = 50;
        public int StartVal = 12000;         // mV / mA / mW
        public int StopVal = 8000;
        public uint PostTriggerMs = 0;
        public uint PreTriggerMs = 0;        // 预留，必须 0

        public byte[] Build()
        {
            var p = new byte[48];
            p[0] = PowerMask;
            p[1] = TempMask;
            p[2] = RecordMode;
            p[3] = Tier;
            WriteU32(p, 4, SampleRateHz);
            WriteU32(p, 8, ReportIntervalMs);
            WriteU32(p, 12, BinMs);
            WriteU32(p, 16, DurationS);
            WriteU32(p, 20, TempIntervalMs);
            p[24] = TrigSource;
            p[25] = TrigCh;
            byte tf = 0;
            if (TrigStartEn) tf |= 0x01;
            if (TrigStopEn) tf |= 0x02;
            if (TrigAutoRearm) tf |= 0x04;
            p[26] = tf;
            p[27] = 12;                          // temp_res_bits
            WriteU16(p, 28, StartHoldMs);
            WriteU16(p, 30, StopHoldMs);
            WriteI32(p, 32, StartVal);
            WriteI32(p, 36, StopVal);
            WriteU32(p, 40, PostTriggerMs);
            WriteU32(p, 44, PreTriggerMs);
            return p;
        }

        public string Validate()
        {
            if (PowerMask == 0 || (PowerMask & 0xF0) != 0) return "功率通道掩码非法";
            if ((TempMask & 0xFC) != 0) return "温度掩码非法";
            if (SampleRateHz < 1 || SampleRateHz > Pdl.MaxRate) return "采样率超出范围";
            if (ReportIntervalMs < 1 || ReportIntervalMs > 60000) return "上报间隔超出范围";
            long ticks = (long)SampleRateHz * ReportIntervalMs / 1000;
            if (ticks < 1 || ticks > Pdl.MaxBlockTicks) return "采样率×间隔超出单块上限";
            if (RecordMode == Pdl.RecordMode.Trigger && (TrigCh > 3)) return "触发通道非法";
            if (StartHoldMs == 0) return "触发消抖不能为 0（会被尖峰打穿）";
            return "";
        }

        public static void WriteU16(byte[] b, int o, int v) { b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); }
        public static void WriteU32(byte[] b, int o, uint v)
        {
            b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); b[o + 2] = (byte)(v >> 16); b[o + 3] = (byte)(v >> 24);
        }
        public static void WriteI32(byte[] b, int o, int v) => WriteU32(b, o, (uint)v);
    }

    /// <summary>通道配置 —— GET_CH_CONFIG / SET_CH_CONFIG 的 28 B。</summary>
    public sealed class ChannelConfig
    {
        public byte Ch;
        public bool Enabled = true;
        public byte SensorType;
        public byte TempResBits = 12;
        public uint VNum = 346, VDen = 100;
        public uint INum = 2441, IDen = 1000;
        public short VOff;
        public short IOff = 2048;            // 0 A 在 ADC 中点
        public ushort FsCurrentMa = 5000;
        public ushort VFsDv = 142;

        public double FullScaleV => VFsDv / 10.0;
        public double LsbMv => VNum / (double)VDen;
        public double LsbMa => INum / (double)IDen;

        public static ChannelConfig Parse(byte[] p, int off)
        {
            return new ChannelConfig
            {
                Ch = p[off],
                Enabled = p[off + 1] != 0,
                SensorType = p[off + 2],
                TempResBits = p[off + 3],
                VNum = Pdl.U32(p, off + 4),
                VDen = Pdl.U32(p, off + 8),
                INum = Pdl.U32(p, off + 12),
                IDen = Pdl.U32(p, off + 16),
                VOff = Pdl.I16(p, off + 20),
                IOff = Pdl.I16(p, off + 22),
                FsCurrentMa = Pdl.U16(p, off + 24),
                VFsDv = Pdl.U16(p, off + 26),
            };
        }
    }

    public sealed class IdentifyAck
    {
        public string Magic = "";
        public ushort ProtoMin, ProtoMax, FwVersion, HwRev;
        public byte PowerChannels, TempChannels, Caps, MaxReportSize;
        public ushort MaxSampleRateHz, MaxBlockTicks;
        public ushort[] FsCurrentMa = Array.Empty<ushort>();
        public uint Nonce;
        public bool PerChannelRange => (Caps & 0x80) != 0;

        public static IdentifyAck Parse(byte[] extra)
        {
            var a = new IdentifyAck
            {
                Magic = System.Text.Encoding.ASCII.GetString(extra, 0, 4),
                ProtoMin = Pdl.U16(extra, 4),
                ProtoMax = Pdl.U16(extra, 6),
                FwVersion = Pdl.U16(extra, 8),
                HwRev = Pdl.U16(extra, 10),
                PowerChannels = extra[12],
                TempChannels = extra[13],
                Caps = extra[14],
                MaxReportSize = extra[15],
                MaxSampleRateHz = Pdl.U16(extra, 16),
                MaxBlockTicks = Pdl.U16(extra, 18),
                Nonce = Pdl.U32(extra, 28),
            };
            a.FsCurrentMa = new ushort[a.PowerChannels];
            for (int i = 0; i < a.PowerChannels; i++)
                a.FsCurrentMa[i] = Pdl.U16(extra, 20 + i * 2);
            return a;
        }

        public string FwText => $"{FwVersion >> 8}.{FwVersion & 0xFF}";
    }

    public sealed class AcquireAck
    {
        public byte State, EffTier;
        public ushort EffBinMs, EffTempIntervalMs, EffTempRes;
        public uint EffSampleRateHz, EffReportIntervalMs, FirstSampleIndex;

        public static AcquireAck Parse(byte[] extra)
        {
            return new AcquireAck
            {
                State = extra[0],
                EffTier = extra[1],
                EffBinMs = Pdl.U16(extra, 2),
                EffSampleRateHz = Pdl.U32(extra, 4),
                EffReportIntervalMs = Pdl.U32(extra, 8),
                FirstSampleIndex = Pdl.U32(extra, 12),
                EffTempIntervalMs = Pdl.U16(extra, 16),
                EffTempRes = extra[18],
            };
        }
    }

    public sealed class AbortAck
    {
        public byte FinalState, StopReason;
        public uint TotalTicks, TotalBlocks, DroppedTicks, DurationMs, TrigCount;

        public static AbortAck Parse(byte[] extra)
        {
            return new AbortAck
            {
                FinalState = extra[0],
                StopReason = extra[1],
                TotalTicks = Pdl.U32(extra, 4),
                TotalBlocks = Pdl.U32(extra, 8),
                DroppedTicks = Pdl.U32(extra, 12),
                DurationMs = Pdl.U32(extra, 16),
                TrigCount = Pdl.U32(extra, 20),
            };
        }

        public string ReasonText => StopReason switch
        {
            0 => "主机中止",
            1 => "到时长自动停止",
            2 => "触发关",
            3 => "设备故障",
            _ => $"0x{StopReason:X2}",
        };
    }

    public sealed class StatusAck
    {
        public byte State, PowerMask, TempMask, ErrorFlags;
        public uint SampleRateHz, ReportIntervalMs, TotalTicks, TotalBlocks, OverrunBlocks, DroppedTicks;
        public ushort BufferLevel, BufferCapacity;
        public short TempC10;
        public uint UptimeS;
        public ushort TrigState;
        public short[] TempC16 = new short[2];

        public static StatusAck Parse(byte[] extra)
        {
            var a = new StatusAck
            {
                State = extra[0],
                PowerMask = extra[1],
                TempMask = extra[2],
                ErrorFlags = extra[3],
                SampleRateHz = Pdl.U32(extra, 4),
                ReportIntervalMs = Pdl.U32(extra, 8),
                TotalTicks = Pdl.U32(extra, 12),
                TotalBlocks = Pdl.U32(extra, 16),
                OverrunBlocks = Pdl.U32(extra, 20),
                DroppedTicks = Pdl.U32(extra, 24),
                BufferLevel = Pdl.U16(extra, 28),
                BufferCapacity = Pdl.U16(extra, 30),
                TempC10 = Pdl.I16(extra, 32),
                UptimeS = Pdl.U32(extra, 34),
                TrigState = Pdl.U16(extra, 38),
            };
            a.TempC16 = new short[2] { Pdl.I16(extra, 40), Pdl.I16(extra, 42) };
            return a;
        }
    }

    public sealed class TimeSyncAck
    {
        public ulong HostMonoUs, DeviceMonoUs;
        public static TimeSyncAck Parse(byte[] extra) =>
            new() { HostMonoUs = Pdl.U64(extra, 0), DeviceMonoUs = Pdl.U64(extra, 8) };
    }
}
