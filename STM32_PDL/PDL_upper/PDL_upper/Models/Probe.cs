using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Threading.Tasks;
using PDL_upper.Models.Protocol;
using PDL_upper.Models.Transport;

namespace PDL_upper.Models
{
    /// <summary>
    /// 无界面联调：连模拟器跑一遍完整流程，验证 DeviceSession 与真设备（模拟器）的互操作性。
    /// 用法：先起模拟器，再 `dotnet run -- --probe --tcp 127.0.0.1:5555`
    /// </summary>
    public static class Probe
    {
        public static async Task<int> RunAsync()
        {
            var fails = new List<string>();
            void Check(string name, bool ok, string detail = "")
            {
                Console.WriteLine($"  [{(ok ? "OK" : "FAIL")}] {name}{(ok ? "" : "  " + detail)}");
                if (!ok) fails.Add(name);
            }

            var tp = new TcpTransport(AppArgs.Host, AppArgs.Port);
            Console.WriteLine($"连接 {tp.Describe} …");
            tp.Open();

            using var s = new DeviceSession(tp);
            var svc = new AcquisitionService();
            var blocks = new List<byte[]>();
            var events = new List<PdlEvent>();
            s.BlockPayload += b => { svc.HandleBlock(b); blocks.Add(b); };
            s.EventReceived += events.Add;
            s.Log += m => Console.WriteLine("  [链路] " + m);
            s.Start();

            try
            {
                // 1. 识别
                var id = await s.IdentifyAsync(0xC0FFEE);
                Check("IDENTIFY magic=PDL1", id.Magic == "PDL1" && id.Nonce == 0xC0FFEE, id.Magic);
                Check("4 功率 + 2 温度", id.PowerChannels == 4 && id.TempChannels == 2);
                Check("caps bit7=1（各通道量程不同）", id.PerChannelRange, $"caps=0x{id.Caps:X2}");

                // 2. 逐通道配置
                var cfgs = new ChannelConfig[id.PowerChannels];
                var vfs = new List<double>();
                for (int ch = 0; ch < id.PowerChannels; ch++)
                {
                    cfgs[ch] = await s.GetChConfigAsync(ch);
                    vfs.Add(cfgs[ch].FullScaleV);
                }
                for (int ch = 0; ch < id.TempChannels; ch++)
                    await s.GetChConfigAsync(id.PowerChannels + ch);
                svc.SetConfig(cfgs);
                Check("四路满量程 14.2/28.1/75.9/102.3",
                    Math.Abs(vfs[0] - 14.2) < 0.05 && Math.Abs(vfs[1] - 28.1) < 0.05 &&
                    Math.Abs(vfs[2] - 75.9) < 0.05 && Math.Abs(vfs[3] - 102.3) < 0.05,
                    string.Join("/", vfs));

                // 3. 时间同步 + 温度
                await s.TimeSyncAsync();
                Check("TIME_SYNC RTT < 200 ms", s.RttMs < 200, $"{s.RttMs:F1} ms");
                var temps = await s.ReadTempOnceAsync();
                Check("READ_TEMP_ONCE", temps[0] != short.MaxValue,
                    $"raw={temps[0]}, {temps[0] / 16.0:F1} ℃ / {temps[1] / 16.0:F1} ℃");

                // 4. 采集 1.2 s
                var p = new AcquireParams
                {
                    PowerMask = 0x0F, TempMask = 0x03,
                    RecordMode = Pdl.RecordMode.Manual,
                    SampleRateHz = 1000, ReportIntervalMs = 100,
                };
                var acq = await s.AcquireAsync(p);
                Check("ACQUIRE 进入 RUNNING", acq.State == Pdl.DevState.Running);
                Check("设备回报的采样率 = 请求值", acq.EffSampleRateHz == p.SampleRateHz);

                await Task.Delay(1200);

                // 5. 停止
                var f = await s.AbortAsync(false);
                var extra = new byte[f.Payload.Length - 4];
                Array.Copy(f.Payload, 4, extra, 0, extra.Length);
                var ab = AbortAck.Parse(extra);
                Check("ABORT 后回 IDLE", ab.FinalState == Pdl.DevState.Idle);
                Check("tick 数 ≈ 1200", ab.TotalTicks is > 1000 and < 1400, ab.TotalTicks.ToString());
                Check("块数 ≈ 12", ab.TotalBlocks is > 8 and < 20, ab.TotalBlocks.ToString());

                // 6. 块内容抽查
                int tempBlocks = 0;
                foreach (var b in blocks)
                    if ((BlockHeader.Parse(b).Flags & Pdl.BF.TempBlock) != 0) tempBlocks++;
                Check("收到数据块", blocks.Count >= 10, blocks.Count.ToString());
                Console.WriteLine($"  温度块 {tempBlocks} 个；解码后 TH0={svc.Temp[0].Celsius:F1} ℃ TH1={svc.Temp[1].Celsius:F1} ℃");
                Check("温度解码", svc.Temp[0].Celsius > 20 && svc.Temp[0].Celsius < 60,
                    svc.Temp[0].Celsius.ToString("F1"));
                if (blocks.Count > 0)
                {
                    var h = BlockHeader.Parse(blocks[0]);
                    Check("块头 fmt=RAW16", h.Fmt == Pdl.Fmt.Raw16);
                    Check("块头 mask=0x0F", h.ChannelMask == 0x0F);
                    // 功率块与温度块共用同一 seq 计数器（见协议 6.1），所以只验递增不回退
                    Check("块序号单调递增", blocks.Count == 1 ||
                        BlockHeader.Parse(blocks[^1]).BlockSeq > BlockHeader.Parse(blocks[^2]).BlockSeq);
                }
                Check("sample_index 连续无缺口", blocks.Count == 0 ||
                    BlockHeader.Parse(blocks[0]).SampleIndex == 0);

                Console.WriteLine($"  统计：块 {blocks.Count}，事件 {events.Count}，RTT {s.RttMs:F1} ms，offset {s.OffsetMs:F1} ms");

                // ---- .pdl 写读往返 ----
                Console.WriteLine("PDL 文件往返：");
                var w = new PdlIO.PdlFileWriter();
                var hdr = new PdlIO.PdlFileHeader
                {
                    SampleRateHz = 1000,
                    ReportIntervalMs = 100,
                    DeviceInfo = "probe",
                    Power = cfgs.ToList(),
                };
                string spool = Path.Combine(Path.GetTempPath(), "pdl_probe.pdl.part");
                w.Start(spool, hdr);
                foreach (var b in blocks) w.AppendBlock(b);
                foreach (var e in events) w.AppendEvent(e.Code, e.Ch, e.Param, e.TMs);
                var sum = w.Finalize();
                Check("PDL 写入块数", sum.BlockCount == blocks.Count,
                    $"{sum.BlockCount}/{blocks.Count}");
                Check("PDL 时长", sum.DurationMs >= 1000, sum.DurationMs + " ms");

                var rr = PdlIO.PdlFileReader.Open(spool);
                Check("PDL 头有效", rr.HeaderOk && rr.Header.Power.Count == id.PowerChannels);
                Check("PDL 块数回读", rr.PowerBlocks.Count + rr.TempBlocks.Count == blocks.Count,
                    $"{rr.PowerBlocks.Count}+{rr.TempBlocks.Count}/{blocks.Count}");
                Check("PDL Footer", rr.FooterOk && rr.DurationMs == sum.DurationMs);
                var dec = rr.Decode();
                int totalPts = dec.T.Sum(t => t.Count);
                Check("PDL 解码点数", (long)totalPts >= (long)sum.DurationMs / 1000, totalPts + " 点");
                Check("PDL 温度解码", dec.Tv.Sum(t => t.Count) > 0,
                    dec.Tv[0].Count + "+" + dec.Tv[1].Count);
                File.Delete(spool);
            }
            catch (Exception ex)
            {
                fails.Add(ex.Message);
                Console.WriteLine("  [FAIL] 异常：" + ex);
            }
            finally
            {
                try { tp.Close(); } catch { }
            }

            Console.WriteLine(fails.Count == 0 ? "联调全部通过。" : $"失败 {fails.Count} 项。");
            return fails.Count == 0 ? 0 : 1;
        }
    }
}
