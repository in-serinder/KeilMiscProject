using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace PWMCtrol.Controls;

/// <summary>
/// 七段数码管（自绘）。与下位机面板同一套字段规则，用来在上位机里镜像显示。
/// <para><see cref="Text"/> 每位一个字符，空格表示该位不亮；<see cref="Dots"/> 等长，'1' 表示该位 dp 点亮。</para>
/// </summary>
public class SegmentDisplay : Control
{
    public static readonly StyledProperty<string> TextProperty =
        AvaloniaProperty.Register<SegmentDisplay, string>(nameof(Text), "        ");

    public static readonly StyledProperty<string> DotsProperty =
        AvaloniaProperty.Register<SegmentDisplay, string>(nameof(Dots), string.Empty);

    public static readonly StyledProperty<double> IntensityProperty =
        AvaloniaProperty.Register<SegmentDisplay, double>(nameof(Intensity), 1.0);

    private static readonly Dictionary<char, string> Segments = new()
    {
        ['0'] = "abcdef",
        ['1'] = "bc",
        ['2'] = "abged",
        ['3'] = "abgcd",
        ['4'] = "fgbc",
        ['5'] = "afgcd",
        ['6'] = "afgedc",
        ['7'] = "abc",
        ['8'] = "abcdefg",
        ['9'] = "abcdfg",
        ['-'] = "g",
        [' '] = string.Empty,
    };

    private static readonly Color OnColor = Color.Parse("#FF5533");
    private static readonly Color OffColor = Color.Parse("#0D1014");

    static SegmentDisplay()
    {
        AffectsRender<SegmentDisplay>(TextProperty, DotsProperty, IntensityProperty);
    }

    public string Text
    {
        get => GetValue(TextProperty);
        set => SetValue(TextProperty, value);
    }

    public string Dots
    {
        get => GetValue(DotsProperty);
        set => SetValue(DotsProperty, value);
    }

    public double Intensity
    {
        get => GetValue(IntensityProperty);
        set => SetValue(IntensityProperty, value);
    }

    /// <summary>没有内容尺寸，必须自己给出期望大小，否则在居中/自动布局里会被压成 0 宽。</summary>
    protected override Size MeasureOverride(Size availableSize)
    {
        var count = Math.Max(1, (Text ?? string.Empty).Length);
        var width = double.IsInfinity(availableSize.Width) ? count * 22.0 : availableSize.Width;
        var height = double.IsInfinity(availableSize.Height) ? 46.0 : availableSize.Height;
        return new Size(width, height);
    }

    public override void Render(DrawingContext context)
    {
        var text = Text ?? string.Empty;
        if (text.Length == 0 || Bounds.Width <= 4 || Bounds.Height <= 4)
        {
            return;
        }

        var dots = Dots ?? string.Empty;
        var intensity = Math.Clamp(Intensity, 0.25, 1.0);
        var onBrush = new SolidColorBrush(Scale(OnColor, intensity));
        var offBrush = new SolidColorBrush(OffColor);

        var count = text.Length;
        var dw = Bounds.Width / count;
        var dh = Bounds.Height;

        var thickness = dh * 0.10;
        var segLen = Math.Max(dw * 0.48, thickness * 2.2);
        var height = dh * 0.84;
        var vLen = (height - 3 * thickness) / 2;

        for (var i = 0; i < count; i++)
        {
            var dx = i * dw;
            var x0 = dx + (dw - (segLen + thickness)) / 2;
            var y0 = (dh - height) / 2;

            var lit = Segments.TryGetValue(text[i], out var on) ? on : string.Empty;

            Draw(context, 'a', x0, y0, segLen, thickness, lit.Contains('a'), onBrush, offBrush);
            Draw(context, 'f', x0, y0 + thickness, thickness, vLen, lit.Contains('f'), onBrush, offBrush);
            Draw(context, 'b', x0 + segLen - thickness, y0 + thickness, thickness, vLen, lit.Contains('b'), onBrush, offBrush);
            Draw(context, 'g', x0, y0 + thickness + vLen, segLen, thickness, lit.Contains('g'), onBrush, offBrush);
            Draw(context, 'e', x0, y0 + 2 * thickness + vLen, thickness, vLen, lit.Contains('e'), onBrush, offBrush);
            Draw(context, 'c', x0 + segLen - thickness, y0 + 2 * thickness + vLen, thickness, vLen, lit.Contains('c'), onBrush, offBrush);
            Draw(context, 'd', x0, y0 + 2 * thickness + 2 * vLen, segLen, thickness, lit.Contains('d'), onBrush, offBrush);

            var dpOn = i < dots.Length && dots[i] == '1';
            var dpSize = thickness * 0.95;
            var dpRect = new Rect(
                x0 + segLen + thickness * 0.30,
                y0 + height - dpSize,
                dpSize,
                dpSize);
            context.DrawRectangle(dpOn ? onBrush : offBrush, null, dpRect, dpSize / 2, dpSize / 2);
        }
    }

    private static void Draw(
        DrawingContext context,
        char segment,
        double x,
        double y,
        double w,
        double h,
        bool on,
        IBrush onBrush,
        IBrush offBrush)
    {
        if (w <= 0 || h <= 0)
        {
            return;
        }

        var radius = Math.Min(w, h) * 0.35;
        context.DrawRectangle(on ? onBrush : offBrush, null, new Rect(x, y, w, h), radius, radius);
    }

    private static Color Scale(Color color, double factor)
    {
        return Color.FromRgb(
            (byte)(color.R * factor),
            (byte)(color.G * factor),
            (byte)(color.B * factor));
    }
}
