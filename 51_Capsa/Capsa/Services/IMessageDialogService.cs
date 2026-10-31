using System.Threading.Tasks;

namespace Capsa.Services
{
    /// <summary>轻量消息弹窗（错误提示）。</summary>
    public interface IMessageDialogService
    {
        Task ShowErrorAsync(string title, string message);
    }

    /// <summary>空实现：设计时 / 未注入时只写日志。</summary>
    public sealed class NullMessageDialogService : IMessageDialogService
    {
        public Task ShowErrorAsync(string title, string message)
            => Task.CompletedTask;
    }
}
