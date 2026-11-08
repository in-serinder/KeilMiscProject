using System;
using System.ComponentModel;
using System.Linq;
using System.Text;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Reactive;
using Avalonia.Threading;
using Capsa.ViewModels.Editors;

namespace Capsa.Views.Editors
{
    /// <summary>
    /// 纯文本编辑器。文本变更做 300ms 防抖后再按所选编码回写共享缓冲区，
    /// 避免每次按键都重建 HEX 视图。
    ///
    /// ★ 左侧行号栏（gutter）：等宽字体与编辑器一致，随编辑器的 ScrollViewer
    ///   垂直滚动同步平移。行号文本只在**行数变化**时重建 —— 行内编辑
    ///   （改字符不改行）只做一次 O(n) 换行计数，不重建字符串。
    /// </summary>
    public partial class TextEditorView : UserControl
    {
        private readonly DispatcherTimer _debounce;

        private EditorSessionViewModel? _session;
        private bool _pendingCommit;

        private int _lastLineCount = -1;
        private bool _observedText;

        public TextEditorView()
        {
            InitializeComponent();

            _debounce = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(300) };
            _debounce.Tick += OnDebounceTick;

            DataContextChanged += (_, _) => Attach();
            DetachedFromVisualTree += (_, _) => _debounce.Stop();

            // ★ 滚动同步走路由事件：内部 ScrollViewer 的 ScrollChanged 会冒泡到
            //   TextBox，不必去找模板里的 ScrollViewer 实例（挂载时机也不可靠）。
            TextHost.AddHandler(ScrollViewer.ScrollChangedEvent, OnHostScrollChanged);
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

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);

            // VM 加载内容（导入文件 / 读取设备）也要刷新行号
            if (!_observedText)
            {
                _observedText = true;
                TextHost.GetObservable(TextBox.TextProperty)
                    .Subscribe(new AnonymousObserver<string>(_ => UpdateLineNumbers()));
            }

            UpdateLineNumbers();
        }

        private void OnHostScrollChanged(object? sender, ScrollChangedEventArgs e)
        {
            // 行号栏的 ScrollViewer 直接跟随编辑器的垂直偏移。
            // e.Source 排除自己 —— 设置 GutterScroll.Offset 会再次触发本事件，
            // 不排除的话就是无限循环。
            if (e.Source is ScrollViewer scroll && !ReferenceEquals(scroll, GutterScroll))
            {
                GutterScroll.Offset = new Vector(0, scroll.Offset.Y);
            }
        }

        private void UpdateLineNumbers()
        {
            var text = TextHost.Text ?? string.Empty;

            // 行数 = 换行数 + 1（TextWrapping=NoWrap，无软换行干扰）。
            // 单次 O(n) 扫描很快，但字符串重建只在行数真正变化时做。
            var lines = 1;
            foreach (var c in text)
            {
                if (c == '\n')
                {
                    lines++;
                }
            }

            if (lines == _lastLineCount)
            {
                return;
            }

            _lastLineCount = lines;

            var sb = new StringBuilder(lines * 6);
            for (var i = 1; i <= lines; i++)
            {
                sb.Append(i);
                if (i < lines)
                {
                    sb.Append('\n');
                }
            }

            GutterText.Text = sb.ToString();
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
