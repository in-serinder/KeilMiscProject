using System;

namespace Capsa.Models
{
    /// <summary>容量 / 长度的统一格式化，避免各视图各写一套。</summary>
    public static class ByteSize
    {
        public static string Format(long bytes)
        {
            if (bytes < 0)
            {
                bytes = 0;
            }

            if (bytes < 1024)
            {
                return $"{bytes} B";
            }

            double value = bytes;
            string[] units = { "KB", "MB", "GB" };

            foreach (var unit in units)
            {
                value /= 1024d;
                if (value < 1024d || unit == "GB")
                {
                    return $"{value:0.##} {unit}";
                }
            }

            return $"{bytes} B";
        }

        /// <summary>十六进制长度表示，例如 0x2000。</summary>
        public static string ToHex(long length) => $"0x{length:X}";

        public static string Percent(double percent) => $"{percent:0.#}%";
    }
}
