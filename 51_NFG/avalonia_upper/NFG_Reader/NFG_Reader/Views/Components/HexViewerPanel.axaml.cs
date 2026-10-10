using System;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Media;
using HexView.Avalonia.Services;
using NFG_Reader.Models;

namespace NFG_Reader.Views.Components
{
    public partial class HexViewerPanel : UserControl
    {
        public static readonly StyledProperty<string> FileNameProperty =
            AvaloniaProperty.Register<HexViewerPanel, string>(nameof(FileName), "No file loaded");

        public static readonly StyledProperty<string> FileSizeTextProperty =
            AvaloniaProperty.Register<HexViewerPanel, string>(nameof(FileSizeText), "0 B  (0 bytes)");

        public static readonly StyledProperty<byte[]?> DataProperty =
            AvaloniaProperty.Register<HexViewerPanel, byte[]?>(nameof(Data));

        public static readonly DirectProperty<HexViewerPanel, bool> HasDataProperty =
            AvaloniaProperty.RegisterDirect<HexViewerPanel, bool>(nameof(HasData), o => o.HasData);

        public static readonly DirectProperty<HexViewerPanel, bool> IsEmptyProperty =
            AvaloniaProperty.RegisterDirect<HexViewerPanel, bool>(nameof(IsEmpty), o => o.IsEmpty);

        private readonly MemoryLineReader _lineReader = new();
        private bool _hasData;
        private bool _isEmpty = true;

        // Candidate bytes-per-line, largest first. The control picks the first
        // one whose rendered row fits the current panel width, so it grows or
        // shrinks with the window (16 -> 8 -> 4 -> 2 -> 1).
        private static readonly int[] ByteWidthCandidates = { 16, 8, 4, 2, 1 };

        // The viewer scales its font within this range as the panel width
        // changes, so the hex row fills the available space.
        private const double MinFontSize = 9.0;
        private const double MaxFontSize = 18.0;
        private const double BaseFontSize = 12.0;
        private const double GroupSize = 8.0;

        private static readonly Typeface MonoTypeface =
            new(new FontFamily("Consolas,Menlo,Monospace"));

        private int _bytesWidth = 8;     // current effective bytes-per-line
        private double _fontSize = BaseFontSize;
        private double _charWidth = -1;  // char width for the current font size
        private byte[] _buffer = Array.Empty<byte>();

        public HexViewerPanel()
        {
            InitializeComponent();

            _charWidth = MeasureCharWidth(_fontSize);
            _baseCharAtBase = MeasureCharWidth(BaseFontSize);

            HexView.LineReader = _lineReader;
            HexView.ToBase = 16;
            HexView.BytesWidth = _bytesWidth;
            ApplyFontSize();

            ApplyData(null);

            // Re-flow whenever the available width changes so the hex row
            // (offset | hex | ascii) always fits the panel.
            SizeChanged += (_, e) => RefitToWidth(e.NewSize.Width);
        }

        public string FileName
        {
            get => GetValue(FileNameProperty);
            set => SetValue(FileNameProperty, value);
        }

        public string FileSizeText
        {
            get => GetValue(FileSizeTextProperty);
            set => SetValue(FileSizeTextProperty, value);
        }

        /// <summary>Raw bytes rendered by the embedded HexView control.</summary>
        public byte[]? Data
        {
            get => GetValue(DataProperty);
            set => SetValue(DataProperty, value);
        }

        public bool HasData => _hasData;

        public bool IsEmpty => _isEmpty;

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);

            if (change.Property == DataProperty)
            {
                ApplyData(change.GetNewValue<byte[]?>());
            }
        }

        /// <summary>
        /// Chooses the largest bytes-per-line whose rendered row fits
        /// <paramref name="availableWidth"/> and rebuilds the formatter.
        /// </summary>
        private void RefitToWidth(double availableWidth)
        {
            if (availableWidth <= 0)
            {
                return;
            }

            // Reserve a little for borders/scrollbar/padding.
            const double padding = 24.0;
            double usable = availableWidth - padding;

            // 1) Scale the font so a full 16-byte row roughly fills the panel.
            //    Wider panel -> bigger text; narrower -> smaller text (clamped).
            double target = BaseFontSize * (usable / RowWidthChars(16) / _baseCharAtBase);
            double newFontSize = Math.Clamp(target, MinFontSize, MaxFontSize);
            bool fontChanged = Math.Abs(newFontSize - _fontSize) > 0.25;
            if (fontChanged)
            {
                _fontSize = newFontSize;
                _charWidth = MeasureCharWidth(_fontSize);
                ApplyFontSize();
            }

            // 2) Choose the largest bytes-per-line that fits the available width
            //    at the (possibly new) font size.
            int chosen = ByteWidthCandidates[^1];
            foreach (int candidate in ByteWidthCandidates)
            {
                if (EstimateRowWidth(candidate) <= usable)
                {
                    chosen = candidate;
                    break;
                }
            }

            if (!fontChanged && chosen == _bytesWidth)
            {
                return;
            }

            _bytesWidth = chosen;
            HexView.BytesWidth = _bytesWidth;
            BuildFormatter();
            HexView.InvalidateScrollable();
        }

        // Char width at BaseFontSize, captured once for scaling math.
        private double _baseCharAtBase = -1;

        /// <summary>Number of characters in one "offset: hex | ascii" row.</summary>
        private static int RowWidthChars(int bytesPerLine)
        {
            int addressChars = 10;                                  // "00000000: "
            int hexChars = bytesPerLine * 3;                        // "XX " per byte
            int groupGaps = (bytesPerLine - 1) / (int)GroupSize;    // "| " every 8 bytes
            int separatorChars = groupGaps * 2;
            int asciiLead = 3;                                      // " | "
            int asciiChars = bytesPerLine;
            return addressChars + hexChars + separatorChars + asciiLead + asciiChars;
        }

        /// <summary>Approximate pixel width of one row at the current font size.</summary>
        private double EstimateRowWidth(int bytesPerLine)
            => RowWidthChars(bytesPerLine) * _charWidth;

        private static double MeasureCharWidth(double size)
        {
            var ft = new FormattedText(
                "0",
                CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight,
                MonoTypeface,
                size,
                Brushes.Black);
            return ft.Width;
        }

        private void ApplyFontSize()
        {
            TextElement.SetFontSize(HexView, _fontSize);
        }

        private void ApplyData(byte[]? data)
        {
            _buffer = data ?? Array.Empty<byte>();
            _lineReader.SetData(_buffer);

            BuildFormatter();

            SetAndRaise(HasDataProperty, ref _hasData, _buffer.Length > 0);
            SetAndRaise(IsEmptyProperty, ref _isEmpty, _buffer.Length == 0);

            // Force a re-measure / repaint with the new content.
            HexView.InvalidateScrollable();
        }

        private void BuildFormatter()
        {
            // HexFormatter caches the total length, so re-create it for each new buffer.
            // Width MUST match BytesWidth so the offset/hex/ascii columns line up.
            HexView.HexFormatter = new HexFormatter(_buffer.Length)
            {
                Width = _bytesWidth,
                GroupSize = (int)GroupSize,
                ShowGroupSeparator = true,
                Encoding = System.Text.Encoding.ASCII
            };
        }
    }
}
