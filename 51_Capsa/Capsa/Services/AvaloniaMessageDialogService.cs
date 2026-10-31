using System.Threading.Tasks;
using Avalonia.Controls;
using Capsa.Views.Dialogs;

namespace Capsa.Services
{
    /// <summary>以模态窗口显示错误提示。</summary>
    public sealed class AvaloniaMessageDialogService : IMessageDialogService
    {
        private readonly Window _owner;

        public AvaloniaMessageDialogService(Window owner)
        {
            _owner = owner;
        }

        public async Task ShowErrorAsync(string title, string message)
        {
            var window = new MessageWindow(title, message);
            await window.ShowDialog(_owner).ConfigureAwait(true);
        }
    }
}
