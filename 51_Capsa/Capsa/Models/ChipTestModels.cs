using System.Collections.Generic;

namespace Capsa.Models
{
    /// <summary>单颗芯片的测试阶段。</summary>
    public enum ChipTestPhase
    {
        Idle = 0,

        /// <summary>写入测试图案（进度条第一遍，紫色）。</summary>
        Writing = 1,

        /// <summary>读回校验（进度条第二遍，绿色）。</summary>
        Reading = 2,

        Completed = 3,
    }

    /// <summary>健康分级：按读回校验的错字节数划分。</summary>
    public enum ChipHealthGrade
    {
        /// <summary>0 个错误字节。</summary>
        Healthy = 0,

        /// <summary>1 - 9 个错误字节。</summary>
        Warning = 1,

        /// <summary>10 - 20 个错误字节（中等健康问题）。</summary>
        Medium = 2,

        /// <summary>大于 20 个错误字节。</summary>
        Error = 3,
    }

    /// <summary>测试进度快照。</summary>
    public readonly record struct ChipTestProgress(
        int Address,
        ChipTestPhase Phase,
        double Percent,
        string Action,
        ChipHealthGrade? Grade = null);

    /// <summary>测试选项。</summary>
    public sealed class ChipTestOptions
    {
        /// <summary>是否追加 0x00 图案的读写校验。</summary>
        public bool TestPattern00 { get; init; }

        /// <summary>
        /// 每颗芯片参与校验的字节数：
        /// <c>0</c> = 整片全写全读（独立健康检查窗口用）；
        /// 正数 = 只测前 N 字节的快速窗口（初始化流程用 256）。
        /// </summary>
        public int TestLengthBytes { get; init; } = 256;
    }

    /// <summary>单颗芯片的测试结论。</summary>
    public sealed class ChipTestOutcome
    {
        public int Address { get; init; }

        public bool Present { get; init; }

        /// <summary>读回校验的错字节总数。</summary>
        public int ErrorBytes { get; init; }

        public ChipHealthGrade Grade { get; init; }

        /// <summary>健康分 0-100（由分级换算）。</summary>
        public int HealthScore => Grade switch
        {
            ChipHealthGrade.Healthy => 100,
            ChipHealthGrade.Warning => 70,
            ChipHealthGrade.Medium => 50,
            _ => 30,
        };

        public HealthLevel Level => Grade switch
        {
            ChipHealthGrade.Healthy => HealthLevel.Healthy,
            ChipHealthGrade.Error => HealthLevel.Error,
            _ => HealthLevel.Warning,
        };

        /// <summary>0-9 级错误字节数 → 分级。</summary>
        public static ChipHealthGrade Classify(int errorBytes) => errorBytes switch
        {
            <= 0 => ChipHealthGrade.Healthy,
            < 10 => ChipHealthGrade.Warning,
            <= 20 => ChipHealthGrade.Medium,
            _ => ChipHealthGrade.Error,
        };
    }

    /// <summary>一次健康检查的完整结果。</summary>
    public sealed class ChipTestReport
    {
        public required IReadOnlyList<ChipTestOutcome> Outcomes { get; init; }

        public int HealthyCount { get; init; }

        public int WarningCount { get; init; }

        public int MediumCount { get; init; }

        public int ErrorCount { get; init; }

        /// <summary>健康掩码：bit0~bit7 ↔ 0x50~0x57，1 = 健康。</summary>
        public int HealthMask
        {
            get
            {
                var mask = 0;
                foreach (var outcome in Outcomes)
                {
                    if (outcome.Present && outcome.Grade == ChipHealthGrade.Healthy)
                    {
                        mask |= 1 << (outcome.Address - 0x50);
                    }
                }

                return mask;
            }
        }
    }
}
