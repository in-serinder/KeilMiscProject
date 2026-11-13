using System;
using System.Collections.Generic;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.Threading;
using PDL_upper.ViewModels;

namespace PDL_upper.Controls
{
    /// <summary>
    /// 温度曲线（TH0/TH1 两路同图）：内联迷你折线与独立温度窗口共用，
    /// 高度小（&lt;60px）时省略坐标轴刻度只画线。
    /// </summary>
    public class TempChart : Control
    {
        public static readonly StyledProperty<IReadOnlyList<TempCardViewModel>?> SeriesProperty =
            AvaloniaProperty.Register<TempChart, IReadOnlyList<TempCardViewModel>?>(nameof(Series));

        public IReadOnlyList<TempCardViewModel>? Series
        {
            get => GetValue(SeriesProperty);
            set => SetValue(SeriesProperty, value);
        }

        private readonly Pen[] _pens = new Pen[2];
        private readonly Pen _gridPen;
        private readonly Pen _axisPen;
        private readonly IBrush _textBrush;
        private readonly Typeface _mono = new Typeface("Consolas");
        private DispatcherTimer? _timer;
        private Point? _cursor;
        private readonly Pen _crossPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#8A93A6")), 0.9, DashStyle.Dash);
        private readonly IBrush _tipBg = new ImmutableSolidColorBrush(Color.Parse("#CC1B1E24"));
        private readonly Pen _tipPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#3A4048")), 1);
        private readonly IBrush _valueBrush = new ImmutableSolidColorBrush(Color.Parse("#E6E8EC"));

        static TempChart()
        {
            AffectsRender<TempChart>(SeriesProperty);
        }

        public TempChart()
        {
            _pens[0] = new Pen(new ImmutableSolidColorBrush(Color.Parse("#FF8A65")), 1.5);
            _pens[1] = new Pen(new ImmutableSolidColorBrush(Color.Parse("#FFD54F")), 1.5);
            _gridPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#2E333D")), 0.5, DashStyle.Dash);
            _axisPen = new Pen(new ImmutableSolidColorBrush(Color.Parse("#3A4048")), 0.7);
            _textBrush = new ImmutableSolidColorBrush(Color.Parse("#8B93A1"));
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            _timer = new DispatcherTimer(TimeSpan.FromMilliseconds(200),
                DispatcherPriority.Background, (_, _) => InvalidateVisual());
            _timer.Start();
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

        protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnDetachedFromVisualTree(e);
            _timer?.Stop();
            _timer = null;
        }

        public override void Render(DrawingContext ctx)
        {
            var series = Series;
            double w = Bounds.Width, h = Bounds.Height;
            if (series == null || w < 40 || h < 20) return;

            bool mini = h < 60;
            double x0 = mini ? 4 : 46, x1 = w - 6, y0 = 4, y1 = h - (mini ? 4 : 16);

            // 拉两路快照（值 + 时间戳），合并求量程
            var data = new double[2][];
            var dataT = new double[2][];
            double lo = double.MaxValue, hi = double.MinValue;
            int maxLen = 0;
            double tBase = double.MaxValue, tEnd = 0;
            for (int i = 0; i < 2 && i < series.Count; i++)
            {
                data[i] = series[i].Live.Snapshot();
                dataT[i] = series[i].Live.SnapshotT();
                foreach (var v in data[i])
                {
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
                if (data[i].Length > maxLen) maxLen = data[i].Length;
                if (dataT[i].Length > 0)
                {
                    tBase = Math.Min(tBase, dataT[i][0]);
                    tEnd = Math.Max(tEnd, dataT[i][dataT[i].Length - 1] - tBase);
                }
            }

            if (maxLen < 2 || lo > hi)
            {
                var hint = new FormattedText("等待温度数据…",
                    CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 11, _textBrush);
                ctx.DrawText(hint, new Point((w - hint.Width) / 2, (h - hint.Height) / 2));
                return;
            }

            // 平坦时扩 ±0.5 ℃，留 10% 余量
            if (hi - lo < 1.0) { double m = (hi + lo) / 2; lo = m - 0.5; hi = m + 0.5; }
            double pad = (hi - lo) * 0.10;
            lo -= pad; hi += pad;
            double Range(double v) => y1 - (y1 - y0) * Math.Clamp((v - lo) / (hi - lo), 0.0, 1.0);

            if (!mini)
            {
                for (int k = 1; k <= 3; k++)
                {
                    double yy = y1 - (y1 - y0) * k / 4;
                    ctx.DrawLine(_gridPen, new Point(x0, yy), new Point(x1, yy));
                    double val = hi - (hi - lo) * k / 4;
                    var ft = new FormattedText(val.ToString("0.#") + " ℃",
                        CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 10, _textBrush);
                    ctx.DrawText(ft, new Point(x0 - 6 - ft.Width, yy - ft.Height / 2));
                }
                ctx.DrawLine(_axisPen, new Point(x0, y1), new Point(x1, y1));
                ctx.DrawLine(_axisPen, new Point(x0, y0), new Point(x0, y1));
            }
            else
            {
                // 迷你：画基线和上/下界淡线
                ctx.DrawLine(_gridPen, new Point(x0, y1), new Point(x1, y1));
            }

            double tSpan = Math.Max(1, tEnd);          // 相对首点的时间范围 ms
            double XOfTime(double t) => x0 + (x1 - x0) * Math.Clamp(t / tSpan, 0, 1);

            for (int i = 0; i < 2 && i < series.Count; i++)
            {
                var d = data[i];
                var dT = dataT[i];
                if (d.Length < 2 || dT.Length < d.Length) continue;
                var geo = new StreamGeometry();
                using (var g = geo.Open())
                {
                    bool first = true;
                    for (int k = 0; k < d.Length; k++)
                    {
                        double x = XOfTime(dT[k] - tBase);
                        double y = Range(d[k]);
                        if (first) { g.BeginFigure(new Point(x, y), false); first = false; }
                        else g.LineTo(new Point(x, y));
                    }
                    g.EndFigure(false);
                }
                ctx.DrawGeometry(null, _pens[i], geo);
            }

            // 底部起始/末尾时间标注
            var ft0 = new FormattedText("起始 0.0 s", CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, _mono, 10, _textBrush);
            ctx.DrawText(ft0, new Point(x0, y1 + 4));
            var ftE = new FormattedText(string.Format(CultureInfo.InvariantCulture,
                "末尾 {0:F1} s（每点约 1 s）", tSpan / 1000.0), CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, _mono, 10, _textBrush);
            ctx.DrawText(ftE, new Point(x1 - ftE.Width, y1 + 4));

            if (!mini)
            {
                // 图例
                double lx = x0 + 6;
                for (int i = 0; i < 2 && i < series.Count; i++)
                {
                    var c = series[i];
                    var ft = new FormattedText($"{c.Label} {c.Celsius:F1} ℃",
                        CultureInfo.InvariantCulture, FlowDirection.LeftToRight, _mono, 11, c.ColorBrush);
                    ctx.DrawText(ft, new Point(lx, y0 + 2));
                    lx += ft.Width + 18;
                }
            }

            // ---- 十字游标：吸附最近点，两路同标（迷你模式不启用） ----
            if (mini || !(_cursor is { } cur) || cur.X < x0 || cur.X > x1 || cur.Y < y0 || cur.Y > y1)
                return;

            double tSpanC = Math.Max(1, tEnd);
            double tCur = (cur.X - x0) / (x1 - x0) * tSpanC;
            ctx.DrawLine(_crossPen, new Point(cur.X, y0), new Point(cur.X, y1));
            ctx.DrawLine(_crossPen, new Point(x0, cur.Y), new Point(x1, cur.Y));

            var tips = new List<(IBrush col, string txt, double py)>();
            for (int i = 0; i < 2 && i < series.Count; i++)
            {
                var dd = data[i];
                var ddT = dataT[i];
                if (dd.Length < 2 || ddT.Length < dd.Length) continue;
                // 光标时刻的最近采样点（时间二分）
                double target = tBase + tCur;
                int blo = 0, bhi = ddT.Length - 1;
                while (bhi - blo > 1)
                {
                    int m = (blo + bhi) / 2;
                    if (ddT[m] < target) blo = m; else bhi = m;
                }
                int j = (target - ddT[blo] <= ddT[bhi] - target) ? blo : bhi;
                double val = dd[j];
                double px = XOfTime(ddT[j] - tBase);
                double py = Range(val);
                ctx.DrawEllipse(series[i].ColorBrush, null, new Point(px, py), 4.5, 4.5);
                tips.Add((series[i].ColorBrush,
                    string.Format(CultureInfo.InvariantCulture, "t={0:F1}s  {1}  {2:F2} ℃",
                        tCur / 1000.0, series[i].Label, val), py));
            }

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
                double bx = cur.X + 16;
                if (bx + bw > x1) bx = cur.X - 16 - bw;
                double by = Math.Clamp(cur.Y - bh / 2, y0, y1 - bh);
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
    }
}
