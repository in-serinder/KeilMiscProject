using System;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services
{
    /// <summary>
    /// 芯片健康检查流程：逐颗芯片写入 0xFF 图案 → 读回校验，可选追加 0x00 图案。
    /// 真实实现接入点：<see cref="AppServices.ChipTest"/>。
    /// </summary>
    public interface IChipTestService
    {
        Task<ChipTestReport> RunAsync(
            ChipTestOptions options,
            IProgress<ChipTestProgress>? progress = null,
            CancellationToken cancellationToken = default);
    }
}
