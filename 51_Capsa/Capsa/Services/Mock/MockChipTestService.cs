using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services.Mock
{
    /// <summary>
    /// 芯片健康检查 Mock：模拟“写入 0xFF / 0x00 → 读回校验”，
    /// 用槽位健康分换算错字节数，走与真实实现相同的分级逻辑。
    /// </summary>
    public sealed class MockChipTestService : IChipTestService
    {
        private const int StepsPerPhase = 10;
        private const int StepDelayMs = 24;

        private readonly Func<IDeviceService> _deviceAccessor;

        public MockChipTestService(Func<IDeviceService> deviceAccessor)
        {
            _deviceAccessor = deviceAccessor;
        }

        public async Task<ChipTestReport> RunAsync(
            ChipTestOptions options,
            IProgress<ChipTestProgress>? progress = null,
            CancellationToken cancellationToken = default)
        {
            var slots = await _deviceAccessor().ReadSlotsAsync(cancellationToken).ConfigureAwait(true);

            var outcomes = new List<ChipTestOutcome>(slots.Count);
            var healthy = 0;
            var warning = 0;
            var medium = 0;
            var error = 0;

            var canceled = false;

            try
            {
                foreach (var slot in slots)
                {
                    cancellationToken.ThrowIfCancellationRequested();

                    if (!slot.Present)
                    {
                        progress?.Report(new ChipTestProgress(
                            slot.Address, ChipTestPhase.Completed, 0, $"0x{slot.Address:X2} · no chip detected"));

                        outcomes.Add(new ChipTestOutcome { Address = slot.Address, Present = false });
                        continue;
                    }

                    var patterns = options.TestPattern00
                        ? new byte[] { 0x00, 0xFF }
                        : new byte[] { 0xFF };

                    foreach (var pattern in patterns)
                    {
                        await RunPhaseAsync(slot.Address, pattern, true, progress, cancellationToken).ConfigureAwait(true);
                        await RunPhaseAsync(slot.Address, pattern, false, progress, cancellationToken).ConfigureAwait(true);
                    }

                    // 按健康分映射到四档错字节数，方便走查四种卡片状态：
                    //   >=90 健康 / 75-89 1-9 / 60-74 10-20 / <60 21+
                    var errorBytes = slot.HealthScore switch
                    {
                        >= 90 => 0,
                        >= 75 => Random.Shared.Next(1, 10),
                        >= 60 => Random.Shared.Next(10, 21),
                        _ => Random.Shared.Next(21, 41),
                    };
                    var grade = ChipTestOutcome.Classify(errorBytes);

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
                        slot.Address,
                        ChipTestPhase.Completed,
                        100,
                        $"0x{slot.Address:X2} · {GradeText(grade)} ({errorBytes} bad bytes)",
                        grade));

                    outcomes.Add(new ChipTestOutcome
                    {
                        Address = slot.Address,
                        Present = true,
                        ErrorBytes = errorBytes,
                        Grade = grade,
                    });
                }
            }
            catch (OperationCanceledException)
            {
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

        private static async Task RunPhaseAsync(
            int address,
            byte pattern,
            bool write,
            IProgress<ChipTestProgress>? progress,
            CancellationToken cancellationToken)
        {
            var phase = write ? ChipTestPhase.Writing : ChipTestPhase.Reading;
            var action = write
                ? $"0x{address:X2} · writing 0x{pattern:X2}…"
                : $"0x{address:X2} · verifying 0x{pattern:X2}…";

            for (var step = 1; step <= StepsPerPhase; step++)
            {
                cancellationToken.ThrowIfCancellationRequested();

                progress?.Report(new ChipTestProgress(address, phase, step * 100d / StepsPerPhase, action));
                await Task.Delay(StepDelayMs, cancellationToken).ConfigureAwait(true);
            }
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
