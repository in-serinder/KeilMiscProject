using System;
using Avalonia.Controls;
using Capsa.Models;
using Capsa.ViewModels.Dialogs;

namespace Capsa.Views.Dialogs
{
    /// <summary>
    /// “初始化存储卡”窗口。视图只负责把 ViewModel 的关闭请求翻译成
    /// <see cref="Window.Close(object?)"/>，业务逻辑全部在 ViewModel 内。
    /// </summary>
    public partial class InitializeCardWindow : Window
    {
        private InitializeCardViewModel? _viewModel;

        public InitializeCardWindow()
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

            _viewModel = DataContext as InitializeCardViewModel;

            if (_viewModel is not null)
            {
                _viewModel.CloseRequested += OnCloseRequested;
            }
        }

        private void OnCloseRequested(object? sender, CardInitResult? result)
        {
            Close(result);
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
