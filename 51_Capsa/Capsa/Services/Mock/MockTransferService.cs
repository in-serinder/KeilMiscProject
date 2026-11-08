using System;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services.Mock
{
    /// <summary>传输 Mock：按块推进进度，用于无硬件时走查进度条 / 状态文本。</summary>
    public sealed class MockTransferService : ITransferService
    {
        public event EventHandler<byte[]>? ReadCompleted;

        public Task<TransferResult> WriteAsync(
            byte[] payload,
            IProgress<TransferProgress>? progress = null,
            CancellationToken cancellationToken = default)
            => SimulateAsync("Writing", payload.Length, progress, cancellationToken);

        public Task<TransferResult> ReadAsync(
            int capacity,
            IProgress<TransferProgress>? progress = null,
            CancellationToken cancellationToken = default)
        {
            var data = BuildPreviewPayload(capacity);
            ReadCompleted?.Invoke(this, data);
            return SimulateAsync("Reading", data.Length, progress, cancellationToken);
        }

        public void RequestAbort()
        {
            /* 模拟器无真实会话，仅用于打断循环（CTS 由面板负责）*/
        }

        public Task<TransferResult> VerifyAsync(
            byte[] payload,
            IProgress<TransferProgress>? progress = null,
            CancellationToken cancellationToken = default)
            => SimulateAsync("Verifying", payload.Length, progress, cancellationToken);

        private static async Task<TransferResult> SimulateAsync(
            string action,
            int bytes,
            IProgress<TransferProgress>? progress,
            CancellationToken cancellationToken)
        {
            if (bytes <= 0)
            {
                return TransferResult.Fail($"{action} failed: payload is empty");
            }

            /* 按 8 片切分推进：让卡片进度条在 Mock 下也能逐片走动。
             * ChipAddress 恒为 0 时界面只知道"总体在动"，看不到每片填充。*/
            const int steps = 32;
            const int chips = 8;
            var chipBytes = Math.Max(1, bytes / chips);

            for (var i = 1; i <= steps; i++)
            {
                cancellationToken.ThrowIfCancellationRequested();
                await Task.Delay(45, cancellationToken).ConfigureAwait(true);

                var percent = i * 100d / steps;
                var written = (long)(bytes * i / (double)steps);
                var index = (int)Math.Min(chips - 1, written / chipBytes);
                var within = (int)Math.Clamp(written - index * (long)chipBytes, 0, chipBytes);

                progress?.Report(new TransferProgress(
                    percent,
                    $"{action} 0x{0x50 + index:X2}… {percent:0}%",
                    0x50 + index,
                    within));
            }

            progress?.Report(new TransferProgress(100, $"{action} complete"));
            return TransferResult.Ok($"{action} complete · {bytes:N0} bytes");
        }

        /// <summary>无硬件时用于展示的占位数据。</summary>
        private static byte[] BuildPreviewPayload(int capacity)
        {
            var length = Math.Clamp(capacity, 256, 4096);
            var buffer = new byte[length];

            for (var i = 0; i < length; i++)
            {
                buffer[i] = (byte)(i % 251);
            }

            return buffer;
        }
    }
}
