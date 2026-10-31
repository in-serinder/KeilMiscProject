using System;

namespace Capsa.Models
{
    /// <summary>单个 I2C 从地址（0x50 - 0x57）对应的存储单元状态。</summary>
    public sealed class SlotStatus
    {
        /// <summary>7 位 I2C 从地址，例如 0x50。</summary>
        public int Address { get; init; }

        /// <summary>该地址上是否检测到芯片。</summary>
        public bool Present { get; init; }

        public int CapacityBytes { get; init; }

        public int UsedBytes { get; init; }

        /// <summary>健康分 0-100。</summary>
        public int HealthScore { get; init; }

        /// <summary>写保护标志 WP：true = 芯片处于写保护状态。</summary>
        public bool WriteProtected { get; init; }

        /// <summary>写使能锁存 WEL：true = 写使能已置位。</summary>
        public bool WriteEnableLatch { get; init; }

        public double OccupancyPercent => CapacityBytes <= 0
            ? 0
            : Math.Round(UsedBytes * 100d / CapacityBytes, 1);
    }
}
