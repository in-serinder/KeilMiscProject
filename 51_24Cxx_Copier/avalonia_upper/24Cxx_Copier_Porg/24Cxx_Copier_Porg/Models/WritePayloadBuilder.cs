using System;
using System.Globalization;
using System.Text;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// Builds the binary payload that will be streamed to a chip from the writing
/// mode selected in the UI.
///
/// Every mode ultimately produces a <c>byte[]</c> of a given length; that length
/// is what drives the <c>&gt;EDGE_0xXXXX&lt;</c> end address and the number of
/// 16-byte <c>&gt;W_[..]&lt;</c> packets.
/// </summary>
public static class WritePayloadBuilder
{
    /// <summary>Outcome of a payload build attempt.</summary>
    public readonly record struct Result(bool Success, byte[] Data, string Error)
    {
        public static Result Ok(byte[] data) => new(true, data, string.Empty);
        public static Result Fail(string error) => new(false, Array.Empty<byte>(), error);
    }

    /// <summary>
    /// Builds the payload for the given <paramref name="mode"/>.
    /// </summary>
    /// <param name="mode">Selected writing mode.</param>
    /// <param name="text">Text payload (text mode).</param>
    /// <param name="filePath">File path (binary-file mode).</param>
    /// <param name="customHex">Custom fill byte, e.g. "0xAA" (custom mode).</param>
    /// <param name="fillLength">
    /// Number of bytes to generate for the fill modes (normally the selected
    /// chip's full capacity, so a fill writes the whole chip).
    /// </param>
    public static Result Build(WritingMode mode, string? text, string? filePath, string? customHex,
        int fillLength = 0)
    {
        switch (mode)
        {
            case WritingMode.Text:
                return BuildText(text);

            case WritingMode.BinaryFile:
                return BuildFromFile(filePath);

            case WritingMode.Fill0xFF:
                return BuildFill(0xFF, fillLength);

            case WritingMode.Fill0x00:
                return BuildFill(0x00, fillLength);

            case WritingMode.FillCustom:
                return BuildCustomFill(customHex, fillLength);

            default:
                return Result.Fail($"Unsupported writing mode: {mode}");
        }
    }

    /// <summary>
    /// Generates a buffer of <paramref name="length"/> bytes all equal to
    /// <paramref name="value"/> (a full-chip fill).
    /// </summary>
    public static Result BuildFill(byte value, int length)
    {
        if (length <= 0)
        {
            return Result.Fail("Fill length is unknown (no chip selected).");
        }

        var buffer = new byte[length];
        for (var i = 0; i < buffer.Length; i++)
        {
            buffer[i] = value;
        }

        return Result.Ok(buffer);
    }

    /// <summary>Text is encoded as UTF-8 (no BOM).</summary>
    public static Result BuildText(string? text)
    {
        if (string.IsNullOrEmpty(text))
        {
            return Result.Fail("Text content is empty.");
        }

        return Result.Ok(new UTF8Encoding(false).GetBytes(text));
    }

    /// <summary>Reads any file as raw bytes (no text interpretation).</summary>
    public static Result BuildFromFile(string? filePath)
    {
        if (string.IsNullOrWhiteSpace(filePath))
        {
            return Result.Fail("No file selected.");
        }

        try
        {
            var bytes = System.IO.File.ReadAllBytes(filePath);
            if (bytes.Length == 0)
            {
                return Result.Fail("The selected file is empty.");
            }

            return Result.Ok(bytes);
        }
        catch (Exception ex)
        {
            return Result.Fail($"Failed to read file: {ex.Message}");
        }
    }

    /// <summary>Parses a fill byte from a "0xAA" / "AA" style string and repeats it.</summary>
    public static Result BuildCustomFill(string? customHex, int length)
    {
        if (!TryParseByte(customHex, out var value))
        {
            return Result.Fail("Invalid custom fill value. Use a hex byte, e.g. 0xAA.");
        }

        return BuildFill(value, length);
    }

    /// <summary>
    /// Parses "0xAA", "AA", "0Xaa", or decimal "170" into a byte.
    /// </summary>
    public static bool TryParseByte(string? text, out byte value)
    {
        value = 0;
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        var s = text.Trim();

        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            s = s.Substring(2);
            return byte.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out value);
        }

        if (byte.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out value))
        {
            return true;
        }

        // Fall back to hex without a prefix.
        return byte.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out value);
    }
}
