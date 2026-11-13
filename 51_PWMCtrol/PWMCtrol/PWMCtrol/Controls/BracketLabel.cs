using System;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace PWMCtrol.Controls;

/// <summary>
/// 括号标签：一条带两端短竖线的下划线，中间断开写标签，用于标注数码管某个字段段。
/// 样式对齐"PWM / kHz"那种仪表丝印。
/// </summary>
public class BracketLabel : Control
{
    public static readonly StyledProperty<string> TextProperty =
        AvaloniaProperty.Register<BracketLabel, string>(nameof(Text), string.Empty);

    public static readonly StyledProperty<string> SubTextProperty =
        AvaloniaProperty.Register<BracketLabel, string>(nameof(SubText), string.Empty);

    public static readonly StyledProperty<IBrush?> StrokeProperty =
        AvaloniaProperty.Register<BracketLabel, IBrush?>(nameof(Stroke),
            new SolidColorBrush(Color.Parse("#4A5563")));

    public static readonly StyledProperty<IBrush?> ForegroundProperty =
        AvaloniaProperty.Register<BracketLabel, IBrush?>(nameof(Foreground),
            new SolidColorBrush(Color.Parse("#9AA7B6")));

    static BracketLabel()
    {
        AffectsRender<BracketLabel>(TextProperty, SubTextProperty, StrokeProperty, ForegroundProperty);
    }

    public string Text
    {
        get => GetValue(TextProperty);
        set => SetValue(TextProperty, value);
    }

    public string SubText
    {
        get => GetValue(SubTextProperty);
        set => SetValue(SubTextProperty, value);
    }

    public IBrush? Stroke
    {
        get => GetValue(StrokeProperty);
        set => SetValue(StrokeProperty, value);
    }

    public IBrush? Foreground
    {
        get => GetValue(ForegroundProperty);
        set => SetValue(ForegroundProperty, value);
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        var width = double.IsInfinity(availableSize.Width) ? 120 : availableSize.Width;
        return new Size(width, 20);
    }

    public override void Render(DrawingContext context)
    {
        var w = Bounds.Width;
        var h = Bounds.Height;
        if (w <= 8 || h <= 8)
        {
            return;
        }

        var brush = Stroke ?? Brushes.Gray;
        var textBrush = Foreground ?? Brushes.LightGray;
        var pen = new Pen(brush, 1);

        var baseline = h - 7;
        var legTop = baseline - 5;

        var main = new FormattedText(Text ?? string.Empty, CultureInfo.CurrentCulture,
            FlowDirection.LeftToRight, new Typeface("Microsoft YaHei UI"), 11, textBrush);

        var sub = string.IsNullOrEmpty(SubText)
            ? null
            : new FormattedText(SubText, CultureInfo.CurrentCulture,
                FlowDirection.LeftToRight, new Typeface("Microsoft YaHei UI"), 8, textBrush);

        var textWidth = main.Width + (sub is null ? 0 : sub.Width + 2);
        var gap = Math.Min(w - 8, textWidth + 14);
        var gapStart = (w - gap) / 2;
        var gapEnd = gapStart + gap;

        // 左右两段横线，中间留出写标签的位置
        if (gapStart > 1)
        {
            context.DrawLine(pen, new Point(0, baseline), new Point(gapStart, baseline));
            context.DrawLine(pen, new Point(0, legTop), new Point(0, baseline));
        }

        if (gapEnd < w - 1)
        {
            context.DrawLine(pen, new Point(gapEnd, baseline), new Point(w, baseline));
            context.DrawLine(pen, new Point(w - 0.5, legTop), new Point(w - 0.5, baseline));
        }

        // 文字压在断口上，垂直居中对齐横线
        var textX = gapStart + (gap - textWidth) / 2;
        var textY = baseline - main.Height / 2 - 1;
        context.DrawText(main, new Point(textX, textY));

        if (sub is not null)
        {
            context.DrawText(sub, new Point(textX + main.Width + 2, baseline - sub.Height / 2 + 1));
        }
    }
}
