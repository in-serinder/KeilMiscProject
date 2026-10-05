using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using _24Cxx_Copier_Porg.Models;
using CommunityToolkit.Mvvm.ComponentModel;
using HexView.Avalonia.Model;

namespace _24Cxx_Copier_Porg.ViewModels;

/// <summary>
/// View-model for <c>HexViewWindow</c>. Exposes the binary payload to display
/// together with the numeric base, byte grouping and encoding information shown
/// in the status footer.
/// </summary>
public partial class HexViewViewModel : ViewModelBase
{
    private byte[] _data = Array.Empty<byte>();
    private ILineReader? _reader;

    public HexViewViewModel()
    {
        SelectedEncoding = EncodingOptions[0];
    }

    public HexViewViewModel(byte[] data, string? title = null)
        : this()
    {
        Data = data;
        if (!string.IsNullOrWhiteSpace(title))
        {
            Title = title!;
        }
    }

    // ---- Hex decoder encoding ----

    /// <summary>
    /// Selectable decoding encodings offered by the footer drop-down.
    /// </summary>
    public ObservableCollection<EncodingOption> EncodingOptions { get; } =
        new(BuildEncodingOptions());
    /// <summary>
    /// Currently selected decoding encoding. Kept in sync with
    /// <see cref="Encoding"/> so the footer text and the ASCII pane always match
    /// the drop-down selection.
    /// </summary>
    [ObservableProperty]
    private EncodingOption? _selectedEncoding;

    partial void OnSelectedEncodingChanged(EncodingOption? value)
    {
        if (value is not null)
        {
            Encoding = value.Encoding;
        }
    }

    // private static IEnumerable<EncodingOption> BuildEncodingOptions()
    // {
    //     return new[]
    //     {
    //         new EncodingOption("ASCII", Encoding.ASCII),
    //         new EncodingOption("UTF-8", Encoding.UTF8),
    //         new EncodingOption("UTF-16 LE", Encoding.Unicode),
    //         new EncodingOption("UTF-16 BE", Encoding.BigEndianUnicode),
    //         new EncodingOption("UTF-32", Encoding.UTF32),
    //         new EncodingOption("Latin-1 (ISO-8859-1)", Encoding.Latin1),
    //         new EncodingOption("Unicode", Encoding.Unicode),
    //         
    //     };
    // }
    private static IEnumerable<EncodingOption> BuildEncodingOptions()
    {
        yield return new EncodingOption("ASCII", Encoding.ASCII);
        yield return new EncodingOption("UTF-8", Encoding.UTF8);
        yield return new EncodingOption("UTF-16 LE", Encoding.Unicode);
        yield return new EncodingOption("UTF-16 BE", Encoding.BigEndianUnicode);
        yield return new EncodingOption("UTF-32", Encoding.UTF32);
        yield return new EncodingOption("Latin-1 (ISO-8859-1)", Encoding.Latin1);
        yield return new EncodingOption("Windows-1252 (CP1252)", Encoding.GetEncoding(1252));
    

        yield return new EncodingOption("GB2312", Encoding.GetEncoding("GB2312"));
        yield return new EncodingOption("GBK", Encoding.GetEncoding("GBK"));
        yield return new EncodingOption("GB18030", Encoding.GetEncoding("GB18030"));

        yield return new EncodingOption("Shift_JIS", Encoding.GetEncoding("Shift_JIS"));
    }

    // ---- Header ----

    /// <summary>Window / panel title.</summary>
    [ObservableProperty]
    private string _title = "Hex View";

    // ---- Content interface ----

    /// <summary>
    /// The binary payload passed in by the caller. Setting it also rebuilds the
    /// line reader used by the HexView control and refreshes the footer info.
    /// </summary>
    public byte[] Data
    {
        get => _data;
        set
        {
            _data = value ?? Array.Empty<byte>();
            Reader = new MemoryLineReader(_data);
            OnPropertyChanged(nameof(Data));
            OnPropertyChanged(nameof(ByteLength));
            OnPropertyChanged(nameof(SizeText));
            OnPropertyChanged(nameof(LengthText));
            OnPropertyChanged(nameof(FooterText));
        }
    }

    /// <summary>
    /// Reader exposed for the view to assign to <c>HexViewControl.LineReader</c>.
    /// </summary>
    public ILineReader? Reader
    {
        get => _reader;
        private set
        {
            _reader = value;
            OnPropertyChanged(nameof(Reader));
        }
    }

    // ---- View configuration ----

    /// <summary>Numeric base for the hex pane (2, 8, 10 or 16).</summary>
    [ObservableProperty]
    private int _toBase = 16;

    /// <summary>Number of bytes rendered per line.</summary>
    [ObservableProperty]
    private int _bytesPerLine = 16;

    /// <summary>Optional character encoding used for the ASCII pane.</summary>
    [ObservableProperty]
    private Encoding? _encoding = Encoding.ASCII;

    /// <summary>Whether editing is allowed inside the control.</summary>
    [ObservableProperty]
    private bool _isEditable = true;

    // ---- Footer info (read-only, derived) ----

    /// <summary>Total number of bytes.</summary>
    public int ByteLength => _data.Length;

    /// <summary>Size expressed in bytes (e.g. "256 bytes").</summary>
    public string SizeText
    {
        get
        {
            var n = ByteLength;
            return $"{n} bytes ({n} B)";
        }
    }

    /// <summary>Length expressed in the current base, e.g. "0x100".</summary>
    public string LengthText
    {
        get
        {
            var n = ByteLength;
            return ToBase switch
            {
                2 => $"0b{Convert.ToString(n, 2)}",
                8 => $"0o{Convert.ToString(n, 8)}",
                16 => $"0x{n:X}",
                _ => n.ToString()
            };
        }
    }

    /// <summary>Human readable encoding description for the footer.</summary>
    public string EncodingText => Encoding?.WebName.ToUpperInvariant() ?? "None";

    /// <summary>Single formatted line combining size, length and encoding.</summary>
    public string FooterText =>
        $"Size: {SizeText}  |  Length: {LengthText}  |  Encoding: {EncodingText}  |  Base: {ToBase}  |  {BytesPerLine} bytes/line";

    partial void OnToBaseChanged(int value)
    {
        OnPropertyChanged(nameof(LengthText));
        OnPropertyChanged(nameof(FooterText));
    }

    partial void OnBytesPerLineChanged(int value) => OnPropertyChanged(nameof(FooterText));

    partial void OnEncodingChanged(Encoding? value)
    {
        // Keep the drop-down selection aligned when the encoding is set
        // programmatically (e.g. from the constructor).
        var match = EncodingOptions.FirstOrDefault(o => Equals(o.Encoding, value));
        if (match is not null && !ReferenceEquals(match, SelectedEncoding))
        {
            SelectedEncoding = match;
        }

        OnPropertyChanged(nameof(EncodingText));
        OnPropertyChanged(nameof(FooterText));
    }
}

