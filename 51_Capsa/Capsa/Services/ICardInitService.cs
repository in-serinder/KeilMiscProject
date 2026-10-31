using System;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services
{
    /// <summary>
    /// 存储卡初始化流程（写入初始化信息 → 校准 → 芯片测试 → 完成）。
    /// 真实实现接入点：<see cref="AppServices.CardInit"/>。
    /// </summary>
    public interface ICardInitService
    {
        Task<CardInitResult> InitializeAsync(
            CardInitRequest request,
            IProgress<CardInitProgress>? progress = null,
            CancellationToken cancellationToken = default);
    }
}
