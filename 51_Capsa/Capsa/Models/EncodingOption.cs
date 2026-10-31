using System.Text;

namespace Capsa.Models
{
    /// <summary>编码下拉框条目。编码表由 System.Text.Encoding.CodePages 提供。</summary>
    public sealed class EncodingOption
    {
        public EncodingOption(string displayName, Encoding encoding)
        {
            DisplayName = displayName;
            Encoding = encoding;
        }

        public string DisplayName { get; }

        public Encoding Encoding { get; }

        public override string ToString() => DisplayName;

        /// <summary>下拉框统一使用的编码清单（默认 UTF-8）。</summary>
        public static EncodingOption[] Catalog { get; } =
        {
            new("UTF-8", Encoding.UTF8),
            new("ASCII", Encoding.ASCII),
            new("GB2312", Encoding.GetEncoding("GB2312")),
            new("GBK", Encoding.GetEncoding("GBK")),
            new("GB18030", Encoding.GetEncoding("GB18030")),
            new("UTF-16 LE", Encoding.Unicode),
            new("UTF-16 BE", Encoding.BigEndianUnicode),
            new("UTF-32", Encoding.UTF32),
            new("Latin-1 (ISO-8859-1)", Encoding.Latin1),
            new("Windows-1252 (CP1252)", Encoding.GetEncoding(1252)),
            new("Shift_JIS", Encoding.GetEncoding("Shift_JIS")),
        };
    }
}
