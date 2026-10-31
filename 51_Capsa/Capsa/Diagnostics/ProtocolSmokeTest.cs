using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Capsa.Models;
using Capsa.Services;
using Capsa.Services.Protocol;

namespace Capsa.Diagnostics
{
    /// <summary>
    /// 串口协议自检（命令行：<c>Capsa.exe --sim-test COM1 [log.txt]</c>）。
    /// 用于在真实串口 / 桥接对上验证 Helper 的编码与解析，不依赖界面。
    /// </summary>
    internal static class ProtocolSmokeTest
    {
        public static int Run(string portName, string logPath)
        {
            var lines = new List<string>();
            var exit = 0;

            void Log(string message)
            {
                lines.Add($"{DateTime.Now:HH:mm:ss} {message}");
            }

            try
            {
                using var protocol = new CapsaProtocol(portName);
                protocol.Open();
                Log($"OPEN {portName} ok");

                Log($"PING {(protocol.Ping(1200) ? "PASS" : "FAIL")}");

                var now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
                var info = new CapsaInfo
                {
                    Name = "SMOKE",
                    WriteProtectMask = 0x00,
                    HealthMask = 0xFF,
                    CapacityKb = 64,
                    ModelKbit = 64,
                    ModifiedAt = now,
                    ReadAt = now,
                    Medium = 0,
                    Uid = "A1B2C3D4E5F6",
                };

                Log($"SET {(protocol.WriteInfo(info) ? "PASS" : "FAIL")}");

                var readBack = protocol.ReadInfo();
                Log($"GET {(readBack is null ? "FAIL (null)" : readBack.ToInfoFrame())}");

                var payload = Enumerable.Range(0, 32).Select(i => (byte)i).ToArray();
                Log($"WRITE {(protocol.WriteMemory(0x50, payload) ? "PASS" : "FAIL")}");

                var data = protocol.ReadMemory(0x50, 32);
                Log($"READ {(data is null ? "FAIL (null)" : Convert.ToHexString(data))}");
                Log($"MATCH {(data is not null && data.SequenceEqual(payload) ? "PASS" : "FAIL")}");

                // 写保护：打开 0x50 后应被拒绝
                info.WriteProtectMask = 0x01;
                Log($"SET WP {(protocol.WriteInfo(info) ? "PASS" : "FAIL")}");

                var blocked = protocol.SelectChip(0x50) && protocol.SetEndAddress(31);
                if (blocked)
                {
                    var start = protocol.Exchange("INT_W");
                    Log($"WRITE PROTECTED {(start is { } f && f.Matches("ERR_WP") ? "PASS" : "FAIL")}");
                }

                info.WriteProtectMask = 0x00;
                protocol.WriteInfo(info);

                // ---- 服务层：设备信息 / 槽位 ----
                var device = new CapsaDeviceService(() => protocol);
                var deviceInfo = device.ReadDeviceInfoAsync().GetAwaiter().GetResult();
                Log($"DEVICE {(deviceInfo is null ? "FAIL (null)" : $"{deviceInfo.DeviceName} · {deviceInfo.ChipModel} · {deviceInfo.ChipType} · health {deviceInfo.HealthScore}")}");

                var slots = device.ReadSlotsAsync().GetAwaiter().GetResult();
                Log($"SLOTS {string.Join(" ", slots.Select(x => $"0x{x.Address:X2}:{(x.WriteProtected ? "WP" : "RW")}/H{x.HealthScore}"))}");

                // ---- 服务层：芯片测试（小窗口，快速） ----
                var chipTest = new CapsaChipTestService(() => protocol);
                var report = chipTest.RunAsync(new ChipTestOptions { TestPattern00 = true, TestLengthBytes = 64 })
                    .GetAwaiter().GetResult();
                Log($"CHIPTEST healthy={report.HealthyCount} warn={report.WarningCount} medium={report.MediumCount} err={report.ErrorCount} mask=0b{Convert.ToString(report.HealthMask, 2).PadLeft(8, '0')}");

                // ---- 服务层：传输 ----
                var transfer = new CapsaTransferService(() => protocol);
                var block = Enumerable.Range(0, 64).Select(i => (byte)(0xA0 + i)).ToArray();
                Log($"XFER-WRITE {transfer.WriteAsync(block).GetAwaiter().GetResult().Message}");
                Log($"XFER-VERIFY {transfer.VerifyAsync(block).GetAwaiter().GetResult().Message}");

                byte[]? readBack2 = null;
                transfer.ReadCompleted += (_, data) => readBack2 = data;
                Log($"XFER-READ {transfer.ReadAsync(64).GetAwaiter().GetResult().Message}");
                Log($"XFER-READ-MATCH {(readBack2 is not null && readBack2.Take(64).SequenceEqual(block) ? "PASS" : "FAIL")}");

                // ---- 服务层：初始化流程 ----
                var chip = ChipCatalog.Find("24C64")!;
                var medium = StorageMediumCatalog.All[0];
                var initService = new CapsaCardInitService(() => protocol, chipTest);
                var initResult = initService.InitializeAsync(new CardInitRequest
                {
                    PortName = portName,
                    DeviceName = "SIMCARD",
                    Chip = chip,
                    Medium = medium,
                }).GetAwaiter().GetResult();

                Log($"INIT {initResult.DeviceName} uid={initResult.Uid} healthy={initResult.HealthyChips} bytes={initResult.ArrayCapacityBytes}");

                var afterInit = protocol.ReadInfo();
                Log($"INIT-GET {(afterInit is null ? "FAIL" : afterInit.ToInfoFrame())}");

                // ---- 已使用空间：UG / UGS（写入 156 kbit = 19968 B） ----
                Log($"UGS-AFTER-INIT {protocol.ReadUsedBytes()?.ToString() ?? "FAIL"}");

                const int usedBytes = 19_968;
                var block2 = new byte[usedBytes];
                for (var i = 0; i < block2.Length; i++)
                {
                    block2[i] = (byte)(i & 0xFF);
                }

                var write2 = transfer.WriteAsync(block2).GetAwaiter().GetResult();
                var ugOk = protocol.SetUsedBytes(usedBytes);
                var ugEcho = protocol.ReadUsedBytes();
                Log($"UG-SET {(ugOk ? "OK" : "FAIL")} -> echo {ugEcho}");

                var slots2 = device.ReadSlotsAsync().GetAwaiter().GetResult();
                Log($"UG-SLOTS {string.Join(" ", slots2.Select(x => $"0x{x.Address:X2}:{x.UsedBytes}B"))}");

                var info2 = device.ReadDeviceInfoAsync().GetAwaiter().GetResult();
                Log($"UG-DEVICE used={info2?.UsedBytes}B array={info2?.ArrayTotalBytes}B pct={info2?.UsagePercent:0.#}%");

                // UG 越界应被拒绝
                Log($"UG-OVERFLOW {(protocol.SetUsedBytes(65_537) ? "FAIL (accepted)" : "OK (rejected)")}");
                Log($"UG-RESTORE {(protocol.SetUsedBytes(usedBytes) ? "OK" : "FAIL")}");

                // ---- 文件载荷端到端：写入带 .png 后缀的数据，按 UGS 长度读回并还原 ----
                var fileBytes = new byte[1234];
                for (var i = 0; i < fileBytes.Length; i++)
                {
                    fileBytes[i] = (byte)(i * 7 & 0xFF);
                }

                var wrapped = FilePayloadCodec.Wrap(fileBytes, "smoke-test.png");
                var wrote = transfer.WriteAsync(wrapped).GetAwaiter().GetResult();
                var recorded = protocol.SetUsedBytes(wrapped.Length) ? protocol.ReadUsedBytes() : null;

                byte[]? fileBack = null;
                transfer.ReadCompleted += (_, bytes) => fileBack = bytes;
                var readResult = transfer.ReadAsync((int)(recorded ?? wrapped.Length)).GetAwaiter().GetResult();

                var restored = fileBack is null ? default : FilePayloadCodec.Unwrap(fileBack);
                Log($"FILE-ROUNDTRIP write={wrote.Success} ug={recorded} read={readResult.Success} " +
                    $"name={restored.FileName ?? "<none>"} " +
                    $"match={(fileBack is not null && restored.Data.SequenceEqual(fileBytes) ? "PASS" : "FAIL")}");

                // ---- 整片覆写测试（独立窗口用的模式）：只跑 0xFF，8 片全片 ----
                var sw = System.Diagnostics.Stopwatch.StartNew();
                var full = chipTest.RunAsync(new ChipTestOptions { TestPattern00 = false, TestLengthBytes = 0 })
                    .GetAwaiter().GetResult();
                Log($"CHIPTEST-FULL healthy={full.HealthyCount} warn={full.WarningCount} medium={full.MediumCount} err={full.ErrorCount} in {sw.ElapsedMilliseconds} ms");

                Log("DONE");
            }
            catch (Exception ex)
            {
                Log("EXCEPTION " + ex);
                exit = 1;
            }
            finally
            {
                try
                {
                    File.WriteAllLines(logPath, lines);
                }
                catch (Exception)
                {
                    // 忽略日志写入失败
                }
            }

            return exit;
        }
    }
}
