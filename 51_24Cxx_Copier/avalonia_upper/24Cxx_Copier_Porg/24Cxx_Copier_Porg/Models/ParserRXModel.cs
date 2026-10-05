using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// Kind of a frame received from the simulated lower device (下位机).
/// Mirrors the table in <c>CMD.md §5.2</c>.
/// </summary>
public enum SerialResponseKind
{
    /// <summary>Not recognised / malformed frame.</summary>
    Unknown = 0,

    /// <summary><c>&gt;R-S&lt;</c> — everything OK.</summary>
    Success,

    /// <summary><c>&gt;R-ERR&lt;</c> — generic error.</summary>
    Error,

    /// <summary><c>&gt;YES&lt;</c> — answer to <c>TYPE_ECHO</c>.</summary>
    Yes,

    /// <summary><c>&gt;RX-OK&lt;</c> — a data packet was received.</summary>
    RxOk,

    /// <summary><c>&gt;TX-OVER&lt;</c> — read flow finished.</summary>
    TxOver,

    /// <summary><c>&gt;R_[d1,...,d16]&lt;</c> — a packet of read data.</summary>
    ReadData,
}

/// <summary>
/// A parsed frame received from the lower device.
/// </summary>
public sealed class SerialResponse
{
    /// <summary>Parsed frame kind.</summary>
    public SerialResponseKind Kind { get; init; } = SerialResponseKind.Unknown;

    /// <summary>Raw content between the '&gt;' and '&lt;' markers.</summary>
    public string Content { get; init; } = string.Empty;

    /// <summary>
    /// Decoded payload for <see cref="SerialResponseKind.ReadData"/> frames,
    /// otherwise <c>null</c>. Always <see cref="ParserTXModel.PacketSize"/>
    /// bytes for read packets.
    /// </summary>
    public byte[]? Data { get; init; }

    /// <summary>True for a healthy frame (<c>R-S</c>).</summary>
    public bool IsSuccess => Kind == SerialResponseKind.Success;

    /// <summary>True for an error frame (<c>R-ERR</c>).</summary>
    public bool IsError => Kind == SerialResponseKind.Error;

    /// <summary>True for a read-data frame.</summary>
    public bool HasData => Kind == SerialResponseKind.ReadData;

    public override string ToString() =>
        Data is null ? $"{Kind}: {Content}" : $"{Kind}: [{Data.Length} bytes]";
}

/// <summary>
/// Parses the strings received on the serial port into typed parameters, per
/// the frame rules in <c>CMD.md</c>.
///
/// The parser is deliberately forgiving about the inter-frame gap
/// (<c>\r\n</c>, bare <c>\n</c>, bare <c>\r</c> or any combination) and about
/// garbage that appears before the '&gt;' start marker. A frame is complete
/// once the '&lt;' end marker is seen.
///
/// Typical usage:
/// <code>
/// var parser = new ParserRXModel();
/// foreach (var frame in parser.Feed(chunkFromSerialPort))
/// {
///     if (frame.IsSuccess) { ... }
///     else if (frame.HasData) { Use(frame.Data!); }
/// }
/// </code>
/// </summary>
public sealed class ParserRXModel
{
    private readonly StringBuilder _buffer = new();

    /// <summary>Maximum buffered frame length before it is discarded as bogus.</summary>
    public int MaxFrameLength { get; set; } = 1024;

    /// <summary>Raised when a frame's content contains an illegal line break.</summary>
    public event EventHandler<string>? InvalidFrame;

    /// <summary>
    /// Feeds a chunk of received text and returns every complete frame that
    /// became available. Incomplete data is buffered until the next call.
    /// </summary>
    public IReadOnlyList<SerialResponse> Feed(string? chunk)
    {
        var frames = new List<SerialResponse>();
        if (string.IsNullOrEmpty(chunk))
        {
            return frames;
        }

        foreach (var ch in chunk)
        {
            // Ignore everything before the start marker; the gap bytes
            // (\r, \n) between frames are naturally dropped here too.
            if (!IsReceiving)
            {
                if (ch == ParserTXModel.FrameStart)
                {
                    IsReceiving = true;
                    _buffer.Clear();
                }

                continue;
            }

            if (ch == ParserTXModel.FrameEnd)
            {
                IsReceiving = false;
                frames.Add(ParseContent(_buffer.ToString()));
                _buffer.Clear();
                continue;
            }

            // Illegal line break inside a frame => drop the frame.
            if (ch == '\r' || ch == '\n')
            {
                IsReceiving = false;
                var bad = _buffer.ToString();
                _buffer.Clear();
                InvalidFrame?.Invoke(this, bad);
                frames.Add(new SerialResponse
                {
                    Kind = SerialResponseKind.Unknown,
                    Content = bad,
                });
                continue;
            }

            _buffer.Append(ch);

            // Guard against a runaway stream without an end marker.
            if (_buffer.Length > MaxFrameLength)
            {
                IsReceiving = false;
                _buffer.Clear();
                frames.Add(new SerialResponse
                {
                    Kind = SerialResponseKind.Unknown,
                    Content = string.Empty,
                });
            }
        }

        return frames;
    }

    /// <summary>True while the parser is inside a frame (after '&gt;').</summary>
    public bool IsReceiving { get; private set; }

    /// <summary>Clears any partially buffered frame.</summary>
    public void Reset()
    {
        _buffer.Clear();
        IsReceiving = false;
    }

    // ---------------------------------------------------------------------
    // Single-frame parsing
    // ---------------------------------------------------------------------

    /// <summary>
    /// Parses a single complete frame. Accepts the raw form
    /// (<c>&gt;R-S&lt;\r\n</c>), the bare content (<c>R-S</c>) or anything in
    /// between; surrounding whitespace and stray markers are tolerated.
    /// </summary>
    /// <returns>
    /// The parsed <see cref="SerialResponse"/>, or one with
    /// <see cref="SerialResponseKind.Unknown"/> when the frame is malformed.
    /// </returns>
    public static SerialResponse Parse(string? raw)
    {
        var content = ExtractContent(raw);
        return ParseContent(content);
    }

    /// <summary>Convenience wrapper around <see cref="Parse(string)"/>.</summary>
    public static bool TryParse(string? raw, out SerialResponse response)
    {
        response = Parse(raw);
        return response.Kind != SerialResponseKind.Unknown;
    }

    /// <summary>
    /// Extracts the content between the first '&gt;' and the first following
    /// '&lt;'. When either marker is missing the trimmed input is used as-is.
    /// </summary>
    public static string ExtractContent(string? raw)
    {
        if (string.IsNullOrEmpty(raw))
        {
            return string.Empty;
        }

        var start = raw.IndexOf(ParserTXModel.FrameStart);
        var end = raw.IndexOf(ParserTXModel.FrameEnd);

        if (start >= 0 && end > start)
        {
            return raw.Substring(start + 1, end - start - 1).Trim();
        }

        return raw.Trim(MarkerTrimChars);
    }

    private static readonly char[] MarkerTrimChars = { '>', '<', '\r', '\n', ' ', '\t' };

    /// <summary>Parses the inner content of a frame (no markers).</summary>
    public static SerialResponse ParseContent(string content)
    {
        content = content ?? string.Empty;
        var trimmed = content.Trim();

        switch (trimmed)
        {
            case "R-S":
                return new SerialResponse { Kind = SerialResponseKind.Success, Content = trimmed };
            case "R-ERR":
                return new SerialResponse { Kind = SerialResponseKind.Error, Content = trimmed };
            case "YES":
                return new SerialResponse { Kind = SerialResponseKind.Yes, Content = trimmed };
            case "RX-OK":
                return new SerialResponse { Kind = SerialResponseKind.RxOk, Content = trimmed };
            case "TX-OVER":
                return new SerialResponse { Kind = SerialResponseKind.TxOver, Content = trimmed };
        }

        // Read data packet: R_[..]
        if (trimmed.StartsWith("R_", StringComparison.Ordinal))
        {
            var data = TryParseDataArray(trimmed.Substring(2), expectedPrefix: '[');
            if (data is not null)
            {
                return new SerialResponse
                {
                    Kind = SerialResponseKind.ReadData,
                    Content = trimmed,
                    Data = data,
                };
            }
        }

        return new SerialResponse { Kind = SerialResponseKind.Unknown, Content = trimmed };
    }

    // ---------------------------------------------------------------------
    // Parameter parsing helpers (for the upper device)
    // ---------------------------------------------------------------------

    /// <summary>
    /// Parses the payload list <c>[XX,XX,...]</c> of a data frame. Returns
    /// <c>null</c> when the syntax is invalid. The result is padded/truncated
    /// to <see cref="ParserTXModel.PacketSize"/> bytes.
    /// </summary>
    public static byte[]? TryParseDataArray(string? text, char expectedPrefix = '[')
    {
        if (string.IsNullOrEmpty(text))
        {
            return null;
        }

        var s = text.Trim();

        // Drop an optional "W_" / "R_" verb prefix.
        if (s.StartsWith("W_", StringComparison.Ordinal) ||
            s.StartsWith("R_", StringComparison.Ordinal))
        {
            s = s.Substring(2).Trim();
        }

        if (s.Length < 2 || s[0] != expectedPrefix || s[^1] != ']')
        {
            return null;
        }

        var inner = s.Substring(1, s.Length - 2);
        if (inner.Length == 0)
        {
            return null;
        }

        var parts = inner.Split(',');
        var bytes = new List<byte>(parts.Length);

        foreach (var part in parts)
        {
            var token = part.Trim();
            if (token.Length != 2 ||
                !byte.TryParse(token, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var b))
            {
                return null;
            }

            bytes.Add(b);
        }

        // Normalise to one packet.
        var result = new byte[ParserTXModel.PacketSize];
        for (var i = 0; i < result.Length; i++)
        {
            result[i] = i < bytes.Count ? bytes[i] : (byte)0x00;
        }

        return result;
    }

    /// <summary>
    /// Parses a <c>ROUTER_0x50</c> style chip address. Returns <c>null</c> when
    /// invalid or outside 0x50 ~ 0x57.
    /// </summary>
    public static byte? TryParseRouterAddress(string? content)
    {
        if (!TryParseHexAfterUnderscore(content, "ROUTER_0x", out var value))
        {
            return null;
        }

        if (value < ParserTXModel.MinI2cAddress || value > ParserTXModel.MaxI2cAddress)
        {
            return null;
        }

        return (byte)value;
    }

    /// <summary>
    /// Parses an <c>EDGE_0x007F</c> style end address. Returns <c>null</c> when
    /// invalid.
    /// </summary>
    public static int? TryParseEdgeAddress(string? content)
    {
        if (!TryParseHexAfterUnderscore(content, "EDGE_0x", out var value))
        {
            return null;
        }

        return value;
    }

    private static bool TryParseHexAfterUnderscore(string? content, string prefix, out int value)
    {
        value = 0;
        if (string.IsNullOrEmpty(content))
        {
            return false;
        }

        // Accept both the bare content and the framed form.
        var s = ExtractContent(content).Trim();
        if (!s.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        var hex = s.Substring(prefix.Length).Trim();
        if (hex.Length == 0)
        {
            return false;
        }

        return int.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out value);
    }
}
