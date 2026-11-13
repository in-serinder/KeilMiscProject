using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using PDL_upper.Models.Protocol;

namespace PDL_upper.Models.PdlIO
{
    /// <summary>文件结构（docs/04 §9）：Header + Record* + [IDX marker] + Index + Footer，追加写。</summary>
    public static class RecType
    {
        public const byte BlockRaw = 0x01;
        public const byte BlockBin = 0x02;
        public const byte BlockTemp = 0x03;
        public const byte Event = 0x04;
        public const byte IdxMarker = 0x05;      // 记录区结束标记，payload "IDX"
    }

    // ---------------------------------------------------------------- CRC32
    public static class Crc32
    {
        static readonly uint[] Table = Build();

        static uint[] Build()
        {
            var t = new uint[256];
            for (uint i = 0; i < 256; i++)
            {
                uint c = i;
                for (int k = 0; k < 8; k++)
                    c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[i] = c;
            }
            return t;
        }

        const uint Init = 0xFFFFFFFFu;

        /// <summary>链式更新（state 初值必须为 ChainSeed）。</summary>
        public static uint Chain(uint state, byte[] data, int offset, int count)
        {
            uint c = state;
            for (int i = offset; i < offset + count; i++)
                c = Table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
            return c;
        }

        public static uint Final(uint state) => ~state;

        public static uint ChainSeed => Init;

        public static uint Compute(byte[] data, int offset, int count)
            => Final(Chain(Init, data, offset, count));
    }

    // ---------------------------------------------------------------- Header
    public sealed class PdlFileHeader
    {
        public DateTime StartedUtc = DateTime.UtcNow;
        public uint SampleRateHz = 1000;
        public uint ReportIntervalMs = 100;
        public uint BinMs;
        public byte Tier;
        public byte RecordMode;
        public string DeviceInfo = "";
        public List<ChannelConfig> Power = new();

        public byte[] Encode()
        {
            using var ms = new MemoryStream();
            using var w = new BinaryWriter(ms);
            w.Write("PDLF"u8);
            w.Write((ushort)1);                       // ver
            w.Write((long)(StartedUtc - DateTime.UnixEpoch).TotalMilliseconds);
            w.Write(SampleRateHz);
            w.Write(ReportIntervalMs);
            w.Write(BinMs);
            w.Write(Tier);
            w.Write(RecordMode);
            var dev = Encoding.UTF8.GetBytes(DeviceInfo);
            w.Write(dev.Length);
            w.Write(dev);
            w.Write((byte)Power.Count);
            foreach (var c in Power)
            {
                w.Write(c.Ch);
                w.Write(c.VFsDv);
                w.Write(c.FsCurrentMa);
                w.Write(c.VNum); w.Write(c.VDen);
                w.Write(c.INum); w.Write(c.IDen);
                w.Write(c.VOff); w.Write(c.IOff);
            }
            return ms.ToArray();
        }

        public static PdlFileHeader Decode(byte[] buf)
        {
            var h = new PdlFileHeader();
            using var ms = new MemoryStream(buf);
            using var r = new BinaryReader(ms);
            if (r.ReadBytes(4) is not [0x50, 0x44, 0x4C, 0x46])   // "PDLF"
                throw new InvalidDataException("不是 PDL 文件（magic 不符）");
            r.ReadUInt16();                                       // ver
            h.StartedUtc = DateTime.UnixEpoch
                .AddMilliseconds(r.ReadInt64()).ToLocalTime();
            h.SampleRateHz = r.ReadUInt32();
            h.ReportIntervalMs = r.ReadUInt32();
            h.BinMs = r.ReadUInt32();
            h.Tier = r.ReadByte();
            h.RecordMode = r.ReadByte();
            int devLen = r.ReadInt32();
            h.DeviceInfo = Encoding.UTF8.GetString(r.ReadBytes(devLen));
            int n = r.ReadByte();
            for (int i = 0; i < n; i++)
            {
                h.Power.Add(new ChannelConfig
                {
                    Ch = r.ReadByte(),
                    VFsDv = r.ReadUInt16(),
                    FsCurrentMa = r.ReadUInt16(),
                    VNum = r.ReadUInt32(), VDen = r.ReadUInt32(),
                    INum = r.ReadUInt32(), IDen = r.ReadUInt32(),
                    VOff = r.ReadInt16(), IOff = r.ReadInt16(),
                });
            }
            return h;
        }
    }

    /// <summary>保存时写入 Footer 的汇总。</summary>
    public sealed class PdlSummary
    {
        public uint RecordCount;
        public uint BlockCount;
        public ulong DurationMs;
        public uint FirstSampleIndex, LastSampleIndex;
        public long FileBytes;
    }

    // ---------------------------------------------------------------- Writer
    public sealed class PdlFileWriter : IDisposable
    {
        FileStream? _fs;
        BinaryWriter? _bw;
        uint _crc;                       // Header+Records 的滚动 CRC
        long _records, _blocks;
        uint _firstIdx = uint.MaxValue, _lastIdx;
        uint _firstT = uint.MaxValue, _lastT;
        readonly List<(long off, uint idx, uint t, bool temp)> _index = new();

        public string FilePath { get; private set; } = "";
        public bool Active { get; private set; }
        public long BytesWritten => _fs?.Length ?? 0;

        public static string NewSpoolPath()
        {
            var dir = Path.Combine(Path.GetTempPath(), "PDL_session");
            Directory.CreateDirectory(dir);
            return Path.Combine(dir,
                $"PDL_{DateTime.Now:yyyyMMdd_HHmmss}.pdl.part");
        }

        public void Start(string path, PdlFileHeader header)
        {
            FilePath = path;
            _fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read, 64 << 10);
            _bw = new BinaryWriter(_fs);
            var hb = header.Encode();
            _bw.Write(hb);
            _crc = Crc32.ChainSeed;
            _crc = Crc32.Chain(_crc, hb, 0, hb.Length);
            Active = true;
        }

        /// <summary>写入一个块（协议块 payload，含 14 B 块头）。</summary>
        public void AppendBlock(byte[] payload)
        {
            if (!Active || _bw == null || payload.Length < 14) return;
            var h = BlockHeader.Parse(payload);
            bool isTemp = (h.Flags & Pdl.BF.TempBlock) != 0 || h.Fmt == Pdl.Fmt.Temp16;
            byte type = isTemp ? RecType.BlockTemp
                      : h.Fmt == Pdl.Fmt.Bin16 ? RecType.BlockBin
                      : RecType.BlockRaw;
            WriteRecord(type, payload);
            _index.Add((_fs!.Position - 4, h.SampleIndex, h.TFirstMs, isTemp)); // 减去 crc 4B 后的记录起始
            _blocks++;
            if (_firstIdx == uint.MaxValue) { _firstIdx = h.SampleIndex; _firstT = h.TFirstMs; }
            _lastIdx = h.SampleIndex + h.Count;
            _lastT = h.TFirstMs;
        }

        public void AppendEvent(byte code, byte ch, ushort param, uint tMs)
        {
            if (!Active || _bw == null) return;
            var p = new byte[8];              // code1 + ch1 + param2 + tMs4
            p[0] = code; p[1] = ch;
            p[2] = (byte)(param & 0xFF); p[3] = (byte)(param >> 8);
            p[4] = (byte)(tMs & 0xFF); p[5] = (byte)((tMs >> 8) & 0xFF);
            p[6] = (byte)((tMs >> 16) & 0xFF); p[7] = (byte)(tMs >> 24);
            WriteRecord(RecType.Event, p);
        }

        void WriteRecord(byte type, byte[] payload)
        {
            int len = payload.Length;
            var head = new byte[3] { type, (byte)(len & 0xFF), (byte)(len >> 8) };
            var crcBuf = new byte[3 + len];
            Array.Copy(head, crcBuf, 3);
            Array.Copy(payload, 0, crcBuf, 3, len);

            _bw!.Write(head);
            _bw.Write(payload);
            _bw.Write(Crc32.Compute(crcBuf, 0, crcBuf.Length));

            // 滚动 CRC 覆盖 Header+Records（Footer 里校验）
            _crc = Crc32.Chain(_crc, crcBuf, 0, crcBuf.Length);
            _records++;
        }

        /// <summary>写 Index + Footer，收尾。之后文件是完整的 .pdl（后缀仍是 .part，保存时改名/复制）。</summary>
        public PdlSummary Finalize()
        {
            if (!Active || _bw == null) return new PdlSummary();
            // 记录区结束标记
            var marker = Encoding.ASCII.GetBytes("IDX");
            WriteRecord(RecType.IdxMarker, marker);

            long indexStart = _fs!.Position;
            _bw.Write((uint)_index.Count);
            foreach (var (off, idx, t, temp) in _index)
            {
                _bw.Write(off);
                _bw.Write(idx);
                _bw.Write(t);
                _bw.Write(temp);
            }

            var s = new PdlSummary
            {
                RecordCount = (uint)_records,
                BlockCount = (uint)_blocks,
                DurationMs = _firstT == uint.MaxValue ? 0 : (ulong)((long)_lastT - _firstT),
                FirstSampleIndex = _firstIdx == uint.MaxValue ? 0 : _firstIdx,
                LastSampleIndex = _lastIdx,
                FileBytes = indexStart,
            };
            _bw.Write(s.RecordCount);
            _bw.Write(s.BlockCount);
            _bw.Write(s.DurationMs);
            _bw.Write(Crc32.Final(_crc));     // 文件 CRC = Header+Records
            _bw.Flush();
            _fs.Flush(flushToDisk: true);
            s.FileBytes = _fs.Length;
            _bw.Dispose(); _fs.Dispose();
            _bw = null; _fs = null;
            Active = false;
            return s;
        }

        /// <summary>关闭并删除 spool（用户选择不保存）。</summary>
        public void Discard()
        {
            try
            {
                _bw?.Dispose(); _fs?.Dispose();
                if (File.Exists(FilePath)) File.Delete(FilePath);
            }
            catch { /* 删不掉就留着，不阻塞退出 */ }
            _bw = null; _fs = null;
            Active = false;
        }

        public void Dispose() => Discard();
    }

    // ---------------------------------------------------------------- Reader
    public sealed class PdlIndexEntry
    {
        public long Offset;
        public uint SampleIndex, TMs;
        public bool IsTemp;
    }

    public sealed class PdlFileReader
    {
        public PdlFileHeader Header { get; set; } = new();
        public bool HeaderOk, FooterOk;
        public uint RecordCount, BlockCount;
        public ulong DurationMs;
        public readonly List<(BlockHeader h, byte[] payload)> PowerBlocks = new();
        public readonly List<(BlockHeader h, byte[] payload)> TempBlocks = new();
        public readonly List<PdlEvent> Events = new();
        public int Truncated;                       // 因坏记录停止时 = 已读到的不完整字节数

        public static PdlFileReader Open(string path)
        {
            var r = new PdlFileReader();
            var bytes = File.ReadAllBytes(path);

            // Header
            int pos = 0;
            // 先扫 header 长度：magic+ver+time+rate+interval+bin+tier+mode+devLen
            if (bytes.Length < 34) throw new InvalidDataException("文件太小");
            // magic4 + ver2 + time8 + rate4 + interval4 + bin4 + tier1 + mode1 = 28
            int devLen = BitConverter.ToInt32(bytes, 28);
            int hdrLen = 32 + devLen;
            int nPower = bytes[hdrLen];
            hdrLen += 1 + nPower * 25;   // 每通道 25 B：ch1+vFs2+fsMa2+4×4+off2+off2
            r.Header = PdlFileHeader.Decode(bytes[..hdrLen]);
            r.HeaderOk = true;
            pos = hdrLen;

            // Records：读到 IDX marker / 坏记录 / EOF
            while (pos + 3 <= bytes.Length)
            {
                byte type = bytes[pos];
                int len = bytes[pos + 1] | bytes[pos + 2] << 8;
                if (pos + 3 + len + 4 > bytes.Length)
                { r.Truncated = bytes.Length - pos; break; }
                var payload = bytes[(pos + 3)..(pos + 3 + len)];
                uint want = BitConverter.ToUInt32(bytes, pos + 3 + len);
                var crcBuf = new byte[3 + len];
                Array.Copy(bytes, pos, crcBuf, 0, 3);
                Array.Copy(payload, 0, crcBuf, 3, len);
                if (Crc32.Compute(crcBuf, 0, crcBuf.Length) != want)
                { r.Truncated = bytes.Length - pos; break; }

                if (type == RecType.IdxMarker) { pos += 3 + len + 4; break; }

                switch (type)
                {
                    case RecType.BlockRaw:
                    case RecType.BlockBin:
                        if (len >= 14)
                            r.PowerBlocks.Add((BlockHeader.Parse(payload), payload));
                        break;
                    case RecType.BlockTemp:
                        if (len >= 14)
                            r.TempBlocks.Add((BlockHeader.Parse(payload), payload));
                        break;
                    case RecType.Event:
                        if (len >= 8)
                            r.Events.Add(new PdlEvent(payload[0], payload[1],
                                (ushort)(payload[2] | payload[3] << 8),
                                BitConverter.ToUInt32(payload, 4)));
                        break;
                }
                pos += 3 + len + 4;
            }

            // Footer（可选，文件可能没保存就中断）
            try
            {
                // Footer 紧跟 Index；索引在 marker 之后：u32 count + count*17
                if (pos + 4 <= bytes.Length && bytes[pos] != 0 || true)
                {
                    int idxCount = BitConverter.ToInt32(bytes, pos);
                    if (idxCount >= 0 && idxCount < 10_000_000 && pos + 4 + idxCount * 17 + 20 <= bytes.Length)
                    {
                        pos += 4 + idxCount * 17;
                        r.RecordCount = BitConverter.ToUInt32(bytes, pos);
                        r.BlockCount = BitConverter.ToUInt32(bytes, pos + 4);
                        r.DurationMs = BitConverter.ToUInt64(bytes, pos + 8);
                        r.FooterOk = true;
                    }
                }
            }
            catch { /* 没有 footer 也能回放 */ }
            return r;
        }

        /// <summary>把块解码成每通道序列：mV / mA / 时间轴 ms（相对会话首块，从 0 起）；温度 ℃。</summary>
        public DecodedData Decode()
        {
            var d = new DecodedData();
            // 时间基准归一：设备时钟可能是"上电 ms"也可能是别的单调基准，
            // 统一减去会话首块时间戳，上位机视窗/缩放都工作在相对时间上。
            long tBase = long.MaxValue;
            foreach (var (h, _) in PowerBlocks) tBase = Math.Min(tBase, h.TFirstMs);
            foreach (var (h, _) in TempBlocks) tBase = Math.Min(tBase, h.TFirstMs);
            if (tBase == long.MaxValue) tBase = 0;
            d.TBaseMs = tBase;
            foreach (var (h, p) in PowerBlocks)
            {
                int nch = BitCount(h.ChannelMask);
                if (nch == 0) continue;
                if (h.Fmt == Pdl.Fmt.Raw16)
                {
                    d.RawBlocks++;
                    double dt = 1000.0 / Math.Max(1u, Header.SampleRateHz);
                    int off = 14;
                    for (int k = 0; k < h.Count && off + nch * 4 <= p.Length; k++)
                    {
                        double t = h.TFirstMs - tBase + k * dt;
                        for (int ch = 0; ch < 4; ch++)
                        {
                            if ((h.ChannelMask >> ch & 1) == 0) continue;
                            short v = (short)(p[off] | p[off + 1] << 8);
                            short i = (short)(p[off + 2] | p[off + 3] << 8);
                            d.V[ch].Add(v);
                            d.I[ch].Add(i);
                            d.T[ch].Add(t);
                            off += 4;
                        }
                    }
                }
                else if (h.Fmt == Pdl.Fmt.Bin16)
                {
                    d.BinBlocks++;
                    double dt = Header.BinMs > 0 ? Header.BinMs : 100;
                    int off = 14;
                    for (int k = 0; k < h.Count && off + nch * 16 <= p.Length; k++)
                    {
                        double t = h.TFirstMs - tBase + k * dt;
                        for (int ch = 0; ch < 4; ch++)
                        {
                            if ((h.ChannelMask >> ch & 1) == 0) continue;
                            short vmin = (short)(p[off] | p[off + 1] << 8);
                            short vmax = (short)(p[off + 2] | p[off + 3] << 8);
                            short vavg = (short)(p[off + 4] | p[off + 5] << 8);
                            short imin = (short)(p[off + 8] | p[off + 9] << 8);
                            short imax = (short)(p[off + 10] | p[off + 11] << 8);
                            short iavg = (short)(p[off + 12] | p[off + 13] << 8);
                            d.V[ch].Add(vavg); d.Vmin[ch].Add(vmin); d.Vmax[ch].Add(vmax);
                            d.I[ch].Add(iavg); d.Imax[ch].Add(imax);
                            d.T[ch].Add(t);
                            off += 16;
                        }
                    }
                }
            }
            foreach (var (h, p) in TempBlocks)
            {
                d.TempBlocks++;
                int nch = BitCount(h.ChannelMask);
                int off = 14;
                for (int k = 0; k < h.Count && off + nch * 2 <= p.Length; k++)
                {
                    double t = h.TFirstMs - tBase;
                    for (int ch = 0; ch < 2; ch++)
                    {
                        if ((h.ChannelMask >> ch & 1) == 0) continue;
                        short c = (short)(p[off] | p[off + 1] << 8);
                        d.Tv[ch].Add(c / 16.0);
                        d.Tt[ch].Add(t);
                        off += 2;
                    }
                }
            }
            return d;
        }

        static int BitCount(byte b)
        {
            int n = 0;
            while (b != 0) { n += b & 1; b >>= 1; }
            return n;
        }
    }

    public sealed class DecodedData
    {
        public long TBaseMs;         // 归一基准（会话首块的原始时间戳）
        public List<double>[] V = New4();        // mV
        public List<double>[] I = New4();        // mA
        public List<double>[] T = New4();        // ms
        public List<double>[] Vmin = New4();
        public List<double>[] Vmax = New4();
        public List<double>[] Imax = New4();
        public List<double>[] Tv = { new(), new() };   // ℃
        public List<double>[] Tt = { new(), new() };   // ms
        public int RawBlocks, BinBlocks, TempBlocks;

        static List<double>[] New4() => new List<double>[4]
        { new(), new(), new(), new() };
    }
}
