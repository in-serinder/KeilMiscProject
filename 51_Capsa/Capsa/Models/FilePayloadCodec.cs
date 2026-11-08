using System;
using System.IO;
using System.Linq;
using System.Text;

namespace Capsa.Models
{
    /// <summary>解包结果：原始数据 + 还原出的文件名（无 trailer 时为 null）。</summary>
    public readonly record struct FilePayload(byte[] Data, string? FileName)
    {
        /// <summary>文件名的扩展名（不含点）；无文件名时为 null。</summary>
        public string? Extension
        {
            get
            {
                if (string.IsNullOrEmpty(FileName))
                {
                    return null;
                }

                var ext = FilePayloadCodec.NormalizeExtension(Path.GetExtension(FileName));
                return ext.Length == 0 ? null : ext;
            }
        }
    }

    /// <summary>
    /// 导入 / 导出的文件载荷编解码。
    /// 写入时把原文件名附在数据末尾（最多 64 字节 UTF-8），布局为：
    /// <code>[data…][文件名 UTF-8][长度 1B][魔数 "CSNX" 4B]</code>
    /// 读取时从尾部识别魔数，还原文件名并裁掉 trailer；数据本身一个字节都不改。
    /// 同时兼容早期只存扩展名的 "CSFX" 布局。
    /// </summary>
    public static class FilePayloadCodec
    {
        /// <summary>文件名魔数（v2：存完整文件名）。</summary>
        private static readonly byte[] NameMagic = { (byte)'C', (byte)'S', (byte)'N', (byte)'X' };

        /// <summary>旧版魔数（v1：只存扩展名）。</summary>
        private static readonly byte[] ExtensionMagic = { (byte)'C', (byte)'S', (byte)'F', (byte)'X' };

        /// <summary>文件名最长字节数：不超就保留原文件名，超过则退回 <c>dump.&lt;ext&gt;</c>。</summary>
        public const int MaxNameBytes = 64;

        /// <summary>扩展名最长字节数。</summary>
        public const int MaxExtensionLength = 10;

        /// <summary>无文件名时的固定开销：长度字段 1B + 魔数 4B。</summary>
        public const int BaseOverhead = 5;

        /// <summary>规整扩展名：去掉点号、只留字母数字、转小写、截断到 10 字符。</summary>
        public static string NormalizeExtension(string? extension)
        {
            var text = (extension ?? string.Empty).Trim();
            if (text.StartsWith(".", StringComparison.Ordinal))
            {
                text = text[1..];
            }

            var chars = text.Where(char.IsLetterOrDigit).Take(MaxExtensionLength).ToArray();
            return new string(chars).ToLowerInvariant();
        }

        /// <summary>去掉路径与非法字符，得到可安全存进卡里的文件名。</summary>
        public static string SanitizeFileName(string? fileName)
        {
            var name = Path.GetFileName((fileName ?? string.Empty).Trim());
            if (name.Length == 0)
            {
                return string.Empty;
            }

            var invalid = Path.GetInvalidFileNameChars();
            var builder = new StringBuilder(name.Length);
            foreach (var ch in name)
            {
                builder.Append(char.IsControl(ch) || invalid.Contains(ch) ? '_' : ch);
            }

            return builder.ToString().Trim('.', ' ');
        }

        /// <summary>
        /// 决定实际写入卡里的文件名：UTF-8 不超过 64 B 就保留原文件名，超过则用 <c>dump.&lt;ext&gt;</c>，
        /// 无扩展名时用 <c>dump</c>；名字为空则返回空串（不写 trailer 内容）。
        /// </summary>
        public static string BuildStoredName(string? fileName)
        {
            var name = SanitizeFileName(fileName);
            if (name.Length == 0)
            {
                return string.Empty;
            }

            if (Encoding.UTF8.GetByteCount(name) <= MaxNameBytes)
            {
                return name;
            }

            var ext = NormalizeExtension(Path.GetExtension(name));
            return ext.Length > 0 ? $"dump.{ext}" : "dump";
        }

        /// <summary>加了 trailer 之后要占用的字节数。</summary>
        public static int WrappedLength(int dataLength, string? fileName)
        {
            var name = BuildStoredName(fileName);
            return dataLength + BaseOverhead + Encoding.UTF8.GetByteCount(name);
        }

        /// <summary>把数据与文件名打包成实际写入存储卡的字节流。</summary>
        public static byte[] Wrap(ReadOnlySpan<byte> data, string? fileName)
        {
            var nameBytes = Encoding.UTF8.GetBytes(BuildStoredName(fileName));
            if (nameBytes.Length > MaxNameBytes)
            {
                nameBytes = nameBytes[..MaxNameBytes];
            }

            var buffer = new byte[data.Length + BaseOverhead + nameBytes.Length];

            data.CopyTo(buffer);
            nameBytes.CopyTo(buffer, data.Length);
            buffer[data.Length + nameBytes.Length] = (byte)nameBytes.Length;
            NameMagic.CopyTo(buffer, data.Length + nameBytes.Length + 1);

            return buffer;
        }

        /// <summary>从读回的字节流里还原数据与文件名；没有 trailer 时原样返回。</summary>
        public static FilePayload Unwrap(ReadOnlySpan<byte> raw)
        {
            if (raw.Length < BaseOverhead)
            {
                return new FilePayload(raw.ToArray(), null);
            }

            var magicStart = raw.Length - 4;
            var lengthIndex = magicStart - 1;
            var length = raw[lengthIndex];

            if (raw[magicStart..].SequenceEqual(NameMagic))
            {
                if (length > MaxNameBytes)
                {
                    return new FilePayload(raw.ToArray(), null);
                }

                // 长度 0 = 明确写了“无文件名”，仍要裁掉 trailer
                if (length == 0)
                {
                    return new FilePayload(raw[..lengthIndex].ToArray(), null);
                }

                var nameStart = lengthIndex - length;
                if (nameStart < 0)
                {
                    return new FilePayload(raw.ToArray(), null);
                }

                var name = Encoding.UTF8.GetString(raw[nameStart..lengthIndex]);
                if (name.Any(char.IsControl))
                {
                    return new FilePayload(raw.ToArray(), null);
                }

                return new FilePayload(raw[..nameStart].ToArray(), name);
            }

            if (raw[magicStart..].SequenceEqual(ExtensionMagic))
            {
                // v1 兼容：只存扩展名
                if (length > MaxExtensionLength)
                {
                    return new FilePayload(raw.ToArray(), null);
                }

                var extStart = lengthIndex - length;
                if (extStart < 0)
                {
                    return new FilePayload(raw.ToArray(), null);
                }

                var ext = Encoding.ASCII.GetString(raw[extStart..lengthIndex]);
                if (ext.Any(c => !char.IsLetterOrDigit(c)))
                {
                    return new FilePayload(raw.ToArray(), null);
                }

                return new FilePayload(
                    raw[..extStart].ToArray(),
                    ext.Length == 0 ? null : $"dump.{ext.ToLowerInvariant()}");
            }

            return new FilePayload(raw.ToArray(), null);
        }
    }
}
