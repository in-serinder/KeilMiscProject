using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using _24Cxx_Copier_Porg.Models;
using CommunityToolkit.Mvvm.ComponentModel;

namespace _24Cxx_Copier_Porg.ViewModels;

/// <summary>
/// View-model for <c>TextViewerWindow</c>. Decodes a byte buffer into text with
/// a selectable encoding and exposes the read-only content plus the status
/// footer (size, character count, current encoding) shown below it.
/// </summary>
public partial class TextViewViewModel : ViewModelBase
{
    private byte[] _data = Array.Empty<byte>();

    public TextViewViewModel()
    {
        EncodingOptions = new ObservableCollection<EncodingOption>(EncodingOption.Catalog);
        SelectedEncoding = EncodingOptions.FirstOrDefault(e => e.Encoding == Encoding.UTF8)
                           ?? EncodingOptions.FirstOrDefault();
    }

    public TextViewViewModel(byte[] data, string? title = null)
        : this()
    {
        Data = data;
        if (!string.IsNullOrWhiteSpace(title))
        {
            Title = title!;
        }
    }

    // ---- Header ----

    /// <summary>Window / panel title.</summary>
    [ObservableProperty]
    private string _title = "Text View";

    // ---- Encoding ----

    /// <summary>Selectable decoding encodings offered by the footer drop-down.</summary>
    public ObservableCollection<EncodingOption> EncodingOptions { get; }

    /// <summary>Currently selected decoding encoding.</summary>
    [ObservableProperty]
    private EncodingOption? _selectedEncoding;

    partial void OnSelectedEncodingChanged(EncodingOption? value)
    {
        OnPropertyChanged(nameof(DisplayText));
        OnPropertyChanged(nameof(EncodingText));
        OnPropertyChanged(nameof(CharCount));
        OnPropertyChanged(nameof(FooterText));
    }

    // ---- Content ----

    /// <summary>
    /// The raw byte payload passed in by the caller. Setting it refreshes the
    /// decoded text and the footer.
    /// </summary>
    public byte[] Data
    {
        get => _data;
        set
        {
            _data = value ?? Array.Empty<byte>();
            OnPropertyChanged(nameof(Data));
            OnPropertyChanged(nameof(ByteLength));
            OnPropertyChanged(nameof(DisplayText));
            OnPropertyChanged(nameof(CharCount));
            OnPropertyChanged(nameof(SizeText));
            OnPropertyChanged(nameof(FooterText));
        }
    }

    /// <summary>The decoded, read-only text shown in the editor.</summary>
    public string DisplayText => Decode(_data, SelectedEncoding?.Encoding);

    // ---- Footer info (read-only, derived) ----

    /// <summary>Total number of bytes in the payload.</summary>
    public int ByteLength => _data.Length;

    /// <summary>Number of characters in the decoded text.</summary>
    public int CharCount => DisplayText.Length;

    /// <summary>Size expressed in bytes (e.g. "256 bytes").</summary>
    public string SizeText => $"{ByteLength} bytes ({ByteLength} B)";

    /// <summary>Human-readable encoding description for the footer.</summary>
    public string EncodingText => SelectedEncoding?.DisplayName ?? "None";

    /// <summary>Single formatted line combining size, count and encoding.</summary>
    public string FooterText =>
        $"Size: {SizeText}  |  Chars: {CharCount}  |  Encoding: {EncodingText}";

    /// <summary>Decodes <paramref name="data"/> using <paramref name="encoding"/>.</summary>
    private static string Decode(byte[] data, Encoding? encoding)
    {
        if (data.Length == 0)
        {
            return string.Empty;
        }

        encoding ??= Encoding.UTF8;

        try
        {
            return encoding.GetString(data);
        }
        catch
        {
            return string.Empty;
        }
    }
}
