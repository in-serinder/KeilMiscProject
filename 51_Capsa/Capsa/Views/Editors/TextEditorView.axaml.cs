using System;
using System.ComponentModel;
using Avalonia.Controls;
using Avalonia.Threading;
using Capsa.ViewModels.Editors;

namespace Capsa.Views.Editors
{
    /// <summary>
    /// 纯文本编辑器。文本变更做 300ms 防抖后再按所选编码回写共享缓冲区，
    /// 避免每次按键都重建 HEX 视图。
    /// </summary>
    public partial class TextEditorView : UserControl
    {
        private readonly DispatcherTimer _debounce;

        private EditorSessionViewModel? _session;
        private bool _pendingCommit;

        public TextEditorView()
        {
            InitializeComponent();

            _debounce = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(300) };
            _debounce.Tick += OnDebounceTick;

            DataContextChanged += (_, _) => Attach();
            DetachedFromVisualTree += (_, _) => _debounce.Stop();
        }

        private void Attach()
        {
            if (_session is not null)
            {
                _session.PropertyChanged -= OnSessionPropertyChanged;
            }

            _session = DataContext as EditorSessionViewModel;

            if (_session is not null)
            {
                _session.PropertyChanged += OnSessionPropertyChanged;
            }
        }

        private void OnSessionPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName != nameof(EditorSessionViewModel.Text))
            {
                return;
            }

            if (_session is null || _session.IsReadOnly)
            {
                return;
            }

            _pendingCommit = true;
            _debounce.Stop();
            _debounce.Start();
        }

        private void OnDebounceTick(object? sender, EventArgs e)
        {
            _debounce.Stop();

            if (!_pendingCommit || _session is null)
            {
                return;
            }

            _pendingCommit = false;
            _session.CommitText();
        }
    }
}
