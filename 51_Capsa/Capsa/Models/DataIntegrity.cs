using System;

namespace Capsa.Models
{
    /// <summary>
    /// 存储卡数据完整性：整卡数据哈希。
    /// 下位机只存储不计算，因此算法由上位机定义 —— 这里固定使用 <b>FNV-1a 64 位</b>
    /// （速度快、无查表，Python / C# 都容易复现；协议文档里也写明同一算法）。
    /// </summary>
    public static class DataIntegrity
    {
        private const ulong FnvOffsetBasis = 14695981039346656037UL;
        private const ulong FnvPrime = 1099511628211UL;

        /// <summary>对整段数据求 64 位 FNV-1a 哈希，返回 16 位大写十六进制。</summary>
        public static string ComputeHashHex(ReadOnlySpan<byte> data)
            => ComputeHash(data).ToString("X16", System.Globalization.CultureInfo.InvariantCulture);

        public static ulong ComputeHash(ReadOnlySpan<byte> data)
        {
            var hash = FnvOffsetBasis;
            foreach (var b in data)
            {
                hash ^= b;
                hash *= FnvPrime;
            }

            return hash;
        }

        /// <summary>读取校验结论。</summary>
        public enum Verdict
        {
            /// <summary>哈希一致。</summary>
            Ok = 0,

            /// <summary>哈希不一致：数据可能已损坏。</summary>
            Mismatch = 1,

            /// <summary>卡上没有哈希记录（还没用本工具写过）。</summary>
            NoRecord = 2,

            /// <summary>读的字节数与已使用空间不一致（全卡回退读取），不具可比性。</summary>
            Skipped = 3,
        }

        /// <summary>
        /// 校验读回的数据：与卡上记录的哈希比对。
        /// 无论结论如何都会给出 <paramref name="actualHash"/>，便于界面显示与排障。
        /// </summary>
        public static Verdict Verify(
            ReadOnlySpan<byte> data,
            string? storedHash,
            int expectedLength,
            out string actualHash)
        {
            actualHash = ComputeHashHex(data);

            // 空哈希与“全 0 哈希”都表示卡上没有有效记录：
            // 格式化 / 重新初始化会用 8 个 0 覆盖哈希，这是占位值而不是真实指纹。
            if (IsNoRecordHash(storedHash))
            {
                return Verdict.NoRecord;
            }

            if (expectedLength <= 0 || data.Length != expectedLength)
            {
                return Verdict.Skipped;
            }

            return Matches(storedHash, actualHash) ? Verdict.Ok : Verdict.Mismatch;
        }

        /// <summary>哈希缺失或全 0 都视为“无记录”。</summary>
        public static bool IsNoRecordHash(string? hash)
        {
            if (string.IsNullOrWhiteSpace(hash))
            {
                return true;
            }

            foreach (var ch in hash!)
            {
                if (ch != '0')
                {
                    return false;
                }
            }

            return true;
        }

        /// <summary>两个 16 位十六进制哈希是否一致（忽略大小写与空白）。</summary>
        public static bool Matches(string? left, string? right)
            => !string.IsNullOrWhiteSpace(left)
               && !string.IsNullOrWhiteSpace(right)
               && string.Equals(left!.Trim(), right!.Trim(), StringComparison.OrdinalIgnoreCase);
    }
}
