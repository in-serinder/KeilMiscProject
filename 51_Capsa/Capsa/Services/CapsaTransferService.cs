using System;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;
using Capsa.Services.Protocol;

namespace Capsa.Services
{
    /// <summary>
    /// 真实传输服务：把整包 payload 按单芯片容量切片，逐片走 ROUTER/ADDR/INT_W/W_*/TX-OVER。
    /// </summary>
    public sealed class CapsaTransferService : ITransferService
    {
        /// <summary>
        /// 进度回调最小间隔：写入时每包约 15 ms，这里每 150 ms 汇总一次，
        /// 既不会淹没界面线程，又能让进度条 / 卡片平滑前进。
        /// </summary>
        public static readonly TimeSpan ProgressInterval = TimeSpan.FromMilliseconds(150);

        private readonly Func<CapsaProtocol?> _link;

        public CapsaTransferService(Func<CapsaProtocol?> link)
        {
            _link = link;
        }

        public Task<TransferResult> WriteAsync(
            byte[] payload,
            IProgress<TransferProgress>? progress = null,
            CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                var protocol = _link();
                if (protocol is null || !protocol.IsOpen)
                {
                    return TransferResult.Fail("Serial port is not connected");
                }

                if (payload.Length == 0)
                {
                    return TransferResult.Fail("Payload is empty");
                }

                var chipBytes = protocol.ReadInfo()?.ChipBytes ?? 8192;
                var total = payload.Length;
                var sent = 0;
                var throttle = new ProgressThrottle(ProgressInterval);

                for (var chip = 0x50; chip <= 0x57 && sent < total; chip++)
                {
                    cancellationToken.ThrowIfCancellationRequested();

                    var size = Math.Min(chipBytes, total - sent);
                    var chunk = new byte[size];
                    Array.Copy(payload, sent, chunk, 0, size);

                    throttle.ShouldReport(force: true);   // 新分片：立刻刷新一次
                    var baseOffset = sent;
                    progress?.Report(new TransferProgress(
                        baseOffset * 100d / total, $"Writing 0x{chip:X2}…", chip, 0));

                    var ok = protocol.WriteMemory(
                        chip,
                        chunk,
                        (current, max) =>
                        {
                            // 每包都会回调，这里按 0.5 s 节流后再推给界面
                            if (!throttle.ShouldReport())
                            {
                                return;
                            }

                            // 注意：协议层的回调给的是“本片百分比”（0-100），不是字节数
                            var chipBytes = (int)Math.Clamp(current / 100d * size, 0, size);
                            progress?.Report(new TransferProgress(
                                (baseOffset + chipBytes) * 100d / total,
                                $"Writing 0x{chip:X2}… {current}%",
                                chip,
                                chipBytes));
                        },
                        cancellationToken);

                    if (!ok)
                    {
                        return TransferResult.Fail($"Write failed at 0x{chip:X2}");
                    }

                    sent += size;
                    // 分片完成：必发，且带上本片实际字节数（末片可能只写一部分）
                    progress?.Report(new TransferProgress(
                        sent * 100d / total, $"0x{chip:X2} written", chip, size));
                }

                progress?.Report(new TransferProgress(100, "Write complete"));
                return TransferResult.Ok($"Write complete · {sent:N0} bytes");
            }, cancellationToken);
        }

        public Task<TransferResult> ReadAsync(
            int capacity,
            IProgress<TransferProgress>? progress = null,
            CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                var protocol = _link();
                if (protocol is null || !protocol.IsOpen)
                {
                    return TransferResult.Fail("Serial port is not connected");
                }

                if (capacity <= 0)
                {
                    return TransferResult.Fail("Nothing to read");
                }

                var chipBytes = protocol.ReadInfo()?.ChipBytes ?? 8192;
                var total = Math.Min(capacity, chipBytes * 8);
                var buffer = new byte[total];
                var offset = 0;
                var throttle = new ProgressThrottle(ProgressInterval);

                for (var chip = 0x50; chip <= 0x57 && offset < total; chip++)
                {
                    cancellationToken.ThrowIfCancellationRequested();

                    var size = Math.Min(chipBytes, total - offset);
                    var baseOffset = offset;

                    throttle.ShouldReport(force: true);
                    progress?.Report(new TransferProgress(
                        baseOffset * 100d / total, $"Reading 0x{chip:X2}…", chip, 0));

                    var data = protocol.ReadMemory(
                        chip,
                        size,
                        (current, max) =>
                        {
                            if (!throttle.ShouldReport())
                            {
                                return;
                            }

                            var chipBytes = (int)Math.Clamp(current / 100d * size, 0, size);
                            progress?.Report(new TransferProgress(
                                (baseOffset + chipBytes) * 100d / total,
                                $"Reading 0x{chip:X2}… {current}%",
                                chip,
                                chipBytes));
                        },
                        cancellationToken);

                    if (data is null)
                    {
                        return TransferResult.Fail($"Read failed at 0x{chip:X2}");
                    }

                    Array.Copy(data, 0, buffer, offset, Math.Min(data.Length, size));
                    offset += size;
                    progress?.Report(new TransferProgress(
                        offset * 100d / total, $"0x{chip:X2} read", chip, size));
                }

                progress?.Report(new TransferProgress(100, "Read complete"));

                // 订阅方会重建 HEX 控件，必须在 UI 线程触发
                UiThread.Post(() => ReadCompleted?.Invoke(this, buffer));
                return TransferResult.Ok($"Read complete · {offset:N0} bytes");
            }, cancellationToken);
        }

        public Task<TransferResult> VerifyAsync(
            byte[] payload,
            IProgress<TransferProgress>? progress = null,
            CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                var protocol = _link();
                if (protocol is null || !protocol.IsOpen)
                {
                    return TransferResult.Fail("Serial port is not connected");
                }

                var chipBytes = protocol.ReadInfo()?.ChipBytes ?? 8192;
                var total = payload.Length;
                var mismatches = 0;
                var firstMismatch = -1L;
                var offset = 0;

                for (var chip = 0x50; chip <= 0x57 && offset < total; chip++)
                {
                    cancellationToken.ThrowIfCancellationRequested();

                    var size = Math.Min(chipBytes, total - offset);
                    var data = protocol.ReadMemory(chip, size, null, cancellationToken);
                    if (data is null)
                    {
                        return TransferResult.Fail($"Verify failed at 0x{chip:X2}");
                    }

                    for (var i = 0; i < size; i++)
                    {
                        if (payload[offset + i] != data[i])
                        {
                            if (firstMismatch < 0)
                            {
                                firstMismatch = offset + i;
                            }

                            mismatches++;
                        }
                    }

                    offset += size;
                    progress?.Report(new TransferProgress(
                        offset * 100d / total, $"Verifying 0x{chip:X2}…", chip, size));
                }

                return mismatches == 0
                    ? TransferResult.Ok($"Verify complete · {total:N0} bytes matched")
                    : TransferResult.Fail(
                        $"Verify failed · {mismatches:N0} mismatched byte(s), first at 0x{firstMismatch:X}");
            }, cancellationToken);
        }

        /// <summary>读取完成后抛出新数据（供面板加载到编辑器）。</summary>
        public event EventHandler<byte[]>? ReadCompleted;
    }
}
