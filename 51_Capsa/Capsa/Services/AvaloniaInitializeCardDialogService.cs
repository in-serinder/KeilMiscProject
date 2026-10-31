using System.Threading.Tasks;
using Avalonia.Controls;
using Capsa.Models;
using Capsa.ViewModels.Dialogs;
using Capsa.Views.Dialogs;

namespace Capsa.Services
{
    /// <summary>以模态对话框方式打开“初始化存储卡”窗口。</summary>
    public sealed class AvaloniaInitializeCardDialogService : IInitializeCardDialogService
    {
        private readonly Window _owner;

        public AvaloniaInitializeCardDialogService(Window owner)
        {
            _owner = owner;
        }

        public async Task<CardInitResult?> ShowAsync(string portName)
        {
            var viewModel = new InitializeCardViewModel(portName);
            var window = new InitializeCardWindow { DataContext = viewModel };

            var result = await window.ShowDialog<CardInitResult?>(_owner).ConfigureAwait(true);
            return result ?? viewModel.Result;
        }
    }
}
