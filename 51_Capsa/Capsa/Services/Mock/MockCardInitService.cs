using System;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services.Mock
{
    /// <summary>
    /// 初始化流程 Mock：按阶段推进进度并返回带 UID / 健康数量的结果。
    /// 真实实现替换点为 <see cref="AppServices.CardInit"/>。
    /// </summary>
    public sealed class MockCardInitService : ICardInitService
    {
        private static readonly (CardInitStage Stage, double From, double To, string Message)[] Stages =
        {
            (CardInitStage.WritingInfo, 0d, 30d, "Writing initialization info"),
            (CardInitStage.Calibrating, 30d, 55d, "Calibrating parameters"),
            (CardInitStage.TestingChips, 55d, 85d, "Testing chips"),
            (CardInitStage.ChipsPassed, 85d, 96d, "Chip test passed"),
            (CardInitStage.Completed, 96d, 100d, "Initialization complete"),
        };

        public async Task<CardInitResult> InitializeAsync(
            CardInitRequest request,
            IProgress<CardInitProgress>? progress = null,
            CancellationToken cancellationToken = default)
        {
            foreach (var (stage, from, to, message) in Stages)
            {
                const int steps = 12;
                for (var i = 1; i <= steps; i++)
                {
                    cancellationToken.ThrowIfCancellationRequested();

                    var percent = from + (to - from) * i / steps;
                    progress?.Report(new CardInitProgress(stage, Math.Round(percent, 1), message));

                    await Task.Delay(stage == CardInitStage.Completed ? 25 : 55, cancellationToken)
                        .ConfigureAwait(true);
                }
            }

            var healthy = Random.Shared.Next(5, 8); // 5 - 7 块芯片通过健康检查
            var uid = BuildUid();

            return new CardInitResult
            {
                DeviceName = request.DeviceName,
                Chip = request.Chip,
                Medium = request.Medium,
                InitFlag = 1,
                HealthyChips = healthy,
                ArrayCapacityBytes = (long)request.Chip.Bytes * healthy,
                Uid = uid,
                InitializedAt = DateTimeOffset.Now,
            };
        }

        /// <summary>生成六位十六进制 UID，例如 “A3F9C1”。</summary>
        private static string BuildUid()
        {
            var buffer = new byte[3];
            Random.Shared.NextBytes(buffer);

            var builder = new StringBuilder(6);
            foreach (var b in buffer)
            {
                builder.Append(b.ToString("X2"));
            }

            return builder.ToString();
        }
    }
}
