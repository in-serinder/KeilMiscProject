using System;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using _24Cxx_Copier_Porg.ViewModels;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using HexView.Avalonia.Controls;
using HexView.Avalonia.Services;

namespace _24Cxx_Copier_Porg.Views;

/// <summary>
/// Reusable wrapper around <see cref="HexViewControl"/>. The control requires
/// its <c>HexFormatter</c> and <c>LineReader</c> to be set imperatively, so this
/// view forwards them from the bound <see cref="HexViewViewModel"/>.
/// </summary>
public partial class HexViewer : UserControl
{
    private HexViewControl? _hexView;
    private HexViewViewModel? _viewModel;
    private Button? _downloadButton;

    public HexViewer()
    {
        InitializeComponent();

        _hexView = this.FindControl<HexViewControl>("HexContent");
        _downloadButton = this.FindControl<Button>("DownloadButton");
        if (_downloadButton is not null)
        {
            _downloadButton.Click += OnDownloadClick;
        }

        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (_viewModel is not null)
        {
            _viewModel.PropertyChanged -= OnViewModelPropertyChanged;
        }

        _viewModel = DataContext as HexViewViewModel;

        if (_viewModel is not null)
        {
            _viewModel.PropertyChanged += OnViewModelPropertyChanged;
        }

        ApplyData();
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(HexViewViewModel.Data)
            or nameof(HexViewViewModel.Reader))
        {
            ApplyData();
        }
        else if (e.PropertyName is nameof(HexViewViewModel.ToBase)
            or nameof(HexViewViewModel.BytesPerLine)
            or nameof(HexViewViewModel.Encoding)
            or nameof(HexViewViewModel.IsEditable))
        {
            ApplyFormatter();
        }
    }

    private void ApplyData()
    {
        if (_hexView is null || _viewModel is null)
        {
            return;
        }

        _hexView.LineReader = _viewModel.Reader;
        ApplyFormatter();
    }

    private void ApplyFormatter()
    {
        if (_hexView is null || _viewModel is null)
        {
            return;
        }

        _hexView.HexFormatter = new HexFormatter(_viewModel.ByteLength)
        {
            Width = _viewModel.BytesPerLine,
            Encoding = _viewModel.Encoding ?? System.Text.Encoding.ASCII,
        };

        // IsEditable is a plain CLR property on the control, not a styled
        // property, so it must be assigned imperatively.
        _hexView.IsEditable = _viewModel.IsEditable;

        _hexView.InvalidateScrollable();
    }

    // ---- Download (.bin) ----

    private async void OnDownloadClick(object? sender, RoutedEventArgs e)
    {
        if (_viewModel is null || _hexView is null)
        {
            return;
        }

        try
        {
            var topLevel = TopLevel.GetTopLevel(this);
            if (topLevel is null)
            {
                return;
            }

            var file = await topLevel.StorageProvider.SaveFilePickerAsync(
                new FilePickerSaveOptions
                {
                    Title = "Download as .bin",
                    SuggestedFileName = "dump.bin",
                    DefaultExtension = "bin",
                    ShowOverwritePrompt = true,
                    FileTypeChoices = new[]
                    {
                        new FilePickerFileType("Binary file")
                        {
                            Patterns = new[] { "*.bin" },
                            MimeTypes = new[] { "application/octet-stream" },
                        },
                        new FilePickerFileType("All files")
                        {
                            Patterns = new[] { "*.*" },
                        },
                    },
                });

            if (file is null)
            {
                return;
            }

            var bytes = BuildExportBytes();

            await using var stream = await file.OpenWriteAsync();
            stream.SetLength(0);
            await stream.WriteAsync(bytes, 0, bytes.Length);
            await stream.FlushAsync();
        }
        catch
        {
            // Ignore save failures (cancelled dialog, locked target, etc.).
        }
    }

    /// <summary>
    /// Returns the current buffer, with any in-control edits merged on top of
    /// the original data.
    /// </summary>
    private byte[] BuildExportBytes()
    {
        var original = _viewModel?.Data ?? Array.Empty<byte>();
        var bytes = new byte[original.Length];
        Array.Copy(original, bytes, original.Length);

        var edits = _hexView?.GetEdits();
        if (edits is not null)
        {
            foreach (var (offset, value) in edits)
            {
                if (offset >= 0 && offset < bytes.Length)
                {
                    bytes[offset] = value;
                }
            }
        }

        return bytes;
    }
}

