using System;
using HexView.Avalonia.Model;

namespace NFG_Reader.Models
{
    /// <summary>
    /// In-memory <see cref="ILineReader"/> backed by a plain byte array.
    /// Used to render the write-tab text as a hexadecimal view without
    /// touching the file system.
    /// </summary>
    public sealed class MemoryLineReader : ILineReader
    {
        private byte[] _data;

        public MemoryLineReader()
        {
            _data = Array.Empty<byte>();
        }

        public MemoryLineReader(byte[] data)
        {
            _data = data ?? Array.Empty<byte>();
        }

        /// <summary>Replaces the backing buffer.</summary>
        public void SetData(byte[] data)
        {
            _data = data ?? Array.Empty<byte>();
        }

        public long Length => _data.Length;

        public byte[] GetLine(long offset, int length)
        {
            // The HexFormatter indexes the returned array for the full line width,
            // so a line must always be exactly 'length' bytes (zero-padded at the end).
            var line = new byte[length < 0 ? 0 : length];
            if (offset < 0 || offset >= _data.Length || length <= 0)
            {
                return line;
            }

            int count = (int)Math.Min(length, _data.Length - offset);
            Array.Copy(_data, (int)offset, line, 0, count);
            return line;
        }

        public int Read(long offset, byte[] buffer, int count)
        {
            if (offset < 0 || offset >= _data.Length || count <= 0)
            {
                return 0;
            }

            int available = (int)Math.Min(count, _data.Length - offset);
            Array.Copy(_data, (int)offset, buffer, 0, available);
            return available;
        }

        public void Dispose()
        {
            // Nothing unmanaged to release for an in-memory buffer.
        }
    }
}


