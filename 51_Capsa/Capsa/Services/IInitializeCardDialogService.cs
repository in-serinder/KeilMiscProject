using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services
{
    /// <summary>
    /// 打开“初始化存储卡”窗口。接口化后主界面无需知道具体窗口类型，
    /// 由 View 层注入 <see cref="AvaloniaInitializeCardDialogService"/>。
    /// </summary>
    public interface IInitializeCardDialogService
    {
        Task<CardInitResult?> ShowAsync(string portName);
    }

    /// <summary>空实现：设计时 / 未注入时安全返回 null。</summary>
    public sealed class NullInitializeCardDialogService : IInitializeCardDialogService
    {
        public Task<CardInitResult?> ShowAsync(string portName)
            => Task.FromResult<CardInitResult?>(null);
    }
}
