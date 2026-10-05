using System;
using HexView.Avalonia.Model;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// An <see cref="ILineReader"/> backed by an in-memory byte buffer. Used to
/// feed arbitrary binary data (e.g. an EEPROM image) into the HexView control
/// without touching the file system.
/// </summary>
public sealed class MemoryLineReader : ILineReader
{
    private readonly byte[] _data;

    public MemoryLineReader(byte[] data)
    {
        _data = data ?? Array.Empty<byte>();
    }

    /// <summary>Total number of bytes.</summary>
    public long Length => _data.Length;

    /// <summary>Reads a single logical line of <paramref name="width"/> bytes.</summary>
    public byte[] GetLine(long lineNumber, int width)
    {
        var bytes = new byte[width];
        var offset = lineNumber * width;

        for (var j = 0; j < width; j++)
        {
            var position = offset + j;
            if (position >= _data.Length)
            {
                break;
            }

            bytes[j] = _data[position];
        }

        return bytes;
    }

    /// <summary>Reads <paramref name="count"/> bytes starting at <paramref name="offset"/>.</summary>
    public int Read(long offset, byte[] buffer, int count)
    {
        if (offset < 0 || offset >= _data.Length || count <= 0)
        {
            return 0;
        }

        var max = (int)Math.Min(count, _data.Length - offset);
        Array.Copy(_data, offset, buffer, 0, max);
        return max;
    }

    public void Dispose()
    {
        // Nothing unmanaged to release for an in-memory buffer.
    }
}
