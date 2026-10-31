using System;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services
{
    /// <summary>写入 / 读取 / 校验传输接口。实现方负责分块、重试与校验。</summary>
    public interface ITransferService
    {
        /// <summary>读流程结束后抛出的数据（供面板装载到编辑器）。</summary>
        event EventHandler<byte[]>? ReadCompleted;

        Task<TransferResult> WriteAsync(
            byte[] payload,
            IProgress<TransferProgress>? progress = null,
            CancellationToken cancellationToken = default);

        Task<TransferResult> ReadAsync(
            int capacity,
            IProgress<TransferProgress>? progress = null,
            CancellationToken cancellationToken = default);

        Task<TransferResult> VerifyAsync(
            byte[] payload,
            IProgress<TransferProgress>? progress = null,
            CancellationToken cancellationToken = default);
    }
}
