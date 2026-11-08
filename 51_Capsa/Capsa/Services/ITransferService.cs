using System;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services
{
    /// <summary>写入 / 读取 / 校验传输接口。实现方负责分块、重试与校验。</summary>
    public interface ITransferService
    {
        /// <summary>读流程结束后抛出的数据（供面板装载到编辑器）。实现方保证在 UI 线程触发。</summary>
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

        /// <summary>
        /// 向固件发 ABORT，让它立刻退出读 / 写会话（中断按钮用）。
        /// 与 CancellationToken 配合使用：token 负责中止主机侧循环，ABORT 负责
        /// 让下位机从"等 RX-OK"里出来，否则它会白等 2 秒超时才退出。
        /// </summary>
        void RequestAbort();
    }
}
