using Avalonia.Media;

namespace Capsa.Models
{
    /// <summary>健康等级。阈值：80-100 合格（蓝）/ 60-80 警告（黄）/ 0-60 错误（红）。</summary>
    public enum HealthLevel
    {
        Healthy = 0,
        Warning = 1,
        Error = 2,
    }

    /// <summary>
    /// 健康分 → 等级 / 文本 / 颜色的统一换算，避免各处重复写阈值。
    /// 颜色与 <c>Styles/Tokens.axaml</c> 中的 Capsa.Ok / Capsa.Warn / Capsa.Err 保持一致。
    /// </summary>
    public static class HealthScale
    {
        public const int HealthyThreshold = 80;
        public const int WarningThreshold = 60;

        public static HealthLevel FromScore(int score) => score switch
        {
            >= HealthyThreshold => HealthLevel.Healthy,
            >= WarningThreshold => HealthLevel.Warning,
            _ => HealthLevel.Error,
        };

        /// <summary>卡片 / 徽标上使用的短文本。</summary>
        public static string ToBadgeText(HealthLevel level) => level switch
        {
            HealthLevel.Healthy => "HEALTH",
            HealthLevel.Warning => "WARN",
            _ => "ERR",
        };

        public static string ToDisplayText(HealthLevel level) => level switch
        {
            HealthLevel.Healthy => "Healthy",
            HealthLevel.Warning => "Warning",
            _ => "Critical",
        };

        public static IBrush ToBrush(HealthLevel level) => level switch
        {
            HealthLevel.Healthy => HealthPalette.Ok,
            HealthLevel.Warning => HealthPalette.Warn,
            _ => HealthPalette.Error,
        };

        public static IBrush ToSoftBrush(HealthLevel level) => level switch
        {
            HealthLevel.Healthy => HealthPalette.OkSoft,
            HealthLevel.Warning => HealthPalette.WarnSoft,
            _ => HealthPalette.ErrorSoft,
        };
    }

    /// <summary>健康语义色（与设计令牌同值，供 ViewModel 直接绑定）。</summary>
    public static class HealthPalette
    {
        public static readonly IBrush Ok = new SolidColorBrush(Color.Parse("#2F6BFF"));

        /// <summary>卡片占用填充（浅蓝，与 Tokens 的 Capsa.CardFill 亮色一致）。</summary>
        public static readonly IBrush CardFill = new SolidColorBrush(Color.Parse("#2E2F6BFF"));

        /// <summary>写保护卡片的整块底色（黄，40% 不透明度，保护时一眼可辨）。</summary>
        public static readonly IBrush CardProtectedBg = new SolidColorBrush(Color.Parse("#99E8A33D"));
        public static readonly IBrush Warn = new SolidColorBrush(Color.Parse("#E8A33D"));
        public static readonly IBrush Medium = new SolidColorBrush(Color.Parse("#F97316"));
        public static readonly IBrush Error = new SolidColorBrush(Color.Parse("#E5484D"));

        public static readonly IBrush OkSoft = new SolidColorBrush(Color.Parse("#262F6BFF"));
        public static readonly IBrush WarnSoft = new SolidColorBrush(Color.Parse("#26E8A33D"));
        public static readonly IBrush ErrorSoft = new SolidColorBrush(Color.Parse("#26E5484D"));

        public static readonly IBrush MediumSoft = new SolidColorBrush(Color.Parse("#26F97316"));

        /// <summary>写入相位（测试进度条第一遍）。</summary>
        public static readonly IBrush Write = new SolidColorBrush(Color.Parse("#7C5CFF"));

        public static readonly IBrush WriteSoft = new SolidColorBrush(Color.Parse("#3D7C5CFF"));

        /// <summary>读取相位（测试进度条第二遍）。</summary>
        public static readonly IBrush Read = new SolidColorBrush(Color.Parse("#16A34A"));

        public static readonly IBrush ReadSoft = new SolidColorBrush(Color.Parse("#3D16A34A"));

        /// <summary>未初始化 / 未知状态使用的中性色。</summary>
        public static readonly IBrush Muted = new SolidColorBrush(Color.Parse("#98A2B3"));

        public static readonly IBrush MutedSoft = new SolidColorBrush(Color.Parse("#1498A2B3"));
    }
}
