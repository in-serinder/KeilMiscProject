using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using PDL_upper.Models;

namespace PDL_upper.Controls
{
    /// <summary>回放用的静态序列（已按当前视窗抽稀）。FullScale>0 按满量程归一化，否则自动量程。</summary>
    public sealed class StaticSeries
    {
        public required string Label { get; init; }
        public required IBrush Color { get; init; }
        public required double[] Y { get; init; }      // 中线（或单值）
        public double[]? Min { get; init; }            // 可选包络（与 Y 等长）
        public double[]? Max { get; init; }
        public double T0 { get; init; }                // 首点时间 ms（视窗内）
        public double T1 { get; init; }                // 末点时间 ms
        public double FullScale { get; init; } = -1;   // <0 = 自动量程
        public string Unit { get; init; } = "V";       // "V"/"A"/"℃"——轴刻度与游标自适应单位
    }

    /// <summary>
    /// 回放概览图：X 轴是时间视窗 [ViewStartMs, ViewEndMs]，点按各自 T0/T1 定位。
    /// 滚轮 = 以鼠标为中心缩放，拖拽 = 平移；交互结果写回这两个属性（TwoWay 绑定到 VM）。
    /// </summary>
    public class StaticChart : Control
    {
        public static readonly StyledProperty<IReadOnlyList<StaticSeries>?> SeriesProperty =
            AvaloniaProperty.Register<StaticChart, IReadOnlyList<StaticSeries>?>(nameof(Series));

        public static readonly StyledProperty<double> ViewStartMsProperty =
            AvaloniaProperty.Register<StaticChart, double>(
                nameof(ViewStartMs), 0.0, false,
                BindingMode.TwoWay, null);

        public static readonly StyledProperty<double> ViewEndMsProperty =
            AvaloniaProperty.Register<StaticChart, double>(
                nameof(ViewEndMs), 0.0, false,
                BindingMode.TwoWay, null);

        public static readonly StyledProperty<double> TotalMsProperty =
            AvaloniaProperty.Register<StaticChart, double>(nameof(TotalMs));

        // 手动 Y 轴（示波器式 V/div）：ManualY=true 时，主单位序列按 [YCenter ± 2×VPerDiv] 定位
        public static readonly StyledProperty<bool> ManualYProperty =
            AvaloniaProperty.Register<StaticChart, bool>(nameof(ManualY));

        public static readonly StyledProperty<double> YCenterProperty =
            AvaloniaProperty.Register<StaticChart, double>(nameof(YCenter), 0.0, false,
                BindingMode.TwoWay);

        public static readonly StyledProperty<double> VPerDivProperty =
            AvaloniaProperty.Register<StaticChart, double>(nameof(VPerDiv), 0.1, false,
                BindingMode.TwoWay);

        public bool ManualY { get => GetValue(ManualYProperty); set => SetValue(ManualYProperty, value); }
        public double YCenter { get => GetValue(YCenterProperty); set => SetValue(YCenterProperty, value); }
        public double VPerDiv { get => GetValue(VPerDivProperty); set => SetValue(VPerDivProperty, value); }
        public const int DivCount = 4;

        /// <summary>逐序列独立量程（混合单位同图时用：V/A/W 各自适配满格）。</summary>
        public static readonly StyledProperty<bool> PerSeriesScaleProperty =
            AvaloniaProperty.Register<StaticChart, bool>(nameof(PerSeriesScale));
        public bool PerSeriesScale
        {
            get => GetValue(PerSeriesScaleProperty);
            set => SetValue(PerSeriesScaleProperty, value);
        }

        public IReadOnlyList<StaticSeries>? Series
        {
            get => GetValue(SeriesProperty);
            set => SetValue(SeriesProperty, value);
        }
        public double ViewStartMs
        {
            get => GetValue(ViewStartMsProperty);
            set => SetValue(ViewStartMsProperty, value);
        }
        public double ViewEndMs
        {
            get => GetValue(ViewEndMsProperty);
            set => SetValue(ViewEndMsProperty, value);
        }
        public double TotalMs
        {
            get => GetValue(TotalMsProperty);
            set => SetValue(TotalMsProperty, value);
        }

        private readonly Pen _gridPen;
        private readonly Pen _axisPen;
        private readonly IBrush _textBrush;
        private readonly Typeface _mono = new Typeface("Consolas");

        // 拖拽状态 + 十字游标
        private bool _dragging;
        private bool _yDragging;                 // 右键拖动 = 平移 Y 中心
        private double _yOrigin, _yCenterStart;
        private Point _dragOrigin;
        private double _dragStart, _dragEnd;
        private Point? _cursor;
        private readonly Pen _crossPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#8A93A6")), 0.9, DashStyle.Dash);
        private readonly IBrush _tipBg = new ImmutableSolidColorBrush(Color.Parse("#CC1B1E24"));
        private readonly IBrush _valueBrush = new ImmutableSolidColorBrush(Color.Parse("#E6E8EC"));
        private readonly Pen _tipPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#3A4048")), 1);

        static StaticChart()
        {
            AffectsRender<StaticChart>(SeriesProperty, ViewStartMsProperty, ViewEndMsProperty,
                ManualYProperty, YCenterProperty, VPerDivProperty, PerSeriesScaleProperty);
        }

        public StaticChart()
        {
            _gridPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#2E333D")), 0.5, DashStyle.Dash);
            _axisPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#3A4048")), 0.7);
            _textBrush = new ImmutableSolidColorBrush(Color.Parse("#8B93A1"));
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            // PointerWheelChanged 需要显式处理，否则会被父级滚轮吞掉
            PointerWheelChanged += OnWheel;
        }

        protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnDetachedFromVisualTree(e);
            PointerWheelChanged -= OnWheel;
        }

        // ------------------------------------------------------------ 交互
        private double X0 => 50;
        private double X1 => Bounds.Width - 10;

        private double FracAt(double x)
        {
            double span = Math.Max(1, X1 - X0);
            return Math.Clamp((x - X0) / span, 0, 1);
        }

        private void OnWheel(object? sender, PointerWheelEventArgs e)
        {
            double total = TotalMs;
            // Ctrl + 滚轮 = 调 V/div
            if (e.KeyModifiers.HasFlag(KeyModifiers.Control) && VPerDiv > 0)
            {
                VPerDiv = Math.Clamp(VPerDiv * (e.Delta.Y > 0 ? 0.8 : 1.25), 0.001, 100);
                e.Handled = true;
                return;
            }
            if (total <= 0) return;
            double f = FracAt(e.GetPosition(this).X);
            double k = e.Delta.Y > 0 ? 0.8 : 1.25;          // 上滚放大
            double s0 = ViewStartMs, s1 = ViewEndMs;
            double w = Math.Max(50, (s1 - s0) * k);
            // 鼠标位置保持不动
            double center = s0 + (s1 - s0) * f;
            double ns = center - w * f;
            double ne = ns + w;
            if (ns < 0) { ns = 0; ne = Math.Min(total, w); }
            if (ne > total) { ne = total; ns = Math.Max(0, ne - w); }
            ViewStartMs = ns;
            ViewEndMs = ne;
            e.Handled = true;
        }

        protected override void OnPointerPressed(PointerPressedEventArgs e)
        {
            base.OnPointerPressed(e);
            var pt = e.GetPosition(this);
            var props = e.GetCurrentPoint(this).Properties;
            if (props.IsRightButtonPressed && ManualY)
            {
                // 右键上下拖 = 平移 Y 中心（手动 V/div 模式）
                _yDragging = true;
                _yOrigin = pt.Y;
                _yCenterStart = YCenter;
                e.Pointer.Capture(this);
            }
            else if (props.IsLeftButtonPressed)
            {
                _dragging = true;
                _dragOrigin = pt;
                _dragStart = ViewStartMs;
                _dragEnd = ViewEndMs;
                e.Pointer.Capture(this);
            }
        }

        protected override void OnPointerMoved(PointerEventArgs e)
        {
            base.OnPointerMoved(e);
            _cursor = e.GetPosition(this);
            InvalidateVisual();                     // 游标要即时跟手（本控件没有定时器）
            if (_yDragging)
            {
                double ph = Math.Max(1, Bounds.Height - 32);
                double scale = (DivCount * VPerDiv) / ph;      // 每像素对应电压
                YCenter = _yCenterStart + (e.GetPosition(this).Y - _yOrigin) * scale;
                return;
            }
            if (!_dragging) return;
            double total = TotalMs;
            if (total <= 0) return;
            double dx = e.GetPosition(this).X - _dragOrigin.X;
            double span = Math.Max(1, X1 - X0);
            double shift = -dx / span * (_dragEnd - _dragStart);
            double w = _dragEnd - _dragStart;
            double ns = Math.Clamp(_dragStart + shift, 0, total - w);
            ViewStartMs = ns;
            ViewEndMs = ns + w;
        }

        protected override void OnPointerReleased(PointerReleasedEventArgs e)
        {
            base.OnPointerReleased(e);
            _dragging = false;
            _yDragging = false;
            e.Pointer.Capture(null);
        }

        protected override void OnPointerExited(PointerEventArgs e)
        {
            base.OnPointerExited(e);
            _cursor = null;
            InvalidateVisual();
        }

        // ------------------------------------------------------------ 渲染
        public override void Render(DrawingContext ctx)
        {
            var series = Series;
            double w = Bounds.Width, h = Bounds.Height;
            if (series == null || w < 60 || h < 40) return;

            double x0 = X0, x1 = X1, y0 = 12, y1 = h - 20;
            double v0 = ViewStartMs, v1 = ViewEndMs;
            double vSpan = Math.Max(1, v1 - v0);

            // 量程策略：PerSeriesScale=每序列独立适配（混合单位同图）；否则全局自动
            bool manual = ManualY && VPerDiv > 0;
            string pu = series.Count > 0 ? series[0].Unit : "V";
            double lo = double.MaxValue, hi = double.MinValue;
            bool any = false;
            if (!PerSeriesScale)
            {
                foreach (var s in series)
                {
                    if (manual && s.Unit == pu) continue;      // 手动模式下主单位走 V/div
                    if (s.Y.Length == 0) continue;
                    any = true;
                    foreach (var v in s.Y)
                    {
                        if (v < lo) lo = v;
                        if (v > hi) hi = v;
                    }
                }
            }
            bool anyData = series.Any(s2 => s2.Y.Length > 0);
            if (!anyData)
            {
                var hint = new FormattedText("视窗内无数据",
                    CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 11, _textBrush);
                ctx.DrawText(hint, new Point((w - hint.Width) / 2, (h - hint.Height) / 2));
                return;
            }
            if (any)
            {
                if (hi - lo < 1e-9) { double m = (hi + lo) / 2; lo = m - 0.5; hi = m + 0.5; }
                double pad = (hi - lo) * 0.08;
                lo -= pad; hi += pad;
            }

            double mLo = YCenter - (DivCount / 2.0) * VPerDiv;
            double mHi = YCenter + (DivCount / 2.0) * VPerDiv;
            double YOf(double v, StaticSeries s)
            {
                if (PerSeriesScale)
                {
                    if (manual && s.Unit == pu)
                        return y1 - (y1 - y0) * Math.Clamp((v - mLo) / Math.Max(1e-9, mHi - mLo), 0.0, 1.0);
                    double sLo = double.MaxValue, sHi = double.MinValue;
                    if (s.Min is { } smn && s.Max is { } smx)
                    {
                        for (int i = 0; i < s.Y.Length; i++)
                        { if (smn[i] < sLo) sLo = smn[i]; if (smx[i] > sHi) sHi = smx[i]; }
                    }
                    else
                    {
                        foreach (var val in s.Y) { if (val < sLo) sLo = val; if (val > sHi) sHi = val; }
                    }
                    if (sHi - sLo < 1e-9) { double m = (sHi + sLo) / 2; sLo = m - 0.5; sHi = m + 0.5; }
                    double p2 = (sHi - sLo) * 0.08;
                    sLo -= p2; sHi += p2;
                    return y1 - (y1 - y0) * Math.Clamp((v - sLo) / (sHi - sLo), 0.0, 1.0);
                }
                return y1 - (y1 - y0) * Math.Clamp((v - lo) / Math.Max(1e-9, hi - lo), 0.0, 1.0);
            }
            double XOf(double t) => x0 + (x1 - x0) * Math.Clamp((t - v0) / vSpan, 0, 1);

            // 轴刻度：手动主单位按 V/div 分度；PerSeriesScale 用主序列自身量程；否则全局自动
            var primary = series.FirstOrDefault(s2 => s2.Y.Length > 0);
            for (int k = 0; k <= 4; k++)
            {
                double yy = y1 - (y1 - y0) * k / 4;
                string unit = primary?.Unit ?? "V";
                double val;
                if (PerSeriesScale && primary != null)
                {
                    if (manual && pu == unit)
                        val = mHi - (mHi - mLo) * k / 4;
                    else
                    {
                        double sLo = double.MaxValue, sHi = double.MinValue;
                        if (primary.Min is { } pmn && primary.Max is { } pmx)
                        {
                            for (int i = 0; i < primary.Y.Length; i++)
                            { if (pmn[i] < sLo) sLo = pmn[i]; if (pmx[i] > sHi) sHi = pmx[i]; }
                        }
                        else
                        {
                            foreach (var v2 in primary.Y) { if (v2 < sLo) sLo = v2; if (v2 > sHi) sHi = v2; }
                        }
                        if (sHi - sLo < 1e-9) { double m = (sHi + sLo) / 2; sLo = m - 0.5; sHi = m + 0.5; }
                        double p3 = (sHi - sLo) * 0.08;
                        val = (sHi + p3) - ((sHi + p3) - (sLo - p3)) * k / 4;
                    }
                }
                else
                    val = manual && pu == unit
                        ? mHi - (mHi - mLo) * k / 4
                        : hi - (hi - lo) * k / 4;
                if (k > 0 && k < 4)
                    ctx.DrawLine(_gridPen, new Point(x0, yy), new Point(x1, yy));
                var ft = new FormattedText(UnitFmt.Axis(val, unit),
                    CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 10, _textBrush);
                ctx.DrawText(ft, new Point(x0 - 6 - ft.Width, yy - ft.Height / 2));
            }
            ctx.DrawLine(_axisPen, new Point(x0, y1), new Point(x1, y1));
            ctx.DrawLine(_axisPen, new Point(x0, y0), new Point(x0, y1));

            foreach (var s in series)
            {
                var d = s.Y;
                if (d.Length < 2) continue;
                double dt = (s.T1 - s.T0) / (d.Length - 1);
                var geo = new StreamGeometry();
                using (var g = geo.Open())
                {
                    bool first = true;
                    for (int i = 0; i < d.Length; i++)
                    {
                        double x = XOf(s.T0 + i * dt);
                        double y = YOf(d[i], s);
                        if (first) { g.BeginFigure(new Point(x, y), false); first = false; }
                        else g.LineTo(new Point(x, y));
                    }
                    g.EndFigure(false);
                }
                ctx.DrawGeometry(null, new Pen(s.Color, 1.4), geo);
            }

            // 图例
            double lx = x0 + 6;
            foreach (var s in series)
            {
                var ft = new FormattedText(s.Label,
                    CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 11, s.Color);
                ctx.DrawText(ft, new Point(lx, y0 - 2));
                lx += ft.Width + 18;
            }

            // ---- 十字游标 + 数据标注 ----
            if (_cursor is { } c && c.X >= x0 && c.X <= x1 && c.Y >= y0 && c.Y <= y1)
            {
                double tCur = v0 + (c.X - x0) / (x1 - x0) * vSpan;
                ctx.DrawLine(_crossPen, new Point(c.X, y0), new Point(c.X, y1));
                ctx.DrawLine(_crossPen, new Point(x0, c.Y), new Point(x1, c.Y));

                // 每条曲线在光标时刻的最近采样点
                var tips = new List<(IBrush col, string txt, double py)>();
                foreach (var s in series)
                {
                    var d = s.Y;
                    if (d.Length < 2) continue;
                    double dt = (s.T1 - s.T0) / (d.Length - 1);
                    int idx = (int)Math.Clamp(Math.Round((tCur - s.T0) / dt), 0, d.Length - 1);
                    double val = d[idx];
                    double px = XOf(s.T0 + idx * dt);
                    double py = YOf(val, s);
                    var brush = new ImmutableSolidColorBrush(Color.Parse("#FFFFFF"));
                    ctx.DrawEllipse(s.Color, null, new Point(px, py), 4.5, 4.5);
                    tips.Add((s.Color,
                        s.Label + "  " + UnitFmt.Value(val, s.Unit), py));
                }

                // 光标时刻的时间标注（压在竖线上）
                var tft = new FormattedText(string.Format(CultureInfo.InvariantCulture, "{0:F3} s", tCur / 1000.0),
                    CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 10, _valueBrush);
                double tx = Math.Clamp(c.X - tft.Width / 2, x0, x1 - tft.Width);
                ctx.FillRectangle(_tipBg, new Rect(tx - 3, y0 + 2, tft.Width + 6, tft.Height + 4));
                ctx.DrawText(tft, new Point(tx, y0 + 4));

                // 数值气泡（贴光标右侧，放不下换左侧；按 y 排序与圆点对齐）
                if (tips.Count > 0)
                {
                    tips.Sort((a, b) => a.py.CompareTo(b.py));
                    double mw = 0;
                    foreach (var (_, txt, _) in tips)
                    {
                        var ft = new FormattedText(txt, CultureInfo.InvariantCulture,
                            FlowDirection.LeftToRight, _mono, 11, _textBrush);
                        mw = Math.Max(mw, ft.Width);
                    }
                    double bw = mw + 18, bh = tips.Count * 17 + 10;
                    double bx = c.X + 16;
                    if (bx + bw > x1) bx = c.X - 16 - bw;
                    double by = Math.Clamp(c.Y - bh / 2, y0, y1 - bh);
                    var rect = new Rect(bx, by, bw, bh);
                    ctx.FillRectangle(_tipBg, rect, 4);
                    ctx.DrawRectangle(null, _tipPen, rect, 4, 4);
                    for (int i = 0; i < tips.Count; i++)
                    {
                        var (col, txt, _) = tips[i];
                        var ft = new FormattedText(txt, CultureInfo.InvariantCulture,
                            FlowDirection.LeftToRight, _mono, 11, col);
                        ctx.DrawText(ft, new Point(bx + 9, by + 6 + i * 17));
                    }
                }
            }

            // X 轴时间刻度（秒）
            void LabelTime(double t, bool right)
            {
                var ft = new FormattedText($"{t / 1000.0:F2} s",
                    CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 10, _textBrush);
                double px = right ? XOf(t) - ft.Width : XOf(t) - ft.Width / 2;
                px = Math.Clamp(px, x0 - 30, x1 - ft.Width);
                ctx.DrawText(ft, new Point(px, y1 + 4));
            }
            LabelTime(v0, false);
            LabelTime(v1, true);
            var mid = new FormattedText($"视窗 {(v1 - v0) / 1000.0:F2} s / 全程 {TotalMs / 1000.0:F1} s",
                CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 10, _textBrush);
            ctx.DrawText(mid, new Point(x1 - mid.Width, y1 + 4));
        }
    }
}
