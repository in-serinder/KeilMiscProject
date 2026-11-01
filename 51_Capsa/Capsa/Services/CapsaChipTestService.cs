using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;
using Capsa.Services.Protocol;

namespace Capsa.Services
{
    /// <summary>
    /// 真实芯片健康检查：对 0x50~0x57 逐片写入 0x00 / 0xFF 图案并读回校验，
    /// 按错字节数分级（0 健康 / 1-9 警告 / 10-20 中等 / &gt;20 错误）。
    /// </summary>
    public sealed class CapsaChipTestService : IChipTestService
    {
        private readonly Func<CapsaProtocol?> _link;

        public CapsaChipTestService(Func<CapsaProtocol?> link)
        {
            _link = link;
        }

        public Task<ChipTestReport> RunAsync(
            ChipTestOptions options,
            IProgress<ChipTestProgress>? progress = null,
            CancellationToken cancellationToken = default)
        {
            // 取消由 RunCore 内部捕获并转成 Canceled 报告，绝不让 OCE 逃到 UI 线程
            return Task.Run(() => RunCore(options, progress, cancellationToken));
        }

        private ChipTestReport RunCore(
            ChipTestOptions options,
            IProgress<ChipTestProgress>? progress,
            CancellationToken cancellationToken)
        {
            var protocol = _link();
            if (protocol is null || !protocol.IsOpen)
            {
                throw new InvalidOperationException("Serial port is not connected");
            }

            var info = protocol.ReadInfo();
            var chipBytes = info?.ChipBytes ?? 8192;

            // TestLengthBytes = 0 → 整片全写全读（独立健康检查窗口）
            var length = options.TestLengthBytes <= 0
                ? chipBytes
                : Math.Clamp(options.TestLengthBytes, 16, chipBytes);
            length = Math.Min(chipBytes, (length + 15) / 16 * 16);

            var patterns = new List<byte> { 0xFF };
            if (options.TestPattern00)
            {
                patterns.Insert(0, 0x00);
            }

            var outcomes = new List<ChipTestOutcome>(8);
            var healthy = 0;
            var warning = 0;
            var medium = 0;
            var error = 0;

            var canceled = false;

            try
            {
                for (var chip = 0x50; chip <= 0x57; chip++)
                {
                    cancellationToken.ThrowIfCancellationRequested();

                    var bit = chip - 0x50;
                    var writeProtected = info is not null && (info.WriteProtectMask & (1 << bit)) != 0;

                    if (writeProtected)
                    {
                        progress?.Report(new ChipTestProgress(
                            chip, ChipTestPhase.Completed, 100, $"0x{chip:X2} · write protected (WP=1)"));

                        outcomes.Add(new ChipTestOutcome
                        {
                            Address = chip,
                            Present = true,
                            ErrorBytes = length,
                            Grade = ChipHealthGrade.Error,
                        });

                        error++;
                        continue;
                    }

                    var errors = 0;

                    // 进度在整片内连续：每个图案占 1/N，其中写入 / 读回各占一半，
                    // 避免多图案时进度条反复归零。
                    var patternCount = patterns.Count;
                    var sliceSpan = 100d / patternCount / 2;

                    for (var pIndex = 0; pIndex < patternCount; pIndex++)
                    {
                        var pattern = patterns[pIndex];
                        var sliceBase = 100d * pIndex / patternCount;

                        var buffer = new byte[length];
                        Array.Fill(buffer, pattern);

                        var writeOk = protocol.WriteMemory(
                            chip,
                            buffer,
                            (current, max) =>
                            {
                                var written = (int)(length * Math.Clamp(current, 0, 100) / 100d);
                                progress?.Report(new ChipTestProgress(
                                    chip,
                                    ChipTestPhase.Writing,
                                    sliceBase + sliceSpan * Math.Clamp(current, 0, 100) / 100d,
                                    $"0x{chip:X2} · writing 0x{pattern:X2} · {written:N0}/{length:N0} B"));
                            },
                            cancellationToken);

                        var readBack = writeOk
                            ? protocol.ReadMemory(
                                chip,
                                length,
                                (current, max) =>
                                {
                                    var read = (int)(length * Math.Clamp(current, 0, 100) / 100d);
                                    progress?.Report(new ChipTestProgress(
                                        chip,
                                        ChipTestPhase.Reading,
                                        sliceBase + sliceSpan + sliceSpan * Math.Clamp(current, 0, 100) / 100d,
                                        $"0x{chip:X2} · verifying 0x{pattern:X2} · {read:N0}/{length:N0} B"));
                                },
                                cancellationToken)
                            : null;

                        if (readBack is null)
                        {
                            errors += length;
                            continue;
                        }

                        for (var i = 0; i < length; i++)
                        {
                            if (readBack[i] != buffer[i])
                            {
                                errors++;
                            }
                        }
                    }

                    var grade = ChipTestOutcome.Classify(errors);

                    switch (grade)
                    {
                        case ChipHealthGrade.Healthy:
                            healthy++;
                            break;
                        case ChipHealthGrade.Warning:
                            warning++;
                            break;
                        case ChipHealthGrade.Medium:
                            medium++;
                            break;
                        default:
                            error++;
                            break;
                    }

                    progress?.Report(new ChipTestProgress(
                        chip,
                        ChipTestPhase.Completed,
                        100,
                        $"0x{chip:X2} · {GradeText(grade)} ({errors} bad bytes)",
                        grade));

                    outcomes.Add(new ChipTestOutcome
                    {
                        Address = chip,
                        Present = true,
                        ErrorBytes = errors,
                        Grade = grade,
                    });
                }
            }
            catch (OperationCanceledException)
            {
                // 用户取消：保留已完成芯片的结论，返回部分报告而不是把异常抛给 UI
                canceled = true;
            }

            return new ChipTestReport
            {
                Canceled = canceled,
                Outcomes = outcomes,
                HealthyCount = healthy,
                WarningCount = warning,
                MediumCount = medium,
                ErrorCount = error,
            };
        }

        private static string GradeText(ChipHealthGrade grade) => grade switch
        {
            ChipHealthGrade.Healthy => "HEALTHY",
            ChipHealthGrade.Warning => "WARN",
            ChipHealthGrade.Medium => "MEDIUM",
            _ => "ERR",
        };
    }
}
