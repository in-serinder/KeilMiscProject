using System.Threading.Tasks;
using Capsa.Diagnostics;

namespace Capsa.Services
{
    /// <summary>轻量消息弹窗（错误提示）。</summary>
    public interface IMessageDialogService
    {
        Task ShowErrorAsync(string title, string message);
    }

    /// <summary>空实现：不弹窗，但仍然记入全局错误日志（设计时 / Mock 模式）。</summary>
    public sealed class NullMessageDialogService : IMessageDialogService
    {
        public Task ShowErrorAsync(string title, string message)
        {
            var latest = Capsa.Diagnostics.ErrorLog.Latest;
            if (latest is null || !string.Equals(latest.Summary, message, System.StringComparison.Ordinal))
            {
                ErrorLog.Add(title, message);
            }

            return Task.CompletedTask;
        }
    }
}
