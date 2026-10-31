using System;
using System.Collections.Generic;
using System.ComponentModel;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Media;
using Avalonia.Threading;
using Capsa.Models;
using Capsa.ViewModels.Editors;
using HexView.Avalonia.Controls;
using HexView.Avalonia.Services;

namespace Capsa.Views.Editors
{
    /// <summary>
    /// HexView 控件的宿主视图。
    /// 控件要求 <c>HexFormatter</c> / <c>LineReader</c> 命令式赋值，因此这里用代码后置
    /// 完成绑定，并定时把控件内的编辑日志同步回共享缓冲区（实现与文本视图的双向同步）。
    /// </summary>
    public partial class HexEditorView : UserControl
    {
        private static readonly FontFamily MonoFont = new("Cascadia Mono,Consolas,Menlo,Monospace");

        private readonly ContentControl _host;
        private readonly DispatcherTimer _pollTimer;

        private HexViewControl? _hex;
        private EditorSessionViewModel? _session;

        public HexEditorView()
        {
            InitializeComponent();

            _host = this.FindControl<ContentControl>("HexHost")!;

            _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(240) };
            _pollTimer.Tick += OnPollTick;

            DataContextChanged += (_, _) => Attach();
            AttachedToVisualTree += (_, _) =>
            {
                Attach();
                _pollTimer.Start();
            };
            DetachedFromVisualTree += (_, _) => _pollTimer.Stop();
        }

        private void Attach()
        {
            if (_session is not null)
            {
                _session.BytesReplaced -= OnBytesReplaced;
                _session.PropertyChanged -= OnSessionPropertyChanged;
            }

            _session = DataContext as EditorSessionViewModel;

            if (_session is not null)
            {
                _session.BytesReplaced += OnBytesReplaced;
                _session.PropertyChanged += OnSessionPropertyChanged;
            }

            Rebuild();
        }

        // ---- 控件创建 / 数据装载 ----

        private void Rebuild()
        {
            if (_session is null)
            {
                _host.Content = null;
                _hex = null;
                return;
            }

            var hex = new HexViewControl
            {
                ToBase = _session.ToBase,
                BytesWidth = _session.BytesPerLine,
                IsEditable = !_session.IsReadOnly,
            };

            hex.SetValue(TextElement.FontFamilyProperty, MonoFont);
            hex.SetValue(TextElement.FontSizeProperty, 12d);

            _hex = hex;
            _host.Content = hex;

            ApplyData();
        }

        private void ApplyData()
        {
            if (_hex is null || _session is null)
            {
                return;
            }

            _hex.LineReader = new MemoryLineReader(_session.Snapshot());
            ApplyFormatter();
        }

        private void ApplyFormatter()
        {
            if (_hex is null || _session is null)
            {
                return;
            }

            _hex.HexFormatter = new HexFormatter(_session.ByteLength)
            {
                Width = _session.BytesPerLine,
                Encoding = _session.CurrentEncoding,
            };

            _hex.IsEditable = !_session.IsReadOnly;
            _hex.ToBase = _session.ToBase;
            _hex.BytesWidth = _session.BytesPerLine;
            _hex.InvalidateScrollable();
        }

        // ---- 同步 ----

        private void OnSessionPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName is nameof(EditorSessionViewModel.ToBase)
                or nameof(EditorSessionViewModel.BytesPerLine))
            {
                ApplyFormatter();
            }
            else if (e.PropertyName is nameof(EditorSessionViewModel.IsReadOnly))
            {
                if (_hex is not null)
                {
                    _hex.IsEditable = !_session!.IsReadOnly;
                }
            }
        }

        private void OnBytesReplaced(object? sender, EditorChangeSource source)
        {
            switch (source)
            {
                case EditorChangeSource.Hex:
                    // 由本控件产生，无需重建。
                    return;
                case EditorChangeSource.Encoding:
                    ApplyFormatter();
                    return;
                default:
                    // 文本 / 导入 / 设备变更：重建控件以清空编辑日志。
                    Rebuild();
                    return;
            }
        }

        private void OnPollTick(object? sender, EventArgs e)
        {
            if (_session is null || _hex is null || _session.IsReadOnly)
            {
                return;
            }

            var edits = _hex.GetEdits();
            if (edits is null)
            {
                return;
            }

            var batch = new List<(long Offset, byte Value)>();
            foreach (var (offset, value) in edits)
            {
                batch.Add((Convert.ToInt64(offset), value));
            }

            if (batch.Count > 0)
            {
                _session.ApplyHexEdits(batch);
            }
        }
    }
}
