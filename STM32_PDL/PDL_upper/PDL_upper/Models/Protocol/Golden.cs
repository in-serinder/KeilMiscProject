using System;
using System.Collections.Generic;

namespace PDL_upper.Models.Protocol
{
    /// <summary>
    /// 协议自检：用 simulator/pdl/proto.py 实际生成、存在 protocol/vectors/golden.json 里的字节做比对。
    /// 编解码一旦改动必须跑通这里。
    /// </summary>
    public static class Golden
    {
        private static string Hex(byte[] b, int n) =>
            BitConverter.ToString(b, 0, n).Replace("-", " ");

        public static List<string> Run()
        {
            var fails = new List<string>();
            void Check(string name, bool ok, string detail = "")
            {
                Console.WriteLine($"  [{(ok ? "OK" : "FAIL")}] {name}{(ok ? "" : "  " + detail)}");
                if (!ok) fails.Add(name);
            }

            // ---- CRC-16/CCITT-FALSE 标准校验值 ----
            Check("CRC16(\"123456789\") == 0x29B1",
                Pdl.Crc16(System.Text.Encoding.ASCII.GetBytes("123456789"), 9) == 0x29B1);

            // ---- IDENTIFY 请求 ----
            var want1 = "A5 01 10 00 04 00 01 00 44 33 22 11 47 12";
            var f1 = Pdl.EncodeFrame(Pdl.T.Identify, 0, 1, BitConverter.GetBytes(0x11223344u));
            Check("EX1 IDENTIFY 请求", Hex(f1, 14) == want1, Hex(f1, 14));

            // ---- 解码 round-trip ----
            var dec = new FrameDecoder();
            var frames = dec.Feed(f1, f1.Length);
            Check("解码回读 1 帧", frames.Count == 1 && frames[0].Type == Pdl.T.Identify
                && frames[0].Seq == 1 && frames[0].Payload.Length == 4);

            // ---- 坏帧必须被丢弃 ----
            var bad = (byte[])f1.Clone();
            bad[10] ^= 0xFF;
            Check("CRC 错帧被拒", new FrameDecoder().Feed(bad, bad.Length).Count == 0);

            // ---- 流式解码（半帧喂入）----
            var dec2 = new FrameDecoder();
            var a = dec2.Feed(f1, 40);
            var b = dec2.Feed(f1, f1.Length - 40);
            Check("半帧喂入可重组", a.Count + b.Count == 1);

            // ---- ACQUIRE 载荷与整帧 ----
            var p = new AcquireParams
            {
                PowerMask = 0x0F, TempMask = 0x03,
                RecordMode = Pdl.RecordMode.Manual, Tier = 0,
                SampleRateHz = 1000, ReportIntervalMs = 100,
                BinMs = 0, DurationS = 0, TempIntervalMs = 1000,
                TrigSource = 0, TrigCh = 0, TrigStartEn = true, TrigStopEn = true,
                StartHoldMs = 50, StopHoldMs = 50, StartVal = 12000, StopVal = 8000,
            };
            var f2 = Pdl.EncodeFrame(Pdl.T.Acquire, 0, 2, p.Build());
            var want2 = "A5 01 20 00 30 00 02 00 0F 03 00 00 E8 03 00 00 64 00 00 00 " +
                        "00 00 00 00 00 00 00 00 E8 03 00 00 00 00 03 0C 32 00 32 00 " +
                        "E0 2E 00 00 40 1F 00 00 00 00 00 00 00 00 00 00 DA 65";
            int n2 = Pdl.Hdr + (f2[4] | f2[5] << 8) + Pdl.Crc;
            Check("EX4 ACQUIRE 整帧", Hex(f2, n2) == want2, Hex(f2, n2));

            // ---- 块头解析 ----
            // EX5：sample_index=100000, t=1000ms, seq=7, count=2, mask=0x0F, flags=0, fmt=0
            byte[] blk = {
                0xA0, 0x86, 0x01, 0x00, 0xE8, 0x03, 0x00, 0x00, 0x07, 0x00, 0x02, 0x0F, 0x00, 0x00,
                0x00, 0x08, 0xDC, 0x05, 0x01, 0x08, 0xDB, 0x05,
            };
            var h = BlockHeader.Parse(blk);
            Check("块头 sample_index/t/count/mask/fmt",
                h.SampleIndex == 100000 && h.TFirstMs == 1000 && h.Count == 2
                && h.ChannelMask == 0x0F && h.Fmt == Pdl.Fmt.Raw16);

            // ---- RAW 块换算：CH0 2048/1500 → 12.0 mV? 2048-0=2048 → 2048*3.46=7086 mV ----
            var cfg = new ChannelConfig { VNum = 346, VDen = 100, INum = 2441, IDen = 1000, IOff = 2048 };
            double vMv = (Pdl.I16(blk, 14) - cfg.VOff) * cfg.LsbMv;
            double iMa = (Pdl.I16(blk, 16) - cfg.IOff) * cfg.LsbMa;
            Check("CH0 码值→工程单位", Math.Abs(vMv - 2048 * 3.46) < 0.5 && Math.Abs(iMa - (1500 - 2048) * 2.441) < 1,
                $"{vMv:F1} mV / {iMa:F1} mA");

            Console.WriteLine(fails.Count == 0
                ? "PDL 协议自检全部通过。"
                : $"PDL 协议自检失败 {fails.Count} 项。");
            return fails;
        }
    }
}
