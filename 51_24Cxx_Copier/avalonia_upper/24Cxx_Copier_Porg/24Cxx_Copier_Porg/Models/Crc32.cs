using System;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// CRC-32 (IEEE 802.3, the same polynomial as zlib / PNG / zip).
///
/// Used to decide whether a freshly read-back buffer still starts with the
/// bytes of the file that was written earlier. A checksum is needed because the
/// chip is usually larger than the file: reading the whole chip returns the
/// file bytes followed by whatever was left in the unused area, so a plain
/// byte-by-byte comparison against the file would always fail.
/// </summary>
public static class Crc32
{
    private static readonly uint[] Table = BuildTable();

    private static uint[] BuildTable()
    {
        var table = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            var c = i;
            for (var bit = 0; bit < 8; bit++)
            {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }

            table[i] = c;
        }

        return table;
    }

    /// <summary>
    /// Computes the CRC-32 of the first <paramref name="length"/> bytes (or the
    /// whole span when <paramref name="length"/> is negative / too large).
    /// </summary>
    public static uint Compute(ReadOnlySpan<byte> data, int length = -1)
    {
        var count = length < 0 ? data.Length : Math.Min(length, data.Length);

        var crc = 0xFFFFFFFFu;
        for (var i = 0; i < count; i++)
        {
            crc = Table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        }

        return crc ^ 0xFFFFFFFFu;
    }
}
