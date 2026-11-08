using System;

namespace Capsa.Models
{
    /// <summary>
    /// 已初始化芯片的静态 + 运行态信息。由 <see cref="Services.IDeviceService"/> 提供，
    /// 界面只负责呈现，不做任何协议解析。
    /// </summary>
    public sealed class DeviceInfo
    {
        public string DeviceName { get; init; } = string.Empty;

        /// <summary>芯片唯一 UID（十六进制字符串）。</summary>
        public string Uid { get; init; } = string.Empty;

        /// <summary>芯片型号，例如 AT24C256。</summary>
        public string ChipModel { get; init; } = string.Empty;

        /// <summary>芯片类型，例如 I2C EEPROM。</summary>
        public string ChipType { get; init; } = string.Empty;

        /// <summary>单片容量（字节）。</summary>
        public int CapacityBytes { get; init; }

        /// <summary>
        /// 阵列总容量（字节）= 单片容量 × 有效芯片数；为 0 时退回 <see cref="CapacityBytes"/>。
        /// </summary>
        public int ArrayCapacityBytes { get; init; }

        /// <summary>WP 掩码，bit0~bit7 ↔ 0x50~0x57；1 = 该片写保护（剔出有效容量）。</summary>
        public int WriteProtectMask { get; init; }

        /// <summary>已写入容量（字节），来自 24C02 的已使用空间字段（UG / UGS）。</summary>
        public int UsedBytes { get; init; }

        /// <summary>整卡数据哈希（16 位十六进制，8 字节），来自 DI / DIG。空串表示卡上还没有记录。</summary>
        public string DataHash { get; init; } = string.Empty;

        /// <summary>累计写入次数，来自 DI / DIG；由上位机递增。</summary>
        public long WriteCount { get; init; }

        /// <summary>哈希的前 8 位，用于界面紧凑显示。</summary>
        public string DataHashShort => DataHash.Length >= 8 ? DataHash[..8] : DataHash;

        public DateTimeOffset LastWriteTime { get; init; }

        public DateTimeOffset LastAccessTime { get; init; }

        /// <summary>健康分 0-100。</summary>
        public int HealthScore { get; init; }

        /// <summary>阵列总容量（保证不为 0）。</summary>
        public int ArrayTotalBytes => ArrayCapacityBytes > 0 ? ArrayCapacityBytes : CapacityBytes;

        /// <summary>写保护片数量。</summary>
        public int ProtectedChips
        {
            get
            {
                var n = 0;
                for (var i = 0; i < 8; i++)
                {
                    if ((WriteProtectMask & (1 << i)) != 0)
                    {
                        n++;
                    }
                }
                return n;
            }
        }

        /// <summary>有效容量 = 总容量 - 写保护片容量。写入 / 读取只路由到非保护片。</summary>
        public int ArrayEffectiveBytes => Math.Max(0, ArrayTotalBytes - ProtectedChips * CapacityBytes);

        /// <summary>写保护容量占总容量的比例（0-100，相对满容量 8 片口径）。</summary>
        public double ProtectedPercent => ArrayTotalBytes <= 0
            ? 0
            : Math.Round(ProtectedChips * (double)CapacityBytes * 100d / ArrayTotalBytes, 1);

        /// <summary>阵列已用百分比。已使用空间是整卡口径，不能用单片容量做分母。</summary>
        public double UsagePercent => ArrayTotalBytes <= 0
            ? 0
            : Math.Round(UsedBytes * 100d / ArrayTotalBytes, 1);
    }
}
