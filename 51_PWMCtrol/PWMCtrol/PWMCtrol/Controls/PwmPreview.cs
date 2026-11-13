using System;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace PWMCtrol.Controls;

/// <summary>
/// 单通道 PWM 波形预览：画两个完整周期（从上升沿起），并标注 Ton / T。
/// 高电平区间带淡色填充，占空比一眼可见。
/// </summary>
public class PwmPreview : Control
{
    public static readonly StyledProperty<double> FrequencyProperty =
        AvaloniaProperty.Register<PwmPreview, double>(nameof(Frequency), 1000);

    public static readonly StyledProperty<double> DutyProperty =
        AvaloniaProperty.Register<PwmPreview, double>(nameof(Duty));

    public static readonly StyledProperty<bool> IsRunningProperty =
        AvaloniaProperty.Register<PwmPreview, bool>(nameof(IsRunning));

    public static readonly StyledProperty<int> PeriodsProperty =
        AvaloniaProperty.Register<PwmPreview, int>(nameof(Periods), 2);

    public static readonly StyledProperty<IBrush?> AccentProperty =
        AvaloniaProperty.Register<PwmPreview, IBrush?>(nameof(Accent),
            new SolidColorBrush(Color.Parse("#3FA9F5")));

    private static readonly IBrush BackgroundBrush = new SolidColorBrush(Color.Parse("#0D1014"));
    private static readonly IBrush GridBrush = new SolidColorBrush(Color.Parse("#182029"));
    private static readonly IBrush BaseBrush = new SolidColorBrush(Color.Parse("#243040"));
    private static readonly IBrush DimBrush = new SolidColorBrush(Color.Parse("#3A4453"));
    private static readonly IBrush TonBrush = new SolidColorBrush(Color.Parse("#E8B339"));

    static PwmPreview()
    {
        AffectsRender<PwmPreview>(FrequencyProperty, DutyProperty, IsRunningProperty,
            PeriodsProperty, AccentProperty);
    }

    public double Frequency
    {
        get => GetValue(FrequencyProperty);
        set => SetValue(FrequencyProperty, value);
    }

    public double Duty
    {
        get => GetValue(DutyProperty);
        set => SetValue(DutyProperty, value);
    }

    public bool IsRunning
    {
        get => GetValue(IsRunningProperty);
        set => SetValue(IsRunningProperty, value);
    }

    public int Periods
    {
        get => GetValue(PeriodsProperty);
        set => SetValue(PeriodsProperty, value);
    }

    public IBrush? Accent
    {
        get => GetValue(AccentProperty);
        set => SetValue(AccentProperty, value);
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        var width = double.IsInfinity(availableSize.Width) ? 240 : availableSize.Width;
        var height = double.IsInfinity(availableSize.Height) ? 110 : availableSize.Height;
        return new Size(width, height);
    }

    public override void Render(DrawingContext context)
    {
        var w = Bounds.Width;
        var h = Bounds.Height;
        if (w <= 12 || h <= 20)
        {
            return;
        }

        context.DrawRectangle(BackgroundBrush, null, new Rect(0, 0, w, h), 6, 6);

        var gridPen = new Pen(GridBrush, 1);
        for (var i = 1; i < 10; i++)
        {
            var gx = w * i / 10.0;
            context.DrawLine(gridPen, new Point(gx, 0), new Point(gx, h));
        }

        for (var i = 1; i < 4; i++)
        {
            var gy = h * i / 4.0;
            context.DrawLine(gridPen, new Point(0, gy), new Point(w, gy));
        }

        var yHigh = h * 0.32;
        var yLow = h * 0.66;
        context.DrawLine(new Pen(BaseBrush, 1), new Point(0, yLow), new Point(w, yLow));

        var accent = Accent ?? Brushes.OrangeRed;
        var accentColor = (accent as ISolidColorBrush)?.Color ?? Colors.OrangeRed;

        if (!IsRunning || Duty <= 0.05)
        {
            context.DrawLine(new Pen(DimBrush, 1.5), new Point(0, yLow), new Point(w, yLow));
            DrawText(context, "无输出", new Point(8, yHigh - 6), 10, DimBrush);
            return;
        }

        var clampDuty = Math.Clamp(Duty, 0, 100);
        var periods = Math.Clamp(Periods, 1, 4);
        var periodPx = w / periods;
        var highPx = periodPx * clampDuty / 100.0;

        var fill = new SolidColorBrush(Color.FromArgb(38, accentColor.R, accentColor.G, accentColor.B));
        var wavePen = new Pen(accent, 2);

        var x = 0.0;
        for (var k = 0; k < periods; k++)
        {
            var xEnd = Math.Min(x + periodPx, w);
            var xFall = Math.Min(x + highPx, w);

            if (xFall > x)
            {
                context.FillRectangle(fill, new Rect(x, yHigh, xFall - x, yLow - yHigh));
            }

            context.DrawLine(wavePen, new Point(x, yLow), new Point(x, yHigh));
            context.DrawLine(wavePen, new Point(x, yHigh), new Point(xFall, yHigh));

            if (xFall < xEnd)
            {
                context.DrawLine(wavePen, new Point(xFall, yHigh), new Point(xFall, yLow));
                context.DrawLine(wavePen, new Point(xFall, yLow), new Point(xEnd, yLow));
            }

            if (k < periods - 1)
            {
                context.DrawLine(wavePen, new Point(xEnd, yLow), new Point(xEnd, yHigh));
            }

            x += periodPx;
        }

        // ---- Ton 标注（高电平段） ----
        var tonY = yHigh - 13;
        DrawMeasure(context, 0, highPx, tonY, TonBrush);
        var tonText = "Ton " + FormatTime(TonSeconds());
        var tonSize = Measure(tonText, 9);
        DrawText(context, tonText,
            new Point(Math.Clamp(highPx / 2 - tonSize.Width / 2, 2, Math.Max(2, w - tonSize.Width - 2)),
                tonY - tonSize.Height - 1),
            9, TonBrush);

        // ---- T 标注（整个周期） ----
        var tY = yLow + 13;
        DrawMeasure(context, 0, periodPx, tY, accent);
        var tText = "T " + FormatTime(PeriodSeconds());
        var tSize = Measure(tText, 9);
        DrawText(context, tText,
            new Point(Math.Clamp(periodPx / 2 - tSize.Width / 2, 2, Math.Max(2, w - tSize.Width - 2)),
                tY + 3),
            9, accent);

        // ---- 右上角占空比读数 ----
        var dutyText = clampDuty.ToString("0.0", CultureInfo.InvariantCulture) + "%";
        var dutySize = Measure(dutyText, 10);
        DrawText(context, dutyText, new Point(w - dutySize.Width - 6, 5), 10, accent);
    }

    private static void DrawMeasure(DrawingContext context, double x0, double x1, double y, IBrush brush)
    {
        if (x1 - x0 < 2)
        {
            return;
        }

        var pen = new Pen(brush, 1);
        context.DrawLine(pen, new Point(x0, y), new Point(x1, y));
        context.DrawLine(pen, new Point(x0, y - 3), new Point(x0, y + 3));
        context.DrawLine(pen, new Point(x1, y - 3), new Point(x1, y + 3));
    }

    private static FormattedText Measure(string text, double size) => new(
        text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
        new Typeface("Consolas"), size, Brushes.Gray);

    private static void DrawText(DrawingContext context, string text, Point origin, double size, IBrush brush)
    {
        var formatted = new FormattedText(text, CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight, new Typeface("Consolas"), size, brush);
        context.DrawText(formatted, origin);
    }

    private double PeriodSeconds() => Frequency > 0 ? 1.0 / Frequency : 0;

    private double TonSeconds() => PeriodSeconds() * Math.Clamp(Duty, 0, 100) / 100.0;

    private static string FormatTime(double seconds)
    {
        var inv = CultureInfo.InvariantCulture;
        if (seconds >= 0.9995)
        {
            return seconds.ToString("0.###", inv) + " s";
        }

        if (seconds >= 0.0009995)
        {
            return (seconds * 1e3).ToString("0.###", inv) + " ms";
        }

        if (seconds >= 0.0000009995)
        {
            return (seconds * 1e6).ToString("0.###", inv) + " μs";
        }

        return (seconds * 1e9).ToString("0.###", inv) + " ns";
    }
}
