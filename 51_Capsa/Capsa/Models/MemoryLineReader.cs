using System;
using HexView.Avalonia.Model;

namespace Capsa.Models
{
    /// <summary>
    /// 基于内存缓冲区的 <see cref="ILineReader"/>，用于把 EEPROM 镜像直接喂给
    /// HexView 控件，无需落盘。缓冲区为共享引用，因此原地修改后重新渲染即可。
    /// </summary>
    public sealed class MemoryLineReader : ILineReader
    {
        private readonly byte[] _data;

        public MemoryLineReader(byte[] data)
        {
            _data = data ?? Array.Empty<byte>();
        }

        public long Length => _data.Length;

        public byte[] GetLine(long lineNumber, int width)
        {
            var line = new byte[width];
            var offset = lineNumber * width;

            for (var i = 0; i < width; i++)
            {
                var position = offset + i;
                if (position >= _data.Length)
                {
                    break;
                }

                line[i] = _data[position];
            }

            return line;
        }

        public int Read(long offset, byte[] buffer, int count)
        {
            if (offset < 0 || offset >= _data.Length || count <= 0)
            {
                return 0;
            }

            var length = (int)Math.Min(count, _data.Length - offset);
            Array.Copy(_data, offset, buffer, 0, length);
            return length;
        }

        public void Dispose()
        {
        }
    }
}
