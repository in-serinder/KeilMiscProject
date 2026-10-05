using System.Text;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// A selectable character-encoding option shown in the hex viewer's decoder
/// drop-down. Wraps an <see cref="Encoding"/> together with its display name.
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
}
