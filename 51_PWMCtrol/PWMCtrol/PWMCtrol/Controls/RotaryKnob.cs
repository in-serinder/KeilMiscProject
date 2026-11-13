using System;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;

namespace PWMCtrol.Controls;

/// <summary>
/// EC11 旋钮（自绘）：上下拖动或滚轮旋转，松手判定为单击。
/// 只负责产生"步进 / 单击"两个命令，具体改什么由 ViewModel 决定。
/// </summary>
public class RotaryKnob : Control
{
    public static readonly StyledProperty<ICommand?> StepCommandProperty =
        AvaloniaProperty.Register<RotaryKnob, ICommand?>(nameof(StepCommand));

    public static readonly StyledProperty<ICommand?> ClickCommandProperty =
        AvaloniaProperty.Register<RotaryKnob, ICommand?>(nameof(ClickCommand));

    public static readonly StyledProperty<IBrush?> AccentProperty =
        AvaloniaProperty.Register<RotaryKnob, IBrush?>(nameof(Accent),
            new SolidColorBrush(Color.Parse("#3FA9F5")));

    private const double StepPixels = 9;

    private static readonly IBrush RingBrush = new SolidColorBrush(Color.Parse("#252C36"));
    private static readonly IBrush RingEdgeBrush = new SolidColorBrush(Color.Parse("#39424F"));
    private static readonly IBrush DialBrush = new SolidColorBrush(Color.Parse("#1D232B"));
    private static readonly IBrush TickBrush = new SolidColorBrush(Color.Parse("#46505E"));
    private static readonly IBrush HubBrush = new SolidColorBrush(Color.Parse("#39424F"));

    private double _angle;
    private double _lastY;
    private double _accumulator;
    private bool _dragging;
    private DateTime _pressedAt;

    public ICommand? StepCommand
    {
        get => GetValue(StepCommandProperty);
        set => SetValue(StepCommandProperty, value);
    }

    public ICommand? ClickCommand
    {
        get => GetValue(ClickCommandProperty);
        set => SetValue(ClickCommandProperty, value);
    }

    public IBrush? Accent
    {
        get => GetValue(AccentProperty);
        set => SetValue(AccentProperty, value);
    }

    public override void Render(DrawingContext context)
    {
        var size = Math.Min(Bounds.Width, Bounds.Height);
        if (size <= 8)
        {
            return;
        }

        var center = new Point(Bounds.Width / 2, Bounds.Height / 2);
        var radius = size / 2 - 2;

        context.DrawEllipse(RingBrush, new Pen(RingEdgeBrush, 2), center, radius, radius);

        var tickOuter = radius - 3;
        var tickInner = radius - 9;
        for (var i = 0; i < 24; i++)
        {
            var a = i * Math.PI / 12;
            var p1 = new Point(center.X + tickOuter * Math.Sin(a), center.Y - tickOuter * Math.Cos(a));
            var p2 = new Point(center.X + tickInner * Math.Sin(a), center.Y - tickInner * Math.Cos(a));
            context.DrawLine(new Pen(TickBrush, 1), p1, p2);
        }

        var dialRadius = radius - 12;
        context.DrawEllipse(DialBrush, new Pen(RingEdgeBrush, 1), center, dialRadius, dialRadius);

        var accent = Accent ?? Brushes.OrangeRed;
        var angle = _angle * Math.PI / 180.0;
        var tip = new Point(
            center.X + dialRadius * 0.86 * Math.Sin(angle),
            center.Y - dialRadius * 0.86 * Math.Cos(angle));
        context.DrawLine(new Pen(accent, 4, lineCap: PenLineCap.Round), center, tip);
        context.DrawEllipse(HubBrush, null, center, 6, 6);
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        _lastY = point.Position.Y;
        _accumulator = 0;
        _dragging = false;
        _pressedAt = DateTime.UtcNow;
        e.Pointer.Capture(this);
        e.Handled = true;
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        if (!ReferenceEquals(e.Pointer.Captured, this))
        {
            return;
        }

        var y = e.GetPosition(this).Y;
        var delta = _lastY - y;
        _lastY = y;
        _accumulator += delta;

        if (Math.Abs(_accumulator) > 4)
        {
            _dragging = true;
        }

        while (_accumulator >= StepPixels)
        {
            _accumulator -= StepPixels;
            Step(1);
        }

        while (_accumulator <= -StepPixels)
        {
            _accumulator += StepPixels;
            Step(-1);
        }
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        e.Pointer.Capture(null);

        if (_dragging)
        {
            return;
        }

        if ((DateTime.UtcNow - _pressedAt).TotalMilliseconds < 450)
        {
            var cmd = ClickCommand;
            if (cmd is not null && cmd.CanExecute(null))
            {
                cmd.Execute(null);
            }
        }
    }

    protected override void OnPointerWheelChanged(PointerWheelEventArgs e)
    {
        base.OnPointerWheelChanged(e);
        Step(e.Delta.Y > 0 ? 1 : -1);
        e.Handled = true;
    }

    private void Step(int delta)
    {
        _angle = (_angle + delta * 15) % 360;
        InvalidateVisual();

        var cmd = StepCommand;
        if (cmd is not null && cmd.CanExecute(delta))
        {
            cmd.Execute(delta);
        }
    }
}
