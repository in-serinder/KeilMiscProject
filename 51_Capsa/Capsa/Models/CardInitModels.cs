using System;

namespace Capsa.Models
{
    /// <summary>初始化阶段。</summary>
    public enum CardInitStage
    {
        Idle = 0,
        WritingInfo = 1,
        Calibrating = 2,
        TestingChips = 3,
        ChipsPassed = 4,
        Completed = 5,
        Failed = 6,
        Canceled = 7,
    }

    /// <summary>初始化进度快照。</summary>
    public readonly record struct CardInitProgress(CardInitStage Stage, double Percent, string Message);

    /// <summary>初始化请求（界面上半部分填写的内容）。</summary>
    public sealed class CardInitRequest
    {
        public required string PortName { get; init; }

        public required string DeviceName { get; init; }

        public required ChipDefinition Chip { get; init; }

        public required StorageMediumOption Medium { get; init; }
    }

    /// <summary>初始化结果（界面下半部分展示的属性参数）。</summary>
    public sealed class CardInitResult
    {
        public required string DeviceName { get; init; }

        public required ChipDefinition Chip { get; init; }

        public required StorageMediumOption Medium { get; init; }

        /// <summary>初始化标志：1 = 已初始化。</summary>
        public int InitFlag { get; init; } = 1;

        /// <summary>芯片健康检查结果（0 - 7）。</summary>
        public int HealthyChips { get; init; }

        /// <summary>阵列容量 = 单芯片容量 × 正常芯片数量。</summary>
        public long ArrayCapacityBytes { get; init; }

        /// <summary>分配的六位 UID。</summary>
        public required string Uid { get; init; }

        public DateTimeOffset InitializedAt { get; init; }
    }
}
