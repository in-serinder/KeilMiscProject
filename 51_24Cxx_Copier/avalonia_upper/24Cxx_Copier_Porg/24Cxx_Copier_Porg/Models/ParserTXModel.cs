using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// Builds the serial command frames that the host (上位机) sends to the
/// simulated lower device (下位机), as described in <c>CMD.md</c>.
///
/// Every frame has the form <c>&gt;content&lt;\r\n</c> — it starts with '&gt;',
/// ends with '&lt;' and is followed by a CR LF inter-frame gap.
///
/// This class only *builds* command strings from parameters; it does not touch
/// the serial port (see <see cref="SerialObjectHelperModel"/> for transport).
/// </summary>
public static class ParserTXModel
{
    /// <summary>Frame start marker.</summary>
    public const char FrameStart = '>';

    /// <summary>Frame end marker.</summary>
    public const char FrameEnd = '<';

    /// <summary>Inter-frame gap appended after every frame.</summary>
    public const string FrameGap = "\r\n";

    /// <summary>Bytes per data packet (fixed by the protocol).</summary>
    public const int PacketSize = 16;

    /// <summary>Lowest valid 7-bit I2C chip address.</summary>
    public const byte MinI2cAddress = 0x50;

    /// <summary>Highest valid 7-bit I2C chip address.</summary>
    public const byte MaxI2cAddress = 0x57;

    // ---------------------------------------------------------------------
    // Generic frame builder
    // ---------------------------------------------------------------------

    /// <summary>
    /// Wraps arbitrary content in a protocol frame: <c>&gt;content&lt;\r\n</c>.
    /// </summary>
    /// <param name="content">Raw frame content (no newline allowed).</param>
    /// <returns>The framed command string.</returns>
    /// <exception cref="ArgumentException">
    /// Thrown when the content is null/empty or contains a line break.
    /// </exception>
    public static string BuildFrame(string content)
    {
        if (string.IsNullOrEmpty(content))
        {
            throw new ArgumentException("Frame content must not be empty.", nameof(content));
        }

        if (content.IndexOf('\r') >= 0 || content.IndexOf('\n') >= 0)
        {
            throw new ArgumentException("Frame content must not contain line breaks.", nameof(content));
        }

        return string.Concat(FrameStart, content, FrameEnd, FrameGap);
    }

    // ---------------------------------------------------------------------
    // Session / routing commands
    // ---------------------------------------------------------------------

    /// <summary>
    /// <c>&gt;ROUTER_0x50&lt;</c> — selects the target I2C chip address.
    /// </summary>
    /// <param name="i2cAddress">7-bit I2C chip address (0x50 ~ 0x57).</param>
    public static string Router(byte i2cAddress)
    {
        return BuildFrame($"ROUTER_0x{i2cAddress:X2}");
    }

    /// <summary>
    /// <c>&gt;EDGE_0x007F&lt;</c> — sets the end address of the transfer.
    /// </summary>
    /// <param name="endAddress">Last address of the transfer.</param>
    public static string Edge(int endAddress)
    {
        if (endAddress < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(endAddress));
        }

        // Protocol examples use a 4-digit hex field for the defined devices
        // (max 0x1FFFF => 5 digits for 24C1024). Keep it right-sized.
        var digits = endAddress <= 0xFFFF ? 4 : 5;
        return BuildFrame($"EDGE_0x{endAddress.ToString($"X{digits}", CultureInfo.InvariantCulture)}");
    }

    /// <summary>
    /// <c>&gt;EDGE_0x007F&lt;</c> — sets the end address using a chip's maximum
    /// address.
    /// </summary>
    public static string Edge(ChipModel chip)
    {
        ArgumentNullException.ThrowIfNull(chip);
        return Edge(chip.AddressEnd);
    }

    /// <summary><c>&gt;INT_R&lt;</c> — starts the read flow.</summary>
    public static string IntRead() => BuildFrame("INT_R");

    /// <summary><c>&gt;INT_W&lt;</c> — starts the write flow.</summary>
    public static string IntWrite() => BuildFrame("INT_W");

    // ---------------------------------------------------------------------
    // Flow control
    // ---------------------------------------------------------------------

    /// <summary><c>&gt;RX-OK&lt;</c> — acknowledges a received data packet.</summary>
    public static string RxOk() => BuildFrame("RX-OK");

    /// <summary><c>&gt;TX-OVER&lt;</c> — signals the end of the write flow.</summary>
    public static string TxOver() => BuildFrame("TX-OVER");

    /// <summary><c>&gt;ABORT&lt;</c> — aborts the current task.</summary>
    public static string Abort() => BuildFrame("ABORT");

    /// <summary><c>&gt;TYPE_ECHO&lt;</c> — queries whether the device is the work target.</summary>
    public static string TypeEcho() => BuildFrame("TYPE_ECHO");

    // ---------------------------------------------------------------------
    // Data packets
    // ---------------------------------------------------------------------

    /// <summary>
    /// <c>&gt;W_[d1,...,d16]&lt;</c> — builds a write packet.
    ///
    /// The packet is padded with <c>0x00</c> up to <see cref="PacketSize"/> (16)
    /// bytes. When the payload is longer than 16 bytes the extra bytes are
    /// ignored (the protocol sends one 16-byte packet at a time).
    /// </summary>
    /// <param name="data">Payload bytes (up to 16).</param>
    public static string WritePacket(byte[] data)
    {
        return BuildFrame("W_" + FormatDataArray(data ?? ReadOnlySpan<byte>.Empty,
            data?.Length ?? 0));
    }

    /// <summary>
    /// <c>&gt;W_[d1,...,d16]&lt;</c> — builds a write packet from a span,
    /// only the first <paramref name="length"/> bytes are meaningful, the rest
    /// are padded with <c>0x00</c>.
    /// </summary>
    public static string WritePacket(ReadOnlySpan<byte> data, int length)
    {
        return BuildFrame("W_" + FormatDataArray(data, length));
    }

    // ---------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------

    /// <summary>
    /// Formats a byte array as the protocol data list <c>[XX,XX,...,XX]</c>,
    /// padding/truncating to exactly <see cref="PacketSize"/> bytes, uppercase
    /// hex, comma-separated.
    /// </summary>
    public static string FormatDataArray(byte[] data)
    {
        return FormatDataArray(data ?? ReadOnlySpan<byte>.Empty,
            data?.Length ?? 0);
    }

    /// <summary>
    /// Formats the first <paramref name="length"/> bytes of a span as the
    /// protocol data list, padding to exactly <see cref="PacketSize"/> bytes.
    /// </summary>
    public static string FormatDataArray(ReadOnlySpan<byte> data, int length)
    {
        if (length < 0)
        {
            length = 0;
        }

        if (length > data.Length)
        {
            length = data.Length;
        }

        // Clamp to one packet.
        if (length > PacketSize)
        {
            length = PacketSize;
        }

        var sb = new StringBuilder(PacketSize * 3 + 2);
        sb.Append('[');

        for (var i = 0; i < PacketSize; i++)
        {
            if (i > 0)
            {
                sb.Append(',');
            }

            var value = i < length ? data[i] : (byte)0x00;
            sb.Append(value.ToString("X2", CultureInfo.InvariantCulture));
        }

        sb.Append(']');
        return sb.ToString();
    }

    /// <summary>
    /// Formats raw bytes as a tight uppercase hex string (no separators),
    /// e.g. <c>00 1A FF</c> =&gt; <c>"001AFF"</c>. Useful for logging.
    /// </summary>
    public static string ToHexString(byte[] data)
    {
        if (data is null || data.Length == 0)
        {
            return string.Empty;
        }

        var sb = new StringBuilder(data.Length * 2);
        foreach (var b in data)
        {
            sb.Append(b.ToString("X2", CultureInfo.InvariantCulture));
        }

        return sb.ToString();
    }

    /// <summary>
    /// Splits a payload into fixed 16-byte write packets. The final packet is
    /// padded with <c>0x00</c> by <see cref="FormatDataArray(ReadOnlySpan{byte}, int)"/>.
    /// </summary>
    /// <param name="data">Full payload to send.</param>
    /// <returns>One framed <c>W_</c> command per 16-byte block.</returns>
    public static IReadOnlyList<string> WritePackets(byte[] data)
    {
        var result = new List<string>();
        if (data is null || data.Length == 0)
        {
            return result;
        }

        for (var offset = 0; offset < data.Length; offset += PacketSize)
        {
            var remaining = Math.Min(PacketSize, data.Length - offset);
            result.Add(BuildFrame("W_" + FormatDataArray(data.AsSpan(offset, remaining), remaining)));
        }

        return result;
    }
}