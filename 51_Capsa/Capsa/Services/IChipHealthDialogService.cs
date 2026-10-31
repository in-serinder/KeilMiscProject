using System.Threading.Tasks;

namespace Capsa.Services
{
    /// <summary>打开“芯片健康检查”窗口；返回值表示本次是否执行过测试。</summary>
    public interface IChipHealthDialogService
    {
        Task<bool> ShowAsync(string portName);
    }

    /// <summary>空实现：设计时 / 未注入时安全返回 false。</summary>
    public sealed class NullChipHealthDialogService : IChipHealthDialogService
    {
        public Task<bool> ShowAsync(string portName) => Task.FromResult(false);
    }
}
