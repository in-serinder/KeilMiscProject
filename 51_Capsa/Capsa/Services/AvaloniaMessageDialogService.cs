using System;
using System.Threading.Tasks;
using Avalonia.Controls;
using Capsa.Diagnostics;
using Capsa.Views.Dialogs;

namespace Capsa.Services
{
    /// <summary>以模态窗口显示错误详情（完整异常 + 协议交互，可复制）。</summary>
    public sealed class AvaloniaMessageDialogService : IMessageDialogService
    {
        private readonly Window _owner;

        public AvaloniaMessageDialogService(Window owner)
        {
            _owner = owner;
        }

        public async Task ShowErrorAsync(string title, string message)
        {
            // 调用方往往已经 ErrorLog.Add 过（带 context / 异常）——
            // 摘要相同就复用那条的详情，避免日志里出现两条重复记录。
            var latest = ErrorLog.Latest;
            if (latest is null || !string.Equals(latest.Summary, message, StringComparison.Ordinal))
            {
                ErrorLog.Add(title, message);
                latest = ErrorLog.Latest;
            }

            var detail = latest?.Detail
                         ?? message + Environment.NewLine + "（No exception detail was captured for this message.）";

            var window = new ErrorDetailsWindow(title, message, detail);
            await window.ShowDialog(_owner).ConfigureAwait(true);
        }
    }
}
