using System;

namespace PDL_upper.Models.Protocol
{
    /// <summary>协议常量与字节层工具 —— 与 simulator/pdl/proto.py 严格对应（PDL/1.4）。</summary>
    public static class Pdl
    {
        public const byte Sof = 0xA5;
        public const byte Ver = 0x01;
        public const int Report = 64;
        public const int Hdr = 8;
        public const int Crc = 2;
        public const int MaxPayload = Report - Hdr - Crc;   // 54

        public const ushort MaxRate = 20000;
        public const ushort MaxBlockTicks = 1024;
        public const int BlockHdr = 14;

        public const uint Magic = 0x314C4450;               // "PDL1" 按 u32 读

        public static class T
        {
            public const byte Identify = 0x10;
            public const byte GetChConfig = 0x11;
            public const byte Acquire = 0x20;
            public const byte ReadTemp = 0x21;
            public const byte Abort = 0x22;
            public const byte GetStatus = 0x23;
            public const byte SetChConfig = 0x30;
            public const byte Zero = 0x31;
            public const byte SaveConfig = 0x32;
            public const byte TimeSync = 0x40;
            public const byte Reset = 0x60;
            public const byte Echo = 0x7F;
            public const byte Ack = 0x80;
            public const byte Nack = 0x81;
            public const byte Data = 0x90;
            public const byte Event = 0xA0;
            public const byte Heartbeat = 0xA1;
        }

        public static class FF
        {
            public const byte Resp = 0x01;
            public const byte Event = 0x02;
            public const byte First = 0x04;
            public const byte More = 0x08;
        }

        public static class BF
        {
            public const byte Saturated = 0x01;
            public const byte Overrun = 0x02;
            public const byte CfgChanged = 0x04;
            public const byte First = 0x08;
            public const byte CalInvalid = 0x10;
            public const byte Triggered = 0x20;
            public const byte Final = 0x40;
            public const byte TempBlock = 0x80;
        }

        public static class Fmt
        {
            public const byte Raw16 = 0;
            public const byte Raw32 = 1;
            public const byte UvUa32 = 2;
            public const byte MvMa16 = 3;
            public const byte Bin16 = 4;
            public const byte Bin32 = 5;
            public const byte Temp16 = 7;
        }

        public static class St
        {
            public const byte Ok = 0x00;
            public const byte UnknownCmd = 0x01;
            public const byte BadParam = 0x02;
            public const byte BadState = 0x03;
            public const byte Busy = 0x04;
            public const byte CrcError = 0x05;
            public const byte LenError = 0x06;
            public const byte Unsupported = 0x07;
            public const byte NotCalibrated = 0x08;
            public const byte Overflow = 0x09;
            public const byte NoData = 0x0A;
            public const byte Internal = 0x0F;
        }

        public static class DevState
        {
            public const byte Boot = 0x00;
            public const byte Idle = 0x01;
            public const byte Armed = 0x02;
            public const byte Running = 0x03;
            public const byte Draining = 0x04;
            public const byte Error = 0x05;

            public static string Name(byte s) => s switch
            {
                Boot => "BOOT",
                Idle => "空闲",
                Armed => "待触发",
                Running => "采集中",
                Draining => "收尾中",
                Error => "故障",
                _ => $"0x{s:X2}",
            };
        }

        public static class Ev
        {
            public const byte Overrun = 0x01;
            public const byte Saturate = 0x02;
            public const byte Overtemp = 0x03;
            public const byte CalInvalid = 0x04;
            public const byte SelfTestFail = 0x05;
            public const byte CfgApplied = 0x06;
            public const byte LogFull = 0x07;
            public const byte UsbReset = 0x08;
            public const byte HwFault = 0x09;
            public const byte TrigArmed = 0x0A;
            public const byte TrigStarted = 0x0B;
            public const byte TrigStopped = 0x0C;
            public const byte AutoStop = 0x0D;
            public const byte Aborted = 0x0E;
            public const byte TempLost = 0x0F;
        }

        public static class RecordMode
        {
            public const byte Manual = 0;
            public const byte Timed = 1;
            public const byte Trigger = 2;
        }

        // ------------------------------------------------------------ 小端读取
        public static ushort U16(byte[] b, int o) => (ushort)(b[o] | b[o + 1] << 8);
        public static short I16(byte[] b, int o) => (short)U16(b, o);
        public static uint U32(byte[] b, int o) =>
            (uint)(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24);
        public static int I32(byte[] b, int o) => (int)U32(b, o);
        public static ulong U64(byte[] b, int o)
        {
            ulong v = 0;
            for (int i = 7; i >= 0; i--) v = (v << 8) | b[o + i];
            return v;
        }

        // ------------------------------------------------------------ CRC
        private static readonly ushort[] CrcTable = BuildTable();

        private static ushort[] BuildTable()
        {
            var t = new ushort[256];
            for (uint i = 0; i < 256; i++)
            {
                uint c = i << 8;
                for (int k = 0; k < 8; k++)
                    c = (c & 0x8000) != 0 ? (c << 1) ^ 0x1021 : c << 1;
                t[i] = (ushort)(c & 0xFFFF);
            }
            return t;
        }

        public static ushort Crc16(byte[] data, int len)
        {
            ushort c = 0xFFFF;
            for (int i = 0; i < len; i++)
                c = (ushort)(((c << 8) & 0xFFFF) ^ CrcTable[((c >> 8) ^ data[i]) & 0xFF]);
            return c;
        }

        // ------------------------------------------------------------ 帧
        public static byte[] EncodeFrame(byte type, byte flags, ushort seq, byte[] payload)
        {
            if (payload.Length > MaxPayload)
                throw new ArgumentException($"payload {payload.Length} > {MaxPayload}");
            var f = new byte[Report];
            f[0] = Sof; f[1] = Ver; f[2] = type; f[3] = flags;
            f[4] = (byte)(payload.Length & 0xFF);
            f[5] = (byte)(payload.Length >> 8);
            f[6] = (byte)(seq & 0xFF);
            f[7] = (byte)(seq >> 8);
            Array.Copy(payload, 0, f, Hdr, payload.Length);
            ushort c = Crc16(f, Hdr + payload.Length);
            f[Hdr + payload.Length] = (byte)(c & 0xFF);
            f[Hdr + payload.Length + 1] = (byte)(c >> 8);
            return f;
        }
    }

    public sealed record Frame(byte Type, byte Flags, ushort Seq, byte[] Payload)
    {
        public override string ToString() => $"Frame(0x{Type:X2} f=0x{Flags:X2} seq={Seq} {Payload.Length}B)";
    }

    /// <summary>流式解码：TCP 一次 recv 可能不足 64 B，也可能一次来多帧。</summary>
    public sealed class FrameDecoder
    {
        private readonly byte[] _buf = new byte[Pdl.Report * 512];   // 32 KB：一次 recv 可能带一整批帧
        private int _len;

        public int BadFrames { get; private set; }

        /// <summary>喂入原始字节，吐出完整帧。</summary>
        public System.Collections.Generic.List<Frame> Feed(byte[] data, int count)
        {
            var list = new System.Collections.Generic.List<Frame>();
            if (_len + count > _buf.Length)
            {
                // 缓冲不够（理论上到不了）→ 找最后一个 SOF 重新同步，而不是整段丢弃
                int keep = 0;
                for (int i = Math.Max(0, _len - Pdl.Report); i < _len; i++)
                    if (_buf[i] == Pdl.Sof) { keep = _len - i; break; }
                Array.Copy(_buf, _len - keep, _buf, 0, keep);
                _len = keep;
            }
            Array.Copy(data, 0, _buf, _len, count);
            _len += count;

            while (_len >= Pdl.Report)
            {
                var f = Parse(_buf);
                Array.Copy(_buf, Pdl.Report, _buf, 0, _len - Pdl.Report);
                _len -= Pdl.Report;
                if (f != null) list.Add(f); else BadFrames++;
            }
            return list;
        }

        private static Frame? Parse(byte[] raw)
        {
            if (raw[0] != Pdl.Sof || raw[1] != Pdl.Ver) return null;
            int len = raw[4] | raw[5] << 8;
            if (len > Pdl.MaxPayload) return null;
            int end = Pdl.Hdr + len;
            ushort got = (ushort)(raw[end] | raw[end + 1] << 8);
            if (Pdl.Crc16(raw, end) != got) return null;
            var pl = new byte[len];
            Array.Copy(raw, Pdl.Hdr, pl, 0, len);
            return new Frame(raw[2], raw[3], (ushort)(raw[6] | raw[7] << 8), pl);
        }
    }

    // ------------------------------------------------------------ 数据块
    public sealed class BlockHeader
    {
        public uint SampleIndex;
        public uint TFirstMs;
        public ushort BlockSeq;
        public byte Count;
        public byte ChannelMask;
        public byte Flags;
        public byte Fmt;

        public static BlockHeader Parse(byte[] p)
        {
            return new BlockHeader
            {
                SampleIndex = Pdl.U32(p, 0),
                TFirstMs = Pdl.U32(p, 4),
                BlockSeq = Pdl.U16(p, 8),
                Count = p[10],
                ChannelMask = p[11],
                Flags = p[12],
                Fmt = p[13],
            };
        }
    }

    public sealed record PdlEvent(byte Code, byte Ch, ushort Param, uint TMs);
}
