using System.Collections.Generic;
using System.Text;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// A selectable character-encoding option shown in the hex / text viewer
/// decoder drop-down. Wraps an <see cref="Encoding"/> together with its display
/// name.
/// </summary>
public sealed class EncodingOption
{
    public EncodingOption(string displayName, Encoding encoding)
    {
        DisplayName = displayName;
        Encoding = encoding;
    }

    /// <summary>Friendly name shown in the drop-down, e.g. "UTF-8".</summary>
    public string DisplayName { get; }

    /// <summary>The underlying .NET encoding.</summary>
    public Encoding Encoding { get; }

    public override string ToString() => DisplayName;

    /// <summary>
    /// The shared, ordered list of encodings offered by the viewers' footer
    /// drop-downs.
    /// </summary>
    public static IReadOnlyList<EncodingOption> Catalog { get; } = new[]
    {
        new EncodingOption("ASCII", Encoding.ASCII),
        new EncodingOption("UTF-8", Encoding.UTF8),
        new EncodingOption("UTF-16 LE", Encoding.Unicode),
        new EncodingOption("UTF-16 BE", Encoding.BigEndianUnicode),
        new EncodingOption("UTF-32", Encoding.UTF32),
        new EncodingOption("Latin-1 (ISO-8859-1)", Encoding.Latin1),
        new EncodingOption("Windows-1252 (CP1252)", Encoding.GetEncoding(1252)),
        new EncodingOption("GB2312", Encoding.GetEncoding("GB2312")),
        new EncodingOption("GBK", Encoding.GetEncoding("GBK")),
        new EncodingOption("GB18030", Encoding.GetEncoding("GB18030")),
        new EncodingOption("Shift_JIS", Encoding.GetEncoding("Shift_JIS")),
    };
}
