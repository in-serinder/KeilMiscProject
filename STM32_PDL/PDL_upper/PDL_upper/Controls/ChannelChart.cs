using System;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.Threading;
using PDL_upper.Models;
using PDL_upper.ViewModels;

namespace PDL_upper.Controls
{
    /// <summary>
    /// 单通道曲线：同图画电压（左轴，实线）与电流（右轴，虚线）。
    /// 两种模式（vm.FullTimeMode 切换）：
    ///   滚动 = 最近一段实时曲线（ChartBuffer）；
    ///   全程 = 会话开始到现在的 min/max 包络（FullHistory，无限时长自动降分辨率）。
    /// </summary>
    public class ChannelChart : Control
    {
        public static readonly StyledProperty<ChannelChartViewModel?> ChartProperty =
            AvaloniaProperty.Register<ChannelChart, ChannelChartViewModel?>(nameof(Chart));

        public ChannelChartViewModel? Chart
        {
            get => GetValue(ChartProperty);
            set => SetValue(ChartProperty, value);
        }

        private static readonly string[] SeriesColors = { "#4A9EFF", "#43D17A", "#FFB020", "#B07CFF" };
        private readonly Pen[] _pens = new Pen[4];
        private readonly Pen[] _iPens = new Pen[4];
        private readonly Pen _gridPen;
        private readonly Pen _axisPen;
        private readonly IBrush _textBrush;
        private readonly IBrush _valueBrush;
        private readonly IBrush _tipBg = new ImmutableSolidColorBrush(Color.Parse("#CC1B1E24"));
        private readonly Pen _tipPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#3A4048")), 1);
        private readonly Pen _crossPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#8A93A6")), 0.9, DashStyle.Dash);
        private readonly Typeface _mono = new Typeface("Consolas");
        private DispatcherTimer? _timer;
        private Point? _cursor;

        static ChannelChart()
        {
            AffectsRender<ChannelChart>(ChartProperty);
        }

        public ChannelChart()
        {
            for (int i = 0; i < 4; i++)
            {
                _pens[i] = new Pen(new ImmutableSolidColorBrush(Color.Parse(SeriesColors[i])), 1.5);
                _iPens[i] = new Pen(new ImmutableSolidColorBrush(Color.Parse(SeriesColors[i])), 1.2, DashStyle.Dash);
            }
            _gridPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#2E333D")), 0.5, DashStyle.Dash);
            _axisPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#3A4048")), 0.7);
            _textBrush = new ImmutableSolidColorBrush(Color.Parse("#8B93A1"));
            _valueBrush = new ImmutableSolidColorBrush(Color.Parse("#E6E8EC"));
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

        protected override void OnPointerMoved(PointerEventArgs e)
        {
            base.OnPointerMoved(e);
            _cursor = e.GetPosition(this);
            InvalidateVisual();
        }

        protected override void OnPointerExited(PointerEventArgs e)
        {
            base.OnPointerExited(e);
            _cursor = null;
            InvalidateVisual();
        }

        public override void Render(DrawingContext ctx)
        {
            var vm = Chart;
            double w = Bounds.Width, h = Bounds.Height;
            if (vm == null || w < 90 || h < 50) return;

            double x0 = 46, x1 = w - 46;
            double y0 = 10, y1 = h - 20;
            double fsV = vm.FullScaleV > 0 ? vm.FullScaleV : 1.0;
            double fsA = vm.FullScaleA > 0 ? vm.FullScaleA : 1.0;

            // 网格 + 左轴（V）+ 右轴（A）
            for (int k = 0; k <= 4; k++)
            {
                double yy = y1 - (y1 - y0) * k / 4;
                if (k > 0 && k < 4)
                    ctx.DrawLine(_gridPen, new Point(x0, yy), new Point(x1, yy));
                var ftV = new FormattedText(UnitFmt.Axis(fsV * k / 4, "V"),
                    CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 10, _textBrush);
                ctx.DrawText(ftV, new Point(x0 - 6 - ftV.Width, yy - ftV.Height / 2));
                var ftA = new FormattedText(UnitFmt.Axis(fsA * k / 4, "A"),
                    CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 10, _textBrush);
                ctx.DrawText(ftA, new Point(x1 + 6, yy - ftA.Height / 2));
            }
            ctx.DrawLine(_axisPen, new Point(x0, y1), new Point(x1, y1));
            ctx.DrawLine(_axisPen, new Point(x0, y0), new Point(x0, y1));
            ctx.DrawLine(_axisPen, new Point(x1, y0), new Point(x1, y1));

            int penIdx = Math.Min(vm.Index, 3);
            double totalS;

            if (vm.FullTimeMode)
            {
                // ================= 全程：min/max 包络 =================
                var (mins, maxs) = vm.FullV.Snapshot();
                var (imins, imaxs) = vm.FullI.Snapshot();
                int n = mins.Length;
                totalS = Math.Max(0, (n * vm.FullV.Bucket) * vm.ChartTickMs / 1000.0);

                if (n < 2)
                {
                    var hint = new FormattedText("全程暂无数据 —— 先采集一会儿",
                        CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 11, _textBrush);
                    ctx.DrawText(hint, new Point((w - hint.Width) / 2, (h - hint.Height) / 2));
                }
                else
                {
                    double XOf(int i) => x0 + (x1 - x0) * i / (n - 1);
                    double YV(double v) => y1 - (y1 - y0) * Math.Clamp(v / fsV, 0.0, 1.0);
                    double YA(double v) => y1 - (y1 - y0) * Math.Clamp(v / fsA, 0.0, 1.0);

                    // 电压包络带
                    var band = new StreamGeometry();
                    using (var g = band.Open())
                    {
                        g.BeginFigure(new Point(XOf(0), YV(maxs[0])), false);
                        for (int i = 1; i < n; i++) g.LineTo(new Point(XOf(i), YV(maxs[i])));
                        for (int i = n - 1; i >= 0; i--) g.LineTo(new Point(XOf(i), YV(mins[i])));
                        g.EndFigure(true);
                    }
                    var cV = Color.Parse(SeriesColors[penIdx]);
                    ctx.DrawGeometry(new ImmutableSolidColorBrush(
                        new Color(cV.R, cV.G, cV.B, 0x50)), null, band);
                    var geoMid = new StreamGeometry();
                    using (var g = geoMid.Open())
                    {
                        bool first = true;
                        for (int i = 0; i < n; i++)
                        {
                            var p = new Point(XOf(i), YV((mins[i] + maxs[i]) / 2));
                            if (first) { g.BeginFigure(p, false); first = false; }
                            else g.LineTo(p);
                        }
                        g.EndFigure(false);
                    }
                    ctx.DrawGeometry(null, _pens[penIdx], geoMid);

                    // 电流包络（虚线中线）
                    int ni = imins.Length;
                    if (ni >= 2)
                    {
                        double XOfI(int i) => x0 + (x1 - x0) * i / (ni - 1);
                        var bandI = new StreamGeometry();
                        using (var g = bandI.Open())
                        {
                            g.BeginFigure(new Point(XOfI(0), YA(imaxs[0])), false);
                            for (int i = 1; i < ni; i++) g.LineTo(new Point(XOfI(i), YA(imaxs[i])));
                            for (int i = ni - 1; i >= 0; i--) g.LineTo(new Point(XOfI(i), YA(imins[i])));
                            g.EndFigure(true);
                        }
                        var cI = Color.Parse(SeriesColors[penIdx]);
                        ctx.DrawGeometry(new ImmutableSolidColorBrush(
                            new Color(cI.R, cI.G, cI.B, 0x30)), null, bandI);
                        var geoI = new StreamGeometry();
                        using (var g = geoI.Open())
                        {
                            bool first = true;
                            for (int i = 0; i < ni; i++)
                            {
                                var p = new Point(XOfI(i), YA((imins[i] + imaxs[i]) / 2));
                                if (first) { g.BeginFigure(p, false); first = false; }
                                else g.LineTo(p);
                            }
                            g.EndFigure(false);
                        }
                        ctx.DrawGeometry(null, _iPens[penIdx], geoI);
                    }

                    // 底部时间刻度
                    var ft0 = new FormattedText("0 s", CultureInfo.InvariantCulture,
                        FlowDirection.LeftToRight, _mono, 10, _textBrush);
                    ctx.DrawText(ft0, new Point(x0, y1 + 4));
                    string endTxt = $"会话全程 {totalS:F0} s";
                    var ftE = new FormattedText(endTxt, CultureInfo.InvariantCulture,
                        FlowDirection.LeftToRight, _mono, 10, _textBrush);
                    ctx.DrawText(ftE, new Point(x1 - ftE.Width, y1 + 4));
                }

                // 全程模式游标
                if (n >= 2 && _cursor is { } c && c.X >= x0 && c.X <= x1 && c.Y >= y0 && c.Y <= y1)
                {
                    int idx = (int)Math.Clamp(Math.Round((c.X - x0) / (x1 - x0) * (n - 1)), 0, n - 1);
                    double px = x0 + (x1 - x0) * idx / (n - 1);
                    double tS = idx * vm.FullV.Bucket * vm.ChartTickMs / 1000.0;
                    ctx.DrawLine(_crossPen, new Point(px, y0), new Point(px, y1));
                    ctx.DrawLine(_crossPen, new Point(x0, c.Y), new Point(x1, c.Y));

                    string txt = string.Format(CultureInfo.InvariantCulture,
                        "t={0:F1}s  {1:F2}~{2:F2} V", tS, mins[idx], maxs[idx]);
                    if (idx < imins.Length)
                        txt += string.Format(CultureInfo.InvariantCulture,
                            " / {0:F2}~{1:F2} A", imins[idx], imaxs[idx]);
                    var ft = new FormattedText(txt, CultureInfo.InvariantCulture,
                        FlowDirection.LeftToRight, _mono, 11, _valueBrush);
                    double bw = ft.Width + 16, bh = ft.Height + 10;
                    double bx = px + 14;
                    if (bx + bw > x1) bx = px - 14 - bw;
                    double by = Math.Clamp(c.Y - bh / 2, y0, y1 - bh);
                    var rect = new Rect(bx, by, bw, bh);
                    ctx.FillRectangle(_tipBg, rect, 4);
                    ctx.DrawRectangle(null, _tipPen, rect, 4, 4);
                    ctx.DrawText(ft, new Point(bx + 8, by + 5));
                }
            }
            else
            {
                // ================= 滚动：最近一段实时曲线 =================
                var data = vm.Series.Data;
                var dataI = vm.Buffer.SnapshotI(vm.Index);
                totalS = data.Length * vm.ChartTickMs / 1000.0;

                if (data.Length >= 2)
                {
                    DrawSeries(ctx, data, x0, x1, y0, y1, fsV, _pens[penIdx]);
                    if (dataI.Length == data.Length)
                        DrawSeries(ctx, dataI, x0, x1, y0, y1, fsA, _iPens[penIdx]);
                }

                // 底部时间刻度
                var ft0 = new FormattedText($"-{totalS:F0} s", CultureInfo.InvariantCulture,
                    FlowDirection.LeftToRight, _mono, 10, _textBrush);
                ctx.DrawText(ft0, new Point(x0, y1 + 4));
                var ftE = new FormattedText("现在", CultureInfo.InvariantCulture,
                    FlowDirection.LeftToRight, _mono, 10, _textBrush);
                ctx.DrawText(ftE, new Point(x1 - ftE.Width, y1 + 4));

                // 滚动模式游标
                if (data.Length >= 2 && _cursor is { } c && c.X >= x0 && c.X <= x1 && c.Y >= y0 && c.Y <= y1)
                {
                    int idx = (int)Math.Clamp(Math.Round((c.X - x0) / (x1 - x0) * (data.Length - 1)), 0, data.Length - 1);
                    double vVal = data[idx];
                    double px = x0 + (x1 - x0) * idx / (data.Length - 1);
                    double pyV = y1 - (y1 - y0) * Math.Clamp(vVal / fsV, 0.0, 1.0);
                    ctx.DrawLine(_crossPen, new Point(px, y0), new Point(px, y1));
                    ctx.DrawLine(_crossPen, new Point(x0, c.Y), new Point(x1, c.Y));
                    ctx.DrawEllipse(_pens[penIdx].Brush, null, new Point(px, pyV), 4.5, 4.5);
                    if (dataI.Length == data.Length)
                    {
                        double pyI = y1 - (y1 - y0) * Math.Clamp(dataI[idx] / fsA, 0.0, 1.0);
                        ctx.DrawEllipse(_iPens[penIdx].Brush, null, new Point(px, pyI), 4.5, 4.5);
                    }

                    string txt = dataI.Length == data.Length
                        ? string.Format(CultureInfo.InvariantCulture, "-{0:F1}s  {1} / {2}",
                            (data.Length - 1 - idx) * vm.ChartTickMs / 1000.0,
                            UnitFmt.Volts(vVal), UnitFmt.Amps(dataI[idx]))
                        : UnitFmt.Volts(vVal);
                    var ft = new FormattedText(txt, CultureInfo.InvariantCulture,
                        FlowDirection.LeftToRight, _mono, 11, _valueBrush);
                    double bw = ft.Width + 16, bh = ft.Height + 10;
                    double bx = px + 14;
                    if (bx + bw > x1) bx = px - 14 - bw;
                    double by = Math.Clamp(pyV - bh / 2, y0, y1 - bh);
                    var rect = new Rect(bx, by, bw, bh);
                    ctx.FillRectangle(_tipBg, rect, 4);
                    ctx.DrawRectangle(null, _tipPen, rect, 4, 4);
                    ctx.DrawText(ft, new Point(bx + 8, by + 5));
                }
            }

            // 图例 + 当前值
            var lgV = new FormattedText("— V", CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, _mono, 11, _pens[penIdx].Brush);
            var lgI = new FormattedText("-- I", CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, _mono, 11, _iPens[penIdx].Brush);
            ctx.DrawText(lgV, new Point(x0 + 6, y0 - 2));
            ctx.DrawText(lgI, new Point(x0 + 6 + lgV.Width + 10, y0 - 2));

            var cur = new FormattedText(
                UnitFmt.Volts(vm.LastVolt) + "  " + UnitFmt.Amps(vm.LastAmp),
                CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 12, _valueBrush);
            ctx.DrawText(cur, new Point(x1 - cur.Width - 2, y0 - 2));
        }

        private static void DrawSeries(DrawingContext ctx, double[] d,
            double x0, double x1, double y0, double y1, double fs, Pen pen)
        {
            var geo = new StreamGeometry();
            using (var g = geo.Open())
            {
                bool first = true;
                for (int i = 0; i < d.Length; i++)
                {
                    double x = x0 + (x1 - x0) * i / (d.Length - 1);
                    double y = y1 - (y1 - y0) * Math.Clamp(d[i] / fs, 0.0, 1.0);
                    if (first) { g.BeginFigure(new Point(x, y), false); first = false; }
                    else g.LineTo(new Point(x, y));
                }
                g.EndFigure(false);
            }
            ctx.DrawGeometry(null, pen, geo);
        }
    }
}
