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

        /// <summary>已写入容量（字节），来自 24C02 的已使用空间字段（UG / UGS）。</summary>
        public int UsedBytes { get; init; }

        public DateTimeOffset LastWriteTime { get; init; }

        public DateTimeOffset LastAccessTime { get; init; }

        /// <summary>健康分 0-100。</summary>
        public int HealthScore { get; init; }

        /// <summary>阵列总容量（保证不为 0）。</summary>
        public int ArrayTotalBytes => ArrayCapacityBytes > 0 ? ArrayCapacityBytes : CapacityBytes;

        /// <summary>阵列已用百分比。已使用空间是整卡口径，不能用单片容量做分母。</summary>
        public double UsagePercent => ArrayTotalBytes <= 0
            ? 0
            : Math.Round(UsedBytes * 100d / ArrayTotalBytes, 1);
    }
}
