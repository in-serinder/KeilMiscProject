using System;
using System.Globalization;

namespace PDL_upper.Models
{
    /// <summary>
    /// 自适应单位格式化：绝对值 &lt; 1 显示毫单位（mV/mA），≥ 1 显示原单位。
    /// 曲线轴刻度 / 游标气泡 / 统计行统一走这里，口径一致。
    /// </summary>
    public static class UnitFmt
    {
        public static string Volts(double v) => Math.Abs(v) < 1
            ? (v * 1000).ToString("0.##", CultureInfo.InvariantCulture) + " mV"
            : v.ToString("0.###", CultureInfo.InvariantCulture) + " V";

        public static string Amps(double a) => Math.Abs(a) < 1
            ? (a * 1000).ToString("0.##", CultureInfo.InvariantCulture) + " mA"
            : a.ToString("0.###", CultureInfo.InvariantCulture) + " A";

        public static string Watts(double w) => Math.Abs(w) < 1
            ? (w * 1000).ToString("0.##", CultureInfo.InvariantCulture) + " mW"
            : Math.Abs(w) >= 1000
                ? (w / 1000).ToString("0.###", CultureInfo.InvariantCulture) + " kW"
                : w.ToString("0.###", CultureInfo.InvariantCulture) + " W";

        /// <summary>轴刻度短格式。unit："V" / "A" / "W" / "℃"。</summary>
        public static string Axis(double v, string unit) => unit switch
        {
            "V" => Math.Abs(v) < 1
                ? (v * 1000).ToString("0.##", CultureInfo.InvariantCulture) + " mV"
                : v.ToString("0.##", CultureInfo.InvariantCulture) + " V",
            "A" => Math.Abs(v) < 1
                ? (v * 1000).ToString("0.##", CultureInfo.InvariantCulture) + " mA"
                : v.ToString("0.##", CultureInfo.InvariantCulture) + " A",
            "W" => Watts(v),
            "℃" => v.ToString("0.#", CultureInfo.InvariantCulture) + " ℃",
            _ => v.ToString("0.###", CultureInfo.InvariantCulture),
        };

        /// <summary>按单位格式化单个值（游标/统计用）。unit："V" / "A" / "℃"。</summary>
        public static string Value(double v, string unit) => unit switch
        {
            "V" => Volts(v),
            "A" => Amps(v),
            "W" => Watts(v),
            "℃" => v.ToString("0.##", CultureInfo.InvariantCulture) + " ℃",
            _ => v.ToString("0.###", CultureInfo.InvariantCulture),
        };
    }
}
