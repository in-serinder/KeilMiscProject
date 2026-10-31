using System.Threading.Tasks;
using Avalonia.Controls;
using Capsa.ViewModels.Dialogs;
using Capsa.Views.Dialogs;

namespace Capsa.Services
{
    /// <summary>以模态对话框方式打开“芯片健康检查”窗口。</summary>
    public sealed class AvaloniaChipHealthDialogService : IChipHealthDialogService
    {
        private readonly Window _owner;

        public AvaloniaChipHealthDialogService(Window owner)
        {
            _owner = owner;
        }

        public async Task<bool> ShowAsync(string portName)
        {
            var viewModel = new ChipHealthViewModel(portName);
            var window = new ChipHealthWindow { DataContext = viewModel };

            var testRan = await window.ShowDialog<bool>(_owner).ConfigureAwait(true);
            return testRan || viewModel.TestRan;
        }
    }
}
