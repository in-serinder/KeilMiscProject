using System;
using Avalonia;
using Avalonia.Controls;
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

        /// <summary>
        /// Bytes shown per row. Kept small (8) so the single line
        /// "offset | hex ... | ascii" fits the panel width without the
        /// ASCII column being clipped off the right edge.
        /// </summary>
        private const int BytesPerLine = 8;

        public HexViewerPanel()
        {
            InitializeComponent();

            HexView.LineReader = _lineReader;
            HexView.BytesWidth = BytesPerLine;
            HexView.ToBase = 16;

            ApplyData(null);
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

        private void ApplyData(byte[]? data)
        {
            var buffer = data ?? Array.Empty<byte>();
            _lineReader.SetData(buffer);

            // HexFormatter caches the total length, so re-create it for each new buffer.
            // Width MUST match BytesWidth so the offset/hex/ascii columns line up.
            HexView.HexFormatter = new HexFormatter(buffer.Length)
            {
                Width = BytesPerLine,
                GroupSize = 4,
                ShowGroupSeparator = true,
                Encoding = System.Text.Encoding.ASCII
            };

            SetAndRaise(HasDataProperty, ref _hasData, buffer.Length > 0);
            SetAndRaise(IsEmptyProperty, ref _isEmpty, buffer.Length == 0);

            // Force a re-measure / repaint with the new content.
            HexView.InvalidateScrollable();
        }
    }
}

