using System;
using System.Text;
using _24Cxx_Copier_Porg.ViewModels;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;

namespace _24Cxx_Copier_Porg.Views;

/// <summary>
/// Rich-text reader: a read-only text editor showing the decoded payload, with
/// a footer that reports its size, character count and current encoding and
/// offers an encoding switcher plus a "download as .txt" action.
/// </summary>
public partial class TextViewer : UserControl
{
    private Button? _downloadButton;

    public TextViewer()
    {
        InitializeComponent();

        _downloadButton = this.FindControl<Button>("DownloadButton");
        if (_downloadButton is not null)
        {
            _downloadButton.Click += OnDownloadClick;
        }
    }

    // ---- Download (.txt) ----

    private async void OnDownloadClick(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not TextViewViewModel vm)
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
                    Title = "Download as .txt",
                    SuggestedFileName = "dump.txt",
                    DefaultExtension = "txt",
                    ShowOverwritePrompt = true,
                    FileTypeChoices = new[]
                    {
                        new FilePickerFileType("Text file")
                        {
                            Patterns = new[] { "*.txt" },
                            MimeTypes = new[] { "text/plain" },
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

            // Write the text using the currently selected encoding, prefixed
            // with its preamble (BOM) when it has one so editors detect it.
            var encoding = vm.SelectedEncoding?.Encoding ?? Encoding.UTF8;
            var preamble = encoding.GetPreamble();
            var content = encoding.GetBytes(vm.DisplayText);

            var bytes = new byte[preamble.Length + content.Length];
            Buffer.BlockCopy(preamble, 0, bytes, 0, preamble.Length);
            Buffer.BlockCopy(content, 0, bytes, preamble.Length, content.Length);

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
}
