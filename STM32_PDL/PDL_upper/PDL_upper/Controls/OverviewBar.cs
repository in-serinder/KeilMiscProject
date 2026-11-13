using System;
using System.Collections.Generic;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Immutable;

namespace PDL_upper.Controls
{
    /// <summary>
    /// 底部全局导航条（FL Studio 式）：画全程包络缩略图 + 当前视窗位置框。
    /// 点击/拖动 = 视窗居中到该处；滚轮 = 以鼠标为中心缩放视窗宽度。
    /// ViewStartMs/ViewEndMs 与上部细节图双向共享（由 VM 桥接）。
    /// </summary>
    public class OverviewBar : Control
    {
        public static readonly StyledProperty<IReadOnlyList<StaticSeries>?> SeriesProperty =
            AvaloniaProperty.Register<OverviewBar, IReadOnlyList<StaticSeries>?>(nameof(Series));

        public static readonly StyledProperty<double> ViewStartMsProperty =
            AvaloniaProperty.Register<OverviewBar, double>(nameof(ViewStartMs), 0.0, false,
                BindingMode.TwoWay);

        public static readonly StyledProperty<double> ViewEndMsProperty =
            AvaloniaProperty.Register<OverviewBar, double>(nameof(ViewEndMs), 0.0, false,
                BindingMode.TwoWay);

        public static readonly StyledProperty<double> TotalMsProperty =
            AvaloniaProperty.Register<OverviewBar, double>(nameof(TotalMs));

        public IReadOnlyList<StaticSeries>? Series
        {
            get => GetValue(SeriesProperty);
            set => SetValue(SeriesProperty, value);
        }
        public double ViewStartMs { get => GetValue(ViewStartMsProperty); set => SetValue(ViewStartMsProperty, value); }
        public double ViewEndMs { get => GetValue(ViewEndMsProperty); set => SetValue(ViewEndMsProperty, value); }
        public double TotalMs { get => GetValue(TotalMsProperty); set => SetValue(TotalMsProperty, value); }

        // 绘图区（与 Render 完全一致）；输入映射必须用它，否则框和点击定位错位（踩过：偏右）
        private double X0 => 6;
        private double X1 => Bounds.Width - 6;

        private bool _dragging;
        private bool _rDragging;                 // 右键拖动视窗框 = 平移（宽度不变）
        private Point _rOrigin;
        private double _rStart0, _rStart1;
        private readonly Pen _axisPen;
        private readonly IBrush _textBrush;
        private readonly IBrush _windowFill;
        private readonly Pen _windowPen;
        private readonly Typeface _mono = new Typeface("Consolas");

        static OverviewBar()
        {
            AffectsRender<OverviewBar>(SeriesProperty, ViewStartMsProperty, ViewEndMsProperty, TotalMsProperty);
        }

        public OverviewBar()
        {
            _axisPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#3A4048")), 0.7);
            _textBrush = new ImmutableSolidColorBrush(Color.Parse("#8B93A1"));
            _windowFill = new ImmutableSolidColorBrush(Color.Parse("#28FFFFFF"));
            _windowPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#E6E8EC")), 1.2);
        }

        protected override void OnPointerPressed(PointerPressedEventArgs e)
        {
            base.OnPointerPressed(e);
            if (TotalMs <= 0) return;
            var pt = e.GetPosition(this);
            var props = e.GetCurrentPoint(this).Properties;
            if (props.IsRightButtonPressed && InWindow(pt))
            {
                // 右键在视窗框内按下 = 平移视窗（宽度不变）
                _rDragging = true;
                _rOrigin = pt;
                _rStart0 = ViewStartMs;
                _rStart1 = ViewEndMs;
                e.Pointer.Capture(this);
            }
            else if (props.IsLeftButtonPressed)
            {
                _dragging = true;
                e.Pointer.Capture(this);
                CenterView(pt.X);
            }
        }

        protected override void OnPointerMoved(PointerEventArgs e)
        {
            base.OnPointerMoved(e);
            if (_rDragging)
            {
                double dx = e.GetPosition(this).X - _rOrigin.X;
                double dT = dx / Math.Max(1, X1 - X0) * TotalMs;
                double w = _rStart1 - _rStart0;
                double s = Math.Clamp(_rStart0 + dT, 0, Math.Max(0, TotalMs - w));
                ViewStartMs = s;
                ViewEndMs = s + w;
                return;
            }
            if (_dragging && TotalMs > 0)
                CenterView(e.GetPosition(this).X);
        }

        protected override void OnPointerReleased(PointerReleasedEventArgs e)
        {
            base.OnPointerReleased(e);
            _dragging = false;
            _rDragging = false;
            e.Pointer.Capture(null);
        }

        protected override void OnPointerWheelChanged(PointerWheelEventArgs e)
        {
            base.OnPointerWheelChanged(e);
            if (TotalMs <= 0) return;
            double w = Math.Max(2000, (ViewEndMs - ViewStartMs) * (e.Delta.Y > 0 ? 0.8 : 1.25));
            double center = (ViewStartMs + ViewEndMs) / 2;
            SetWindow(center - w / 2, w);
        }

        private void CenterView(double px)
        {
            double span = Math.Max(1, X1 - X0);
            double f = Math.Clamp((px - X0) / span, 0, 1);
            double winW = ViewEndMs - ViewStartMs;
            SetWindow(f * TotalMs - winW / 2, winW);
        }

        /// <summary>鼠标点是否落在当前视窗框内（时间维判断）。</summary>
        private bool InWindow(Point pt)
        {
            if (TotalMs <= 0) return false;
            double span = Math.Max(1, X1 - X0);
            double t = Math.Clamp((pt.X - X0) / span, 0, 1) * TotalMs;
            return t >= ViewStartMs && t <= ViewEndMs;
        }

        private void SetWindow(double start, double width)
        {
            double w = Math.Max(2000, width);
            double s = Math.Clamp(start, 0, Math.Max(0, TotalMs - w));
            ViewStartMs = s;
            ViewEndMs = s + w;
        }

        private static string FormatT(double ms)
        {
            if (ms < 1000) return ms.ToString("0", CultureInfo.InvariantCulture) + "ms";
            double s = ms / 1000.0;
            if (s < 60) return s.ToString("F1", CultureInfo.InvariantCulture) + "s";
            if (s < 3600) return (s / 60).ToString("F1", CultureInfo.InvariantCulture) + "分";
            return (s / 3600).ToString("F2", CultureInfo.InvariantCulture) + "h";
        }

        public override void Render(DrawingContext ctx)
        {
            var series = Series;
            double w = Bounds.Width, h = Bounds.Height;
            if (w < 40 || h < 30) return;

            double x0 = X0, x1 = X1, y0 = 16, y1 = h - 18;   // 顶部留给图例/信息，底部留给时间轴
            ctx.DrawLine(_axisPen, new Point(x0, y1), new Point(x1, y1));

            if (series != null && TotalMs > 0)
            {
                foreach (var s in series)
                {
                    var d = s.Y;
                    if (d.Length < 2) continue;
                    double lo = double.MaxValue, hi = double.MinValue;
                    foreach (var v in d) { if (v < lo) lo = v; if (v > hi) hi = v; }
                    if (hi - lo < 1e-9) { double m = (hi + lo) / 2; lo = m - 0.5; hi = m + 0.5; }

                    var geo = new StreamGeometry();
                    using (var g = geo.Open())
                    {
                        bool first = true;
                        for (int i = 0; i < d.Length; i++)
                        {
                            double x = x0 + (x1 - x0) * i / (d.Length - 1);
                            double y = y1 - (y1 - y0) * Math.Clamp((d[i] - lo) / (hi - lo), 0, 1);
                            if (first) { g.BeginFigure(new Point(x, y), false); first = false; }
                            else g.LineTo(new Point(x, y));
                        }
                        g.EndFigure(false);
                    }
                    ctx.DrawGeometry(null, new Pen(s.Color, 1), geo);
                }
            }
            else
            {
                var hint = new FormattedText("无数据", CultureInfo.InvariantCulture,
                    FlowDirection.LeftToRight, _mono, 10, _textBrush);
                ctx.DrawText(hint, new Point((w - hint.Width) / 2, (y0 + y1) / 2));
            }

            // 顶部：图例（左）+ 视窗信息（右）
            double lx = x0;
            if (series != null)
            {
                foreach (var s in series)
                {
                    if (string.IsNullOrEmpty(s.Label)) continue;
                    var ft = new FormattedText(s.Label, CultureInfo.InvariantCulture,
                        FlowDirection.LeftToRight, _mono, 10, s.Color);
                    ctx.DrawText(ft, new Point(lx, 2));
                    lx += ft.Width + 10;
                }
            }
            if (TotalMs > 0)
            {
                var ftR = new FormattedText(
                    string.Format(CultureInfo.InvariantCulture,
                        "视窗 {0} - {1} / 全程 {2}（点击定位 · 右键拖框 · 滚轮缩放）",
                        FormatT(ViewStartMs), FormatT(ViewEndMs), FormatT(TotalMs)),
                    CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 10, _textBrush);
                ctx.DrawText(ftR, new Point(x1 - ftR.Width, 2));
            }

            // 视窗框
            if (TotalMs > 0)
            {
                double fx0 = x0 + (x1 - x0) * Math.Clamp(ViewStartMs / TotalMs, 0, 1);
                double fx1 = x0 + (x1 - x0) * Math.Clamp(ViewEndMs / TotalMs, 0, 1);
                var rect = new Rect(fx0, y0 - 2, Math.Max(6, fx1 - fx0), y1 - y0 + 4);
                ctx.FillRectangle(_windowFill, rect);
                ctx.DrawRectangle(null, _windowPen, rect);

                // ---- 底部时间轴：5 个刻度 + 标注 ----
                for (int k = 0; k <= 4; k++)
                {
                    double f = k / 4.0;
                    double x = x0 + (x1 - x0) * f;
                    double t = TotalMs * f;
                    ctx.DrawLine(_axisPen, new Point(x, y1), new Point(x, y1 + 3));
                    var ft = new FormattedText(FormatT(t), CultureInfo.InvariantCulture,
                        FlowDirection.LeftToRight, _mono, 10, _textBrush);
                    double tx = x - ft.Width / 2;
                    if (k == 0) tx = x0;
                    if (k == 4) tx = x1 - ft.Width;
                    ctx.DrawText(ft, new Point(tx, y1 + 4));
                }
            }
        }
    }
}
