using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.Threading;
using PDL_upper.ViewModels;

namespace PDL_upper.Controls
{
    /// <summary>
    /// 自绘多通道曲线：四路量程不同（14.2/28.1/75.9/102.3 V），
    /// 必须按各自满量程归一化，否则 12 V 的曲线会被 96 V 的压成一条直线。
    /// </summary>
    public class PowerChart : Control
    {
        public static readonly StyledProperty<ChartViewModel?> VmProperty =
            AvaloniaProperty.Register<PowerChart, ChartViewModel?>(nameof(Vm));

        public ChartViewModel? Vm
        {
            get => GetValue(VmProperty);
            set => SetValue(VmProperty, value);
        }

        private static readonly string[] SeriesColors = { "#4A9EFF", "#43D17A", "#FFB020", "#B07CFF" };
        private readonly Pen[] _pens = new Pen[4];
        private readonly Pen _gridPen;
        private readonly Pen _axisPen;
        private DispatcherTimer? _timer;

        static PowerChart()
        {
            AffectsRender<PowerChart>(VmProperty);
        }

        public PowerChart()
        {
            for (int i = 0; i < 4; i++)
                _pens[i] = new Pen(new ImmutableSolidColorBrush(Color.Parse(SeriesColors[i])), 1.4);
            _gridPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#2E333D")), 0.5, DashStyle.Dash);
            _axisPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#3A4048")), 0.7);
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            _timer = new DispatcherTimer(TimeSpan.FromMilliseconds(33),
                DispatcherPriority.Background, (_, _) => InvalidateVisual());
            _timer.Start();
        }

        protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnDetachedFromVisualTree(e);
            _timer?.Stop();
            _timer = null;
        }

        public override void Render(DrawingContext ctx)
        {
            var vm = Vm;
            double w = Bounds.Width, h = Bounds.Height;
            if (vm == null || w < 60 || h < 40) return;

            double x0 = 10, x1 = w - 10, y0 = 10, y1 = h - 16;

            ctx.DrawLine(_axisPen, new Point(x0, y1), new Point(x1, y1));
            ctx.DrawLine(_axisPen, new Point(x0, y0), new Point(x0, y1));
            for (int k = 1; k <= 3; k++)
            {
                double yy = y1 - (y1 - y0) * k / 4;
                ctx.DrawLine(_gridPen, new Point(x0, yy), new Point(x1, yy));
            }

            int window = Math.Max(2, vm.WindowPoints);
            for (int ch = 0; ch < 4 && ch < vm.Visibility.Count; ch++)
            {
                var s = vm.Visibility[ch];
                if (!s.IsVisible) continue;
                var data = s.Data;
                if (data.Length < 2) continue;

                int n = Math.Min(data.Length, window);
                int startIdx = data.Length - n;
                double fs = s.FullScaleV > 0 ? s.FullScaleV : 1.0;

                var geo = new StreamGeometry();
                using (var g = geo.Open())
                {
                    bool first = true;
                    for (int i = startIdx; i < data.Length; i++)
                    {
                        double x = x0 + (x1 - x0) * (i - startIdx) / Math.Max(1, n - 1);
                        double ratio = Math.Clamp(data[i] / fs, 0.0, 1.0);
                        double y = y1 - (y1 - y0) * ratio;
                        if (first) { g.BeginFigure(new Point(x, y), false); first = false; }
                        else g.LineTo(new Point(x, y));
                    }
                    g.EndFigure(false);
                }
                ctx.DrawGeometry(null, _pens[ch], geo);
            }
        }
    }
}
