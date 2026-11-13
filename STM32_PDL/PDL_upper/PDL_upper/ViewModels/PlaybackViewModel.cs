using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using PDL_upper.Models.PdlIO;
using PDL_upper.Controls;

namespace PDL_upper.ViewModels
{
    /// <summary>统计表一行。</summary>
    public sealed class PlaybackRow
    {
        public string Label { get; init; } = "";
        public string Color { get; init; } = "#8B93A1";
        public string Vavg { get; init; } = "--";
        public string Vmax { get; init; } = "--";
        public string Vmin { get; init; } = "--";
        public string Vpp { get; init; } = "--";
        public string Ripple { get; init; } = "--";
        public string Imax { get; init; } = "--";
        public string Pavg { get; init; } = "--";
    }

    /// <summary>.pdl 回放：打开文件 → 概览曲线（抽稀）+ 统计 + 温度。</summary>
    public partial class PlaybackViewModel : ObservableObject
    {
        static readonly string[] Colors = { "#4A9EFF", "#43D17A", "#FFB020", "#B07CFF" };
        static readonly string[] TColors = { "#FF8A65", "#FFD54F" };

        [ObservableProperty] private string _infoText = "尚未打开文件。点击「打开 .pdl」选择记录文件。";
        [ObservableProperty] private bool _hasData;
        [ObservableProperty] private ObservableCollection<PlaybackRow> _stats = new();

        // 时间视窗（ms）：图表滚轮/拖拽与滑块都写回这里，曲线按视窗重抽稀
        [ObservableProperty] private double _viewStartMs;
        [ObservableProperty] private double _viewEndMs;
        [ObservableProperty] private double _totalMs;
        [ObservableProperty] private string _rangeText = "";

        private DecodedData? _all;
        private PdlFileHeader _hdr = new();

        public IReadOnlyList<StaticSeries>? PowerSeries { get; private set; }
        public IReadOnlyList<StaticSeries>? TempSeries { get; private set; }

        partial void OnViewStartMsChanged(double value) => UpdateSeries();
        partial void OnViewEndMsChanged(double value) => UpdateSeries();

        [RelayCommand]
        private void ResetView()
        {
            ViewStartMs = 0;
            ViewEndMs = TotalMs;
        }

        /// <summary>时间二分：均匀序列中 [t0,t1) 的下标范围。</summary>
        static (int lo, int hi) TimeRange(List<double> t, double t0, double t1)
        {
            if (t.Count == 0) return (0, 0);
            int lo = t.BinarySearch(t0);
            if (lo < 0) lo = ~lo;
            int hi = t.BinarySearch(t1);
            if (hi < 0) hi = ~hi;
            return (Math.Clamp(lo, 0, t.Count), Math.Clamp(hi, 0, t.Count));
        }

        List<double> Slice(List<double> src, int lo, int hi)
        {
            var r = new List<double>(Math.Max(0, hi - lo));
            for (int i = lo; i < hi; i++) r.Add(src[i]);
            return r;
        }

        /// <summary>取段并 mV -> V（回放曲线/游标都以 V 显示，与量程标签一致）。</summary>
        List<double> SliceV(List<double> src, int lo, int hi)
        {
            var r = new List<double>(Math.Max(0, hi - lo));
            for (int i = lo; i < hi; i++) r.Add(src[i] / 1000.0);
            return r;
        }

        void UpdateSeries()
        {
            if (_all == null) return;
            double total = Math.Max(1, TotalMs);
            double t0 = Math.Clamp(ViewStartMs, 0, total);
            double t1 = Math.Clamp(ViewEndMs, 0, total);
            if (t1 - t0 < 50) t1 = Math.Min(total, t0 + 50);   // 视窗下限 50 ms

            var ps = new List<StaticSeries>();
            for (int ch = 0; ch < 4; ch++)
            {
                var T = _all.T[ch];
                if (T.Count < 2) continue;
                var (lo, hi) = TimeRange(T, t0, t1);
                if (hi - lo < 2) continue;
                ps.Add(new StaticSeries
                {
                    Label = string.Format("CH{0} ({1:0.#} V)", ch, _hdr.Power[ch].FullScaleV),
                    Color = new ImmutableSolidColorBrush(Color.Parse(Colors[ch])),
                    Y = Decimate(SliceV(_all.V[ch], lo, hi), 3000),
                    T0 = T[lo],
                    T1 = T[hi - 1],
                    FullScale = _hdr.Power[ch].FullScaleV,
                });
            }
            PowerSeries = ps;

            var ts = new List<StaticSeries>();
            var tColors = TColors;
            for (int ch = 0; ch < 2; ch++)
            {
                var T = _all.Tt[ch];
                if (T.Count < 2) continue;
                var (lo, hi) = TimeRange(T, t0, t1);
                if (hi - lo < 2) continue;
                ts.Add(new StaticSeries
                {
                    Label = "TH" + ch,
                    Color = new ImmutableSolidColorBrush(Color.Parse(tColors[ch])),
                    Y = Decimate(Slice(_all.Tv[ch], lo, hi), 3000),
                    T0 = T[lo],
                    T1 = T[hi - 1],
                    FullScale = -1,
                });
            }
            TempSeries = ts;
            RangeText = string.Format("视窗 {0:F2} - {1:F2} s（宽度 {2:F2} s）· 滚轮缩放 / 拖拽平移 / 滑块选段",
                t0 / 1000.0, t1 / 1000.0, (t1 - t0) / 1000.0);
            OnPropertyChanged(nameof(PowerSeries));
            OnPropertyChanged(nameof(TempSeries));
            SeriesChanged?.Invoke();
        }

        /// <summary>刷新图表（清空时也调一次，清掉画布）。</summary>
        public event Action? SeriesChanged;

        void NotifySeries()
        {
            OnPropertyChanged(nameof(PowerSeries));
            OnPropertyChanged(nameof(TempSeries));
        }

        [RelayCommand]
        private async Task OpenFileAsync(Window owner)
        {
            var dlg = new OpenFileDialog
            {
                Title = "打开 .pdl 记录文件",
                AllowMultiple = false,
                Filters = { new FileDialogFilter { Name = "PDL 记录", Extensions = { "pdl" } } },
            };
            var files = await dlg.ShowAsync(owner);
            if (files == null || files.Length == 0) return;
            LoadFile(files[0]);
        }

        public void LoadFile(string path)
        {
            try
            {
                var r = PdlFileReader.Open(path);
                var d = r.Decode();
                var h = r.Header;
                _all = d;
                _hdr = h;


                InfoText = $"{Path.GetFileName(path)}  ·  {h.StartedUtc:yyyy-MM-dd HH:mm:ss}  ·  " +
                           $"{h.SampleRateHz} Hz / {h.ReportIntervalMs} ms  ·  档 {h.Tier}  ·  " +
                           $"时长 {TotalMs / 1000.0:F1} s  ·  块 {r.PowerBlocks.Count + r.TempBlocks.Count}" +
                           (r.Truncated > 0 ? $"  ·  ⚠ 尾部不完整（截断 {r.Truncated} B，已读部分可用）" : "") +
                           (r.FooterOk ? "" : "  ·  （无 Footer：会话未正常保存）");

                // ---- 初始化视窗（全览）；曲线由 UpdateSeries 按 [t0,t1) 抽稀 ----
                double tBegin = double.MaxValue, tEnd = 0;
                foreach (var T in d.T)
                    if (T.Count > 1) { tBegin = Math.Min(tBegin, T[0]); tEnd = Math.Max(tEnd, T[T.Count - 1]); }
                TotalMs = tEnd > tBegin ? tEnd - tBegin + 1 : 0;
                HasData = TotalMs > 0;
                // 初始视窗 = 全览（直接赋值会经 OnXxxChanged 触发 UpdateSeries）
                ViewStartMs = 0;
                ViewEndMs = TotalMs;

                // ---- 统计 ----
                var rows = new ObservableCollection<PlaybackRow>();
                for (int ch = 0; ch < 4; ch++)
                {
                    if (d.T[ch].Count < 2) continue;
                    var v = d.V[ch].ToArray();         // mV
                    var i = d.I[ch].ToArray();         // mA
                    double vavg = v.Average();
                    double vmax = v.Max(), vmin = v.Min();
                    double vpp = vmax - vmin;
                    double imax = i.Max(), imin = i.Min();
                    double pavg = 0;
                    for (int k = 0; k < v.Length; k++) pavg += v[k] * i[k];
                    pavg = v.Length > 0 ? pavg / v.Length / 1000.0 : 0;   // mV*mA = µW → /1000 = mW
                    string fmtV(double mv) => mv >= 1000 ? $"{mv / 1000:F3} V" : $"{mv:F1} mV";
                    rows.Add(new PlaybackRow
                    {
                        Label = $"CH{ch}",
                        Color = Colors[ch],
                        Vavg = fmtV(vavg),
                        Vmax = fmtV(vmax),
                        Vmin = fmtV(vmin),
                        Vpp = vpp >= 1000 ? $"{vpp / 1000:F3} V" : $"{vpp:F1} mV",
                        Ripple = Math.Abs(vavg) > 1e-9 ? $"{vpp / Math.Abs(vavg) * 100:F2} %" : "--",
                        Imax = $"{imax / 1000:F3} A (min {imin / 1000:F3})",
                        Pavg = $"{pavg / 1000:F2} W",
                    });
                }
                for (int ch = 0; ch < 2; ch++)
                {
                    if (d.Tv[ch].Count < 2) continue;
                    var t = d.Tv[ch];
                    rows.Add(new PlaybackRow
                    {
                        Label = $"TH{ch}",
                        Color = TColors[ch],
                        Vavg = $"{t.Average():F1} ℃",
                        Vmax = $"{t.Max():F1} ℃",
                        Vmin = $"{t.Min():F1} ℃",
                        Vpp = $"{t.Max() - t.Min():F1} ℃",
                    });
                }
                Stats = rows;
                OnPropertyChanged(nameof(Stats));
            }
            catch (Exception ex)
            {
                InfoText = "打开失败：" + ex.Message;
                HasData = false;
                _all = null;
                PowerSeries = TempSeries = null;
                TotalMs = 0;
                NotifySeries();
                SeriesChanged?.Invoke();
            }
        }

        /// <summary>等距抽稀：保持极值可见（每格取 min/max）。</summary>
        static double[] Decimate(List<double> src, int maxPts)
        {
            if (src.Count <= maxPts) return src.ToArray();
            int bucket = (int)Math.Ceiling(src.Count / (double)maxPts);
            var outList = new List<double>(maxPts * 2);
            for (int i = 0; i < src.Count; i += bucket)
            {
                double mn = double.MaxValue, mx = double.MinValue;
                int end = Math.Min(i + bucket, src.Count);
                for (int k = i; k < end; k++)
                {
                    if (src[k] < mn) mn = src[k];
                    if (src[k] > mx) mx = src[k];
                }
                outList.Add(mn);
                outList.Add(mx);
            }
            return outList.ToArray();
        }
    }
}
