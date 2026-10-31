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

        /// <summary>最近一次“由缓冲区解码得到”的文本：用来区分用户输入与程序同步。</summary>
        private string? _syncedText;

        /// <summary>缓冲区当前字节实际使用的编码（切换编码时用它做转码的源编码）。</summary>
        private EncodingOption? _appliedEncoding;

        public EditorSessionViewModel(bool isReadOnly = false)
        {
            IsReadOnly = isReadOnly;
            SelectedEncoding = EncodingOptions.FirstOrDefault(o => o.Encoding.CodePage == Encoding.UTF8.CodePage)
                               ?? EncodingOptions[0];
            _appliedEncoding = SelectedEncoding;
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
                _syncedText = Text;
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

            // 文本与缓冲区解码结果一致 = 不是用户改的（例如刚导入二进制后被防抖回调），
            // 此时绝不能重新编码：二进制经 UTF-8 解码是有损的，会把内容改坏。
            if (string.Equals(Text, _syncedText, StringComparison.Ordinal))
            {
                return;
            }

            _document.Encode(Text, SelectedEncoding.Encoding);
            _syncedText = Text;
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
                _syncedText = Text;
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

            var previous = _appliedEncoding;
            _appliedEncoding = value;

            // 写入模式：如果缓冲区确实是文本，就按“旧编码解码 → 新编码编码”真正转码，
            // 而不是拿新编码去重新解释旧字节（那必然乱码）。
            // 二进制内容（图片等）与只读会话保持原字节，只改变显示时的解码方式。
            if (!IsReadOnly
                && previous is not null
                && previous.Encoding.CodePage != value.Encoding.CodePage
                && TryTranscode(previous.Encoding, value.Encoding))
            {
                return;
            }

            Text = _document.Decode(value.Encoding);
            _syncedText = Text;
            RefreshStats();
            BytesReplaced?.Invoke(this, EditorChangeSource.Encoding);
        }

        /// <summary>
        /// 把缓冲区从 <paramref name="from"/> 转码到 <paramref name="to"/>：
        /// 解码成文本后用新编码重新编码。
        /// 只有「解码结果像文本」「旧编码能无损往返」「新编码能表示全部字符」三条都满足才执行，
        /// 因此二进制内容不会被改坏；无法转码时返回 false，由调用方退回“只换显示编码”。
        /// </summary>
        private bool TryTranscode(Encoding from, Encoding to)
        {
            if (_document.Length == 0)
            {
                return false;
            }

            var bytes = _document.Snapshot();
            var text = from.GetString(bytes);

            if (!LooksLikeText(text))
            {
                return false;
            }

            // 旧编码下必须能无损往返，否则说明解出来的文本不是原始内容
            if (!from.GetBytes(text).AsSpan().SequenceEqual(bytes))
            {
                return false;
            }

            // 新编码必须能表示全部字符，否则会写成 '?' 或替换字符
            var encoded = to.GetBytes(text);
            if (!string.Equals(to.GetString(encoded), text, StringComparison.Ordinal))
            {
                return false;
            }

            Text = text;
            _syncedText = text;

            if (!_document.Replace(encoded, EditorChangeSource.Encoding))
            {
                // 两种编码的字节恰好一致（纯 ASCII）：只需让 HEX 更新字符列
                BytesReplaced?.Invoke(this, EditorChangeSource.Encoding);
            }

            return true;
        }

        /// <summary>粗略判断解码结果是否像文本：出现替换字符或不可打印控制符即视为二进制。</summary>
        private static bool LooksLikeText(string text)
        {
            foreach (var ch in text)
            {
                var code = (int)ch;

                // U+FFFD 替换字符 = 解码时有字节无法映射；0x7F = DEL
                if (code == 0xFFFD || code == 0x7F)
                {
                    return false;
                }

                // 只允许制表 / 换行 / 回车这三个控制符
                if (code < 0x20 && code != 0x09 && code != 0x0A && code != 0x0D)
                {
                    return false;
                }
            }

            return true;
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
