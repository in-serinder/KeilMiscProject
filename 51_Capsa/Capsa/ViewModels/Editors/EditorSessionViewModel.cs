using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using Capsa.Models;
using CommunityToolkit.Mvvm.ComponentModel;

namespace Capsa.ViewModels.Editors
{
    /// <summary>
    /// HEX / 纯文本 / 双显三种编辑器共享的会话模型：
    /// 同一个 <see cref="EditorDocument"/> 缓冲区，同一套统计信息与编码设置。
    /// </summary>
    public sealed partial class EditorSessionViewModel : ViewModelBase
    {
        private readonly EditorDocument _document = new();

        public EditorSessionViewModel(bool isReadOnly = false)
        {
            IsReadOnly = isReadOnly;
            SelectedEncoding = EncodingOptions.FirstOrDefault(o => o.Encoding.CodePage == Encoding.UTF8.CodePage)
                               ?? EncodingOptions[0];
            _document.Changed += OnDocumentChanged;
            RefreshStats();
        }

        public EditorDocument Document => _document;

        public ObservableCollection<EncodingOption> EncodingOptions { get; } = new(EncodingOption.Catalog);

        [ObservableProperty]
        private EncodingOption? _selectedEncoding;

        [ObservableProperty]
        private int _toBase = 16;

        [ObservableProperty]
        private int _bytesPerLine = 16;

        [ObservableProperty]
        private bool _isReadOnly;

        [ObservableProperty]
        private EditorDisplayMode _mode = EditorDisplayMode.Hex;

        [ObservableProperty]
        private string _text = string.Empty;

        [ObservableProperty]
        private string _sourceName = "Untitled buffer";

        /// <summary>空缓冲区时编辑器内的引导文案（写入 / 读取面板各自设置）。</summary>
        [ObservableProperty]
        private string _emptyHint = "Import a payload, or switch to Read mode to load from the chip";

        /// <summary>缓冲区被整体替换（导入 / 读取 / 文本重新编码 / 编码切换）时通知视图重建。</summary>
        public event EventHandler<EditorChangeSource>? BytesReplaced;

        public Encoding CurrentEncoding => SelectedEncoding?.Encoding ?? Encoding.UTF8;

        public bool IsEditable => !IsReadOnly;

        // ---- 模式（单选框双向绑定用） ----

        public bool IsHexMode
        {
            get => Mode == EditorDisplayMode.Hex;
            set
            {
                if (value)
                {
                    Mode = EditorDisplayMode.Hex;
                }
            }
        }

        public bool IsTextMode
        {
            get => Mode == EditorDisplayMode.Text;
            set
            {
                if (value)
                {
                    Mode = EditorDisplayMode.Text;
                }
            }
        }

        public bool IsDualMode
        {
            get => Mode == EditorDisplayMode.Dual;
            set
            {
                if (value)
                {
                    Mode = EditorDisplayMode.Dual;
                }
            }
        }

        // ---- 统计信息 ----

        public long ByteLength => _document.Length;

        /// <summary>缓冲区是否为空（用于编辑器内的空状态提示）。</summary>
        public bool IsEmpty => _document.Length == 0;

        public string SizeText => ByteSize.Format(ByteLength);

        public string LengthText => ByteSize.ToHex(ByteLength);

        public string CharacterCountText => Text.Length.ToString("N0");

        public string EncodingText => SelectedEncoding?.DisplayName ?? "—";

        public string StatsText => $"Size {SizeText}    Length {LengthText}";

        public string TextStatsText => $"Size {SizeText}    Chars {CharacterCountText}    Length {LengthText}";

        public string HexStatsText => $"Size {SizeText}    Length {LengthText}    {BytesPerLine} bytes/line";

        // ---- 缓冲区操作 ----

        public byte[] Snapshot() => _document.Snapshot();

        public void LoadBytes(byte[]? data, EditorChangeSource source, string? sourceName = null)
        {
            if (!string.IsNullOrWhiteSpace(sourceName))
            {
                SourceName = sourceName!;
            }

            _document.Replace(data, source);

            if (_document.Length == 0)
            {
                Text = string.Empty;
                RefreshStats();
                BytesReplaced?.Invoke(this, source);
            }
        }

        /// <summary>应用 HEX 编辑器的编辑日志（幂等：相同的字节不会再次触发变更）。</summary>
        public void ApplyHexEdits(System.Collections.Generic.IReadOnlyList<(long Offset, byte Value)> edits)
            => _document.ApplyEdits(edits);

        /// <summary>把当前文本按所选编码写回缓冲区（文本编辑器防抖后调用）。</summary>
        public void CommitText()
        {
            if (IsReadOnly || SelectedEncoding is null)
            {
                return;
            }

            _document.Encode(Text, SelectedEncoding.Encoding);
        }

        public void Clear()
        {
            LoadBytes(Array.Empty<byte>(), EditorChangeSource.Initial);
            SourceName = "Untitled buffer";
        }

        // ---- 内部同步 ----

        private void OnDocumentChanged(object? sender, EditorDocumentChangedEventArgs e)
        {
            if (e.Source != EditorChangeSource.Hex)
            {
                Text = _document.Decode(CurrentEncoding);
            }

            RefreshStats();
            BytesReplaced?.Invoke(this, e.Source);
        }

        partial void OnSelectedEncodingChanged(EncodingOption? value)
        {
            if (value is null)
            {
                return;
            }

            Text = _document.Decode(value.Encoding);
            RefreshStats();
            BytesReplaced?.Invoke(this, EditorChangeSource.Encoding);
        }

        partial void OnModeChanged(EditorDisplayMode value)
        {
            OnPropertyChanged(nameof(IsHexMode));
            OnPropertyChanged(nameof(IsTextMode));
            OnPropertyChanged(nameof(IsDualMode));
        }

        partial void OnTextChanged(string value) => OnPropertyChanged(nameof(CharacterCountText));

        partial void OnBytesPerLineChanged(int value) => OnPropertyChanged(nameof(HexStatsText));

        partial void OnIsReadOnlyChanged(bool value) => OnPropertyChanged(nameof(IsEditable));

        private void RefreshStats()
        {
            OnPropertyChanged(nameof(ByteLength));
            OnPropertyChanged(nameof(IsEmpty));
            OnPropertyChanged(nameof(SizeText));
            OnPropertyChanged(nameof(LengthText));
            OnPropertyChanged(nameof(CharacterCountText));
            OnPropertyChanged(nameof(EncodingText));
            OnPropertyChanged(nameof(StatsText));
            OnPropertyChanged(nameof(TextStatsText));
            OnPropertyChanged(nameof(HexStatsText));
        }
    }
}
