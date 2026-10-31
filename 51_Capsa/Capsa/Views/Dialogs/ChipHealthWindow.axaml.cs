using System;
using Avalonia.Controls;
using Capsa.ViewModels.Dialogs;

namespace Capsa.Views.Dialogs
{
    /// <summary>
    /// “芯片健康检查”窗口。打开时读取一次槽位信息，关闭时把“是否测试过”回传给调用方。
    /// </summary>
    public partial class ChipHealthWindow : Window
    {
        private ChipHealthViewModel? _viewModel;

        public ChipHealthWindow()
        {
            InitializeComponent();
            DataContextChanged += (_, _) => Attach();
        }

        private void Attach()
        {
            if (_viewModel is not null)
            {
                _viewModel.CloseRequested -= OnCloseRequested;
            }

            _viewModel = DataContext as ChipHealthViewModel;

            if (_viewModel is not null)
            {
                _viewModel.CloseRequested += OnCloseRequested;
                _ = _viewModel.LoadAsync();
            }
        }

        private void OnCloseRequested(object? sender, bool testRan)
        {
            Close(testRan);
        }

        protected override void OnClosed(EventArgs e)
        {
            if (_viewModel is not null)
            {
                _viewModel.CloseRequested -= OnCloseRequested;
            }

            base.OnClosed(e);
        }
    }
}
