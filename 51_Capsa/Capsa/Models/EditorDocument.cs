using System;
using System.Collections.Generic;
using System.Text;

namespace Capsa.Models
{
    /// <summary>
    /// 编辑器共享缓冲区。HEX / 文本 / 双显三种视图都读写同一个实例，
    /// 通过 <see cref="Changed"/> 事件把变更来源广播出去。
    /// </summary>
    public sealed class EditorDocument
    {
        private byte[] _bytes = Array.Empty<byte>();

        public event EventHandler<EditorDocumentChangedEventArgs>? Changed;

        public int Length => _bytes.Length;

        public byte[] Snapshot() => (byte[])_bytes.Clone();

        /// <summary>整体替换缓冲区（导入文件 / 从设备读取 / 文本重新编码）。</summary>
        public bool Replace(byte[]? data, EditorChangeSource source)
        {
            var next = data ?? Array.Empty<byte>();
            if (next.AsSpan().SequenceEqual(_bytes))
            {
                return false;
            }

            _bytes = (byte[])next.Clone();
            Changed?.Invoke(this, new EditorDocumentChangedEventArgs(source, 0, _bytes.Length));
            return true;
        }

        /// <summary>按偏移写入若干个字节（来自 HEX 编辑器的编辑日志）。</summary>
        public bool ApplyEdits(IReadOnlyList<(long Offset, byte Value)> edits)
        {
            if (edits.Count == 0)
            {
                return false;
            }

            var changed = false;
            foreach (var (offset, value) in edits)
            {
                if (offset < 0 || offset >= _bytes.Length)
                {
                    continue;
                }

                if (_bytes[offset] == value)
                {
                    continue;
                }

                _bytes[offset] = value;
                changed = true;
            }

            if (changed)
            {
                Changed?.Invoke(this, new EditorDocumentChangedEventArgs(EditorChangeSource.Hex, 0, _bytes.Length));
            }

            return changed;
        }

        public string Decode(Encoding encoding)
            => _bytes.Length == 0 ? string.Empty : encoding.GetString(_bytes);

        /// <summary>用文本重新编码缓冲区，内容一致时返回 false（不触发事件）。</summary>
        public bool Encode(string? text, Encoding encoding)
        {
            var next = encoding.GetBytes(text ?? string.Empty);
            if (next.AsSpan().SequenceEqual(_bytes))
            {
                return false;
            }

            _bytes = next;
            Changed?.Invoke(this, new EditorDocumentChangedEventArgs(EditorChangeSource.Text, 0, _bytes.Length));
            return true;
        }
    }

    public sealed class EditorDocumentChangedEventArgs : EventArgs
    {
        public EditorDocumentChangedEventArgs(EditorChangeSource source, int offset, int length)
        {
            Source = source;
            Offset = offset;
            Length = length;
        }

        public EditorChangeSource Source { get; }

        public int Offset { get; }

        public int Length { get; }
    }
}
