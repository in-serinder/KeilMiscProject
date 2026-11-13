using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using PDL_upper.Controls;
using PDL_upper.Models;
using PDL_upper.Models.PdlIO;

namespace PDL_upper.ViewModels
{
    /// <summary>均匀 min/max 包络序列：第 i 对覆盖时间 [i·dt, (i+1)·dt)。</summary>
    public sealed class EnvSeries
    {
        public double[] Min = Array.Empty<double>();
        public double[] Max = Array.Empty<double>();
        public double DtMs = 1.0;
        public int N => Min.Length;
        public double TotalMs => N * DtMs;
        public bool Empty => N < 2;
    }

    /// <summary>
    /// 独立查看器（FL Studio 式导航）：上部细节图 + 下部全局导航条 + 视窗统计。
    /// 数据源两种：实时会话（Service 的 FullHistory，边采边看）或 .pdl 文件。
    ///
    /// 性能设计（避免大数据卡顿）：
    ///   1. 数据层与视图层分离——包络只在数据变化（实时 tick / 换文件 / 换通道）时重算并缓存；
    ///      拖动/缩放只做"切片 + 抽稀"，不碰原始数据；
    ///   2. 细节序列按视窗切出后 min/max 抽稀到 ≤3500 对，几何体规模恒定；
    ///   3. 全局导航条降采样到 ≤1500 点。
    /// </summary>
    public partial class ViewerViewModel : ObservableObject
    {
        static readonly string[] Colors = { "#4A9EFF", "#43D17A", "#FFB020", "#B07CFF" };
        private const int DetailMaxPairs = 3500;
        private const int OverviewMaxPts = 1500;

        private AcquisitionService? _svc;                 // 实时模式
        private DispatcherTimer? _timer;
        private bool _isLive;
        private PdlFileReader? _reader;                   // 文件模式
        private PdlFileHeader _hdr = new();
        private readonly EnvSeries?[] _fileV = new EnvSeries?[4];
        private readonly EnvSeries?[] _fileI = new EnvSeries?[4];

        // 缓存的包络（数据层），只在数据变化/换通道时重算
        private int _cachedCh = -1;
        private EnvSeries _envV = new();
        private EnvSeries _envI = new();
        private EnvSeries _envP = new();

        [ObservableProperty] private string _infoText = "";
        [ObservableProperty] private string _statsText = "视窗内无数据";
        [ObservableProperty] private int _selectedChannel;
        [ObservableProperty] private double _viewStartMs;
        [ObservableProperty] private double _viewEndMs;
        [ObservableProperty] private double _totalMs;
        [ObservableProperty] private bool _followLatest = true;
        [ObservableProperty] private bool _hasData;

        // 示波器式 V/div：AutoY=true 时自适应；手动时 Y 轴 = [YCenter ± 2×VPerDiv]
        [ObservableProperty] private bool _autoY = true;
        [ObservableProperty] private double _vPerDiv = 0.1;
        [ObservableProperty] private double _yCenter;
        public string[] VDivOptions { get; } =
            { "10 V", "5 V", "2 V", "1 V", "0.5 V", "0.2 V", "0.1 V", "50 mV", "20 mV", "10 mV", "5 mV" };
        private static readonly double[] VDivValues =
            { 10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005 };
        [ObservableProperty] private int _vDivIndex = 6;    // 默认 0.1 V/div
        partial void OnVDivIndexChanged(int value)
        {
            VPerDiv = VDivValues[Math.Clamp(value, 0, VDivValues.Length - 1)];
            AutoY = false;                                   // 手动选档即切手动模式
        }
        partial void OnVPerDivChanged(double value) => OnPropertyChanged(nameof(VPerDivText));
        public string VPerDivText => UnitFmt.Axis(VPerDiv, "V") + "/格";

        private double vPeakField, vValleyField;             // 最近一次视窗统计（切手动时定中心）

        partial void OnAutoYChanged(bool value)
        {
            if (!value && vPeakField > 0)                    // 切手动时把中心放到当前数据中部
                YCenter = (vValleyField + vPeakField) / 2;
            OnPropertyChanged(nameof(VPerDivText));
        }

        public IReadOnlyList<StaticSeries>? DetailSeries { get; private set; }
        public IReadOnlyList<StaticSeries>? OverviewSeries { get; private set; }

        public string[] ChannelNames { get; } = { "CH0", "CH1", "CH2", "CH3" };

        public event Action? SeriesChanged;

        public ViewerViewModel()
        {
            DetailSeries = Array.Empty<StaticSeries>();
            OverviewSeries = Array.Empty<StaticSeries>();
        }

        partial void OnSelectedChannelChanged(int value)
        {
            _dataDirty = true;
            Rebuild();
        }
        // 视窗变化只更新细节层（切片+抽稀），不碰数据层
        partial void OnViewStartMsChanged(double value) => UpdateDetail();
        partial void OnViewEndMsChanged(double value) => UpdateDetail();

        private bool _dataDirty = true;

        private void SetWindow(double start, double width)
        {
            double w = Math.Max(2000, width);
            double st = Math.Clamp(start, 0, Math.Max(0, TotalMs - w));
            ViewStartMs = st;
            ViewEndMs = st + w;
        }

        // ------------------------------------------------------------ 实时模式
        public void AttachLive(AcquisitionService svc)
        {
            _isLive = true;
            _svc = svc;
            _timer?.Stop();
            _timer = new DispatcherTimer(TimeSpan.FromMilliseconds(400), DispatcherPriority.Background,
                (_, _) => OnLiveTick());
            _timer.Start();
            InfoText = "实时模式：数据来自正在运行的采集会话（无需保存），随采集持续增长";
            Rebuild();
        }

        private void OnLiveTick()
        {
            if (_svc == null) return;
            _dataDirty = true;                                // 数据长了，数据层需刷新
            var env = CurrentEnvV();
            if (_isLive && FollowLatest && !env.Empty)
            {
                double total = env.TotalMs;
                double w = ViewEndMs - ViewStartMs;
                if (w <= 0 || Math.Abs(ViewEndMs - TotalMs) < 1500)   // 视窗在末尾附近才跟随
                    SetWindow(total - w, w);
            }
            Rebuild();
        }

        private EnvSeries EnvOfLive(int ch)
        {
            var env = new EnvSeries();
            if (_svc == null) return env;
            var fv = _svc.FullV[ch];
            (env.Min, env.Max) = fv.Snapshot();
            env.DtMs = Math.Max(0.001, fv.Bucket * _svc.ChartTickMs);
            return env;
        }

        private EnvSeries EnvOfLiveI(int ch)
        {
            var env = new EnvSeries();
            if (_svc == null) return env;
            var fi = _svc.FullI[ch];
            (env.Min, env.Max) = fi.Snapshot();
            env.DtMs = Math.Max(0.001, fi.Bucket * _svc.ChartTickMs);
            return env;
        }

        // ------------------------------------------------------------ 文件模式
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
            _timer?.Stop();
            _isLive = false;
            try
            {
                _reader = PdlFileReader.Open(path);
                var d = _reader.Decode();
                _hdr = _reader.Header;
                for (int ch = 0; ch < 4; ch++)
                {
                    _fileV[ch] = ToEnvelope(d.V[ch], d.T[ch], 100_000);
                    _fileI[ch] = ToEnvelope(d.I[ch], d.T[ch], 100_000);
                }
                InfoText = $"文件：{Path.GetFileName(path)}  ·  {_hdr.SampleRateHz} Hz  ·  档 {_hdr.Tier}" +
                           (_reader.Truncated > 0 ? "  ·  ⚠ 尾部截断" : "");
                _dataDirty = true;
                Rebuild();
                ViewStartMs = 0;
                ViewEndMs = TotalMs;      // 初始全览
                Rebuild();
            }
            catch (Exception ex)
            {
                InfoText = "打开失败：" + ex.Message;
                HasData = false;
            }
        }

        // ------------------------------------------------------------ 数据构建
        static List<double>[] New4() => new List<double>[4] { new(), new(), new(), new() };

        /// <summary>单值序列 → 均匀 min/max 包络（时间取整段均匀分桶）。</summary>
        static EnvSeries ToEnvelope(List<double> v, List<double> t, int maxPairs)
        {
            var env = new EnvSeries();
            if (v.Count < 2 || t.Count < 2) return env;
            double t0 = t[0], t1 = t[t.Count - 1];
            int pairs = Math.Min(maxPairs, v.Count);
            double span = Math.Max(1e-9, t1 - t0);
            var mn = new double[pairs];
            var mx = new double[pairs];
            var filled = new bool[pairs];
            for (int i = 0; i < v.Count; i++)
            {
                int p = Math.Clamp((int)((t[i] - t0) / span * pairs), 0, pairs - 1);
                if (!filled[p]) { mn[p] = v[i]; mx[p] = v[i]; filled[p] = true; }
                else
                {
                    if (v[i] < mn[p]) mn[p] = v[i];
                    if (v[i] > mx[p]) mx[p] = v[i];
                }
            }
            int n = pairs;
            while (n > 0 && !filled[n - 1]) n--;
            env.Min = mn[..n];
            env.Max = mx[..n];
            env.DtMs = span / pairs;
            return env;
        }

        /// <summary>从包络按视窗切出片段（时间 = 桶起点）。</summary>
        static (double[] mn, double[] mx, double t0, double t1) SliceEnv(
            EnvSeries env, double t0, double t1)
        {
            int n = env.N;
            if (n == 0) return (Array.Empty<double>(), Array.Empty<double>(), 0, 0);
            int lo = Math.Clamp((int)(t0 / env.DtMs), 0, n - 1);
            int hi = Math.Clamp((int)(t1 / env.DtMs) + 1, 1, n);
            if (hi - lo < 2) { lo = Math.Max(0, lo - 1); hi = Math.Min(n, lo + 2); }
            var mn = new double[hi - lo];
            var mx = new double[hi - lo];
            for (int i = lo; i < hi; i++)
            {
                mn[i - lo] = env.Min[i];
                mx[i - lo] = env.Max[i];
            }
            return (mn, mx, lo * env.DtMs, (hi - 1) * env.DtMs);
        }

        /// <summary>重载：直接接 SliceEnv 的切片结果。</summary>
        static (double[] mn, double[] mx, double t0, double t1, double dtMs) DecimatePairs(
            (double[] mn, double[] mx, double t0, double t1) slice, int maxPairs)
        {
            int n = slice.mn.Length;
            double dt = n > 1 ? (slice.t1 - slice.t0) / (n - 1) : 1.0;
            return DecimatePairs(slice.mn, slice.mx, slice.t0, dt, maxPairs);
        }

        /// <summary>min/max 对抽稀：按步长合并（保持包络，不丢极值）。</summary>
        static (double[] mn, double[] mx, double t0, double t1, double dtMs) DecimatePairs(
            double[] mn, double[] mx, double t0, double dtMs, int maxPairs)
        {
            int n = mn.Length;
            if (n <= maxPairs || n < 2) return (mn, mx, t0, t0 + Math.Max(0, n - 1) * dtMs, dtMs);
            int stride = (int)Math.Ceiling(n / (double)maxPairs);
            int outN = (n + stride - 1) / stride;
            var om = new double[outN];
            var ox = new double[outN];
            for (int k = 0; k < outN; k++)
            {
                int s = k * stride, e = Math.Min(s + stride, n);
                double a = double.MaxValue, b = double.MinValue;
                for (int i = s; i < e; i++)
                {
                    if (mn[i] < a) a = mn[i];
                    if (mx[i] > b) b = mx[i];
                }
                om[k] = a; ox[k] = b;
            }
            return (om, ox, t0, t0 + (outN - 1) * dtMs * stride, dtMs * stride);
        }

        private EnvSeries CurrentEnvV() => _isLive && _svc != null ? EnvOfLive(SelectedChannel)
            : (_fileV[SelectedChannel] ?? new EnvSeries());

        // ------------------------------------------------------------ 主重建
        /// <summary>数据层 + 视图层全量重建（数据变化 / 换通道 / 打开文件时）。</summary>
        public void Rebuild()
        {
            int ch = SelectedChannel;
            bool live = _isLive && _svc != null;

            // ---- 数据层：仅当脏（live tick / 换通道 / 换文件）才重算并缓存 ----
            if (_dataDirty || _envV.Min.Length == 0 || _cachedCh != ch)
            {
                if (live)
                {
                    _envV = EnvOfLive(ch);
                    _envI = EnvOfLiveI(ch);
                }
                else
                {
                    _envV = _fileV[ch] ?? new EnvSeries();
                    _envI = _fileI[ch] ?? new EnvSeries();
                }
                _envP = new EnvSeries { DtMs = _envV.DtMs };
                if (_envI.N == _envV.N)
                {
                    _envP.Min = new double[_envV.N];
                    _envP.Max = new double[_envV.N];
                    for (int i = 0; i < _envV.N; i++)
                    {
                        double p1 = _envV.Min[i] * _envI.Min[i];
                        double p2 = _envV.Min[i] * _envI.Max[i];
                        double p3 = _envV.Max[i] * _envI.Min[i];
                        double p4 = _envV.Max[i] * _envI.Max[i];
                        _envP.Min[i] = Math.Min(Math.Min(p1, p2), Math.Min(p3, p4));
                        _envP.Max[i] = Math.Max(Math.Max(p1, p2), Math.Max(p3, p4));
                    }
                }
                _cachedCh = ch;
                _dataDirty = false;
            }

            TotalMs = _envV.TotalMs;
            if (_envV.Empty)
            {
                HasData = false;
                StatsText = "暂无数据" + (live ? "—— 等待采集产生数据" : "");
                DetailSeries = Array.Empty<StaticSeries>();
                OverviewSeries = Array.Empty<StaticSeries>();
                OnPropertyChanged(nameof(DetailSeries));
                OnPropertyChanged(nameof(OverviewSeries));
                SeriesChanged?.Invoke();
                return;
            }
            HasData = true;

            // ---- 全局导航条：V / I / P 三条中线，各自降采样 ≤1500 点 ----
            var ovColor = new ImmutableSolidColorBrush(Color.Parse(Colors[ch]));
            var ovList = new List<StaticSeries>();
            double ovDt = _envV.DtMs;
            {
                double dt = ovDt;
                ovList.Add(new StaticSeries
                {
                    Label = "V", Color = ovColor, Y = DecimateMid(OvMids(_envV), ref dt),
                    T0 = 0, T1 = Math.Max(1, dt * 1),
                });
            }
            if (!_envI.Empty)
            {
                double dt = _envI.DtMs;
                ovList.Add(new StaticSeries
                {
                    Label = "I", Color = new ImmutableSolidColorBrush(Color.Parse("#B8C0CC")),
                    Y = DecimateMid(OvMids(_envI), ref dt),
                    T0 = 0, T1 = Math.Max(1, dt * 1),
                });
            }
            if (!_envP.Empty)
            {
                double dt = _envP.DtMs;
                ovList.Add(new StaticSeries
                {
                    Label = "P", Color = new ImmutableSolidColorBrush(Color.Parse("#F06292")),
                    Y = DecimateMid(OvMids(_envP), ref dt),
                    T0 = 0, T1 = Math.Max(1, dt * 1),
                });
            }
            OverviewSeries = ovList;
            OnPropertyChanged(nameof(OverviewSeries));

            UpdateDetail();
        }

        /// <summary>包络 → 中点序列。</summary>
        private static double[] OvMids(EnvSeries env)
        {
            var m = new double[env.N];
            for (int i = 0; i < m.Length; i++) m[i] = (env.Min[i] + env.Max[i]) / 2;
            return m;
        }

        /// <summary>步长平均降采样到 ≤maxPts（返回后 dt = 原dt×步长）。</summary>
        private static double[] DecimateMid(double[] src, ref double dtMs)
        {
            int n = src.Length;
            if (n <= OverviewMaxPts) return src;
            int stride = (int)Math.Ceiling(n / (double)OverviewMaxPts);
            var outArr = new double[(n + stride - 1) / stride];
            for (int k = 0; k < outArr.Length; k++)
            {
                int a = k * stride, b = Math.Min(a + stride, n);
                double sum = 0;
                for (int i = a; i < b; i++) sum += src[i];
                outArr[k] = sum / (b - a);
            }
            dtMs *= stride;
            return outArr;
        }

        /// <summary>视图层：按视窗切片 + 抽稀（拖动/缩放时高频调用，代价恒定）。</summary>
        private void UpdateDetail()
        {
            if (!_envV.Empty && _envV.Min.Length > 0)
            {
                HasData = true;
                int ch = SelectedChannel;
                var ovColor = new ImmutableSolidColorBrush(Color.Parse(Colors[ch]));

                double t0 = Math.Clamp(ViewStartMs, 0, Math.Max(0, TotalMs - 50));
                double t1 = Math.Clamp(ViewEndMs, t0 + 50, TotalMs);

                var detail = new List<StaticSeries>();

                // 电压
                var (mn, mx, seg0, seg1, dt1) = DecimatePairs(SliceEnv(_envV, t0, t1), DetailMaxPairs);
                double[] mid = Array.Empty<double>();
                if (mn.Length >= 2)
                {
                    mid = new double[mn.Length];
                    for (int i = 0; i < mid.Length; i++) mid[i] = (mn[i] + mx[i]) / 2;
                    detail.Add(new StaticSeries
                    {
                        Label = $"{ChannelNames[ch]} 电压", Color = ovColor, Unit = "V",
                        Y = mid, Min = mn, Max = mx, T0 = seg0, T1 = seg1,
                    });
                }

                // 电流
                var (imn, imx, _, _, _) = DecimatePairs(SliceEnv(_envI, t0, t1), DetailMaxPairs);
                double[] imid = Array.Empty<double>();
                if (imn.Length >= 2)
                {
                    imid = new double[imn.Length];
                    for (int i = 0; i < imid.Length; i++) imid[i] = (imn[i] + imx[i]) / 2;
                    detail.Add(new StaticSeries
                    {
                        Label = $"{ChannelNames[ch]} 电流", Color = new ImmutableSolidColorBrush(Color.Parse("#E6E8EC")), Unit = "A",
                        Y = imid, Min = imn, Max = imx, T0 = seg0, T1 = seg1,
                    });
                }

                // 功率
                var (pmn, pmx, _, _, _) = DecimatePairs(SliceEnv(_envP, t0, t1), DetailMaxPairs);
                double[] pmid = Array.Empty<double>();
                if (pmn.Length >= 2)
                {
                    pmid = new double[pmn.Length];
                    for (int i = 0; i < pmid.Length; i++) pmid[i] = (pmn[i] + pmx[i]) / 2;
                    detail.Add(new StaticSeries
                    {
                        Label = $"{ChannelNames[ch]} 功率", Color = new ImmutableSolidColorBrush(Color.Parse("#F06292")), Unit = "W",
                        Y = pmid, Min = pmn, Max = pmx, T0 = seg0, T1 = seg1,
                    });
                }
                DetailSeries = detail;
                OnPropertyChanged(nameof(DetailSeries));

                // 视窗统计
                double vPeak = mx.Length > 0 ? mx.Max() : 0;
                double vValley = mn.Length > 0 ? mn.Min() : 0;
                double vAvg = mid.Length > 0 ? mid.Average() : 0;
                vPeakField = vPeak; vValleyField = vValley;
                string iStat = "";
                if (imn.Length >= 2)
                {
                    double iPeak = imx.Max(), iValley = imn.Min(), iAvg = imid.Average();
                    iStat = string.Format(CultureInfo.InvariantCulture,
                        "      I：峰 {0} · 谷 {1} · 均 {2}",
                        UnitFmt.Amps(iPeak), UnitFmt.Amps(iValley), UnitFmt.Amps(iAvg));
                }
                string pStat = "";
                if (pmid.Length >= 2)
                {
                    pStat = string.Format(CultureInfo.InvariantCulture,
                        "      P：峰 {0} · 均 {1}",
                        UnitFmt.Watts(pmx.Max()), UnitFmt.Watts(pmid.Average()));
                }
                StatsText = string.Format(CultureInfo.InvariantCulture,
                    "视窗 {0:F2}-{1:F2}s（{2:F2}s）    V：峰 {3} · 谷 {4} · 均 {5}{6}{7}",
                    t0 / 1000, t1 / 1000, (t1 - t0) / 1000,
                    UnitFmt.Volts(vPeak), UnitFmt.Volts(vValley), UnitFmt.Volts(vAvg), iStat, pStat);
            }
            else
            {
                HasData = _envV.Min.Length > 0;
            }
            SeriesChanged?.Invoke();
        }

        [RelayCommand]
        private void ResetView()
        {
            ViewStartMs = 0;
            ViewEndMs = TotalMs;
        }
    }
}
