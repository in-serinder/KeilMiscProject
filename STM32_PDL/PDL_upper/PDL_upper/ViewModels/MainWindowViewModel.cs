using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using PDL_upper.Views;
using System.Text;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using PDL_upper.Models;
using Avalonia;
using PDL_upper.Models.Transport;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using PDL_upper.Models.Protocol;
using PDL_upper.Models.PdlIO;

namespace PDL_upper.ViewModels
{
    public partial class ChannelCardViewModel : ObservableObject
    {
        public static readonly string[] Colors = { "#4A9EFF", "#43D17A", "#FFB020", "#B07CFF" };

        public ChannelCardViewModel(int idx)
        {
            Index = idx;
            Label = $"CH{idx}";
            ColorBrush = new ImmutableSolidColorBrush(Avalonia.Media.Color.Parse(Colors[idx]));
        }
        public int Index { get; }

        [ObservableProperty] private string _label = "CH0";
        [ObservableProperty] private string _rangeText = "";
        [ObservableProperty] private double _voltage;
        [ObservableProperty] private double _current;
        [ObservableProperty] private double _power;
        [ObservableProperty] private double _vpp;
        [ObservableProperty] private double _ripplePct;
        [ObservableProperty] private double _vmax;
        [ObservableProperty] private double _vmin;
        [ObservableProperty] private double _pavg;
        [ObservableProperty] private double _ratio;
        [ObservableProperty] private bool _saturated;

        public string Color => Colors[Index];
        public IBrush ColorBrush { get; }
        public double FullScale { get; set; } = 1.0;

        // Text/VppText 是派生值：字段变了要手动通知，否则界面永远停在初始值（踩过）
        partial void OnVppChanged(double value) => OnPropertyChanged(nameof(VppText));
        partial void OnSaturatedChanged(bool value) { }
        public string VppText => Vpp >= 1 ? $"{Vpp:F1} mV" : $"{Vpp * 1000:F0} µV";
    }

    public partial class TempCardViewModel : ObservableObject
    {
        public TempCardViewModel(int idx, TempLive live)
        {
            Index = idx;
            Label = $"TH{idx}";
            Live = live;
            ColorBrush = new ImmutableSolidColorBrush(
                Avalonia.Media.Color.Parse(idx == 0 ? "#FF8A65" : "#FFD54F"));
        }
        public int Index { get; }
        /// <summary>温度历史来源（迷你折线与温度窗口共用）。</summary>
        public TempLive Live { get; }
        [ObservableProperty] private string _label = "TH0";
        [ObservableProperty] private double _celsius = 0;
        [ObservableProperty] private bool _present = true;
        public string Color => Index == 0 ? "#FF8A65" : "#FFD54F";
        public IBrush ColorBrush { get; }
        public string Text => Present ? $"{Celsius:F1} ℃" : "-- ℃";
        partial void OnCelsiusChanged(double value) => OnPropertyChanged(nameof(Text));
        partial void OnPresentChanged(bool value) => OnPropertyChanged(nameof(Text));
    }

    public sealed class LogEntry
    {
        public string Time { get; init; } = "";
        public string Level { get; init; } = "";
        public string Msg { get; init; } = "";
    }

    public partial class LogViewModel : ObservableObject
    {
        public ObservableCollection<LogEntry> Entries { get; } = new();

        public void Add(string level, string msg)
        {
            var e = new LogEntry
            {
                Time = DateTime.Now.ToString("HH:mm:ss.fff"),
                Level = level,
                Msg = msg,
            };
            Dispatcher.UIThread.Post(() =>
            {
                Entries.Add(e);
                while (Entries.Count > 600)
                    Entries.RemoveAt(0);
            });
        }

        public void Clear()
        {
            Dispatcher.UIThread.Post(Entries.Clear);
        }
    }

    public partial class ChartViewModel : ObservableObject
    {
        public ChartViewModel(AcquisitionService svc)
        {
            Buffer = svc.Chart;
            for (int i = 0; i < 4; i++)
            {
                int i1 = i;
                Visibility.Add(new ChartSeriesViewModel($"CH{i}", () => Buffer)
                {
                    IsVisible = true,
                });
            }
        }

        public ChartBuffer Buffer { get; }
        public ObservableCollection<ChartSeriesViewModel> Visibility { get; } = new();
        [ObservableProperty] private int _windowPoints = 600;
    }

    public partial class ChartSeriesViewModel : ObservableObject
    {
        private readonly Func<ChartBuffer> _buffer;
        public ChartSeriesViewModel(string label, Func<ChartBuffer> buffer)
        {
            Label = label;
            _buffer = buffer;
        }
        public string Label { get; }
        [ObservableProperty] private bool _isVisible = true;
        public double[] Data => _buffer().Snapshot(int.Parse(Label.Substring(2)));
        public double FullScaleV { get; set; } = 1.0;
    }

    /// <summary>单通道曲线面板的 VM：主窗口停靠位与弹出窗口共用同一实例。</summary>
    public partial class ChannelChartViewModel : ObservableObject
    {
        public ChannelChartViewModel(int idx, AcquisitionService svc)
        {
            Index = idx;
            Service = svc;
            Buffer = svc.Chart;
            ColorBrush = new ImmutableSolidColorBrush(Color.Parse(ChannelCardViewModel.Colors[idx]));
            Series = new ChartSeriesViewModel($"CH{idx}", () => Buffer);
        }

        public int Index { get; }
        public ChartBuffer Buffer { get; }
        public AcquisitionService Service { get; }

        /// <summary>"全程"模式的数据源（min/max 包络）+ 模式开关</summary>
        [ObservableProperty] private bool _fullTimeMode;
        public FullHistory FullV => Service.FullV[Index];
        public FullHistory FullI => Service.FullI[Index];
        public double ChartTickMs => Service.ChartTickMs;
        public ChartSeriesViewModel Series { get; }
        public string Label => $"CH{Index}";
        public IBrush ColorBrush { get; }

        [ObservableProperty] private bool _isPopped;
        [ObservableProperty] private double _fullScaleV = 1.0;
        [ObservableProperty] private double _fullScaleA = 5.0;
        [ObservableProperty] private double _lastVolt;
        [ObservableProperty] private double _lastAmp;

        public string RangeText => $"0–{FullScaleV:F2} V · 0–{FullScaleA:F1} A";
        partial void OnFullScaleVChanged(double v)
        {
            Series.FullScaleV = v;
            OnPropertyChanged(nameof(RangeText));
        }
        partial void OnFullScaleAChanged(double v) => OnPropertyChanged(nameof(RangeText));

        // 弹出窗口里的“停靠回主窗口”按钮
        [RelayCommand]
        private void Dock() => DockRequested?.Invoke(this, EventArgs.Empty);
        public event EventHandler? DockRequested;
    }

    /// <summary>ACQUIRE 面板的全部参数。运行中这些控件会被整体禁用。</summary>
    public partial class AcquireViewModel : ObservableObject
    {
        public bool[] PowerFlags { get; } = { true, true, true, true };
        public bool[] TempFlags { get; } = { true, true };

        [ObservableProperty] private string _rateText = "1000";
        [ObservableProperty] private string _intervalText = "100";
        [ObservableProperty] private int _tierIndex = 0;
        [ObservableProperty] private string _durationText = "0";
        [ObservableProperty] private int _modeIndex = 0;
        [ObservableProperty] private int _trigSourceIndex = 0;
        [ObservableProperty] private int _trigChIndex = 0;
        [ObservableProperty] private string _startValText = "12000";
        [ObservableProperty] private string _stopValText = "8000";
        [ObservableProperty] private string _startHoldText = "50";
        [ObservableProperty] private string _stopHoldText = "50";
        [ObservableProperty] private bool _autoRearm = false;

        public string[] TierNames { get; } = { "0 逐点无损", "1 10ms 桶", "2 秒级", "3 分钟级" };
        public string[] ModeNames { get; } = { "手动", "定时", "触发" };
        public string[] TrigSourceNames { get; } = { "电压 mV", "电流 mA", "功率 mW" };
        public string[] TrigChNames { get; } = { "CH0", "CH1", "CH2", "CH3" };

        public string TrigUnit => TrigSourceIndex switch
        {
            1 => "mA",
            2 => "mW",
            _ => "mV",
        };

        public bool IsTriggerMode => ModeIndex == 2;

        // 左栏摘要：配置进弹窗后，主界面只留一行状态
        public string TrigSummary => ModeIndex switch
        {
            2 => $"触发：{TrigSourceNames[TrigSourceIndex]} @ {TrigChNames[TrigChIndex]} · " +
                 $"开≥{StartValText}{TrigUnit} 保持{StartHoldText}ms · " +
                 $"关≤{StopValText}{TrigUnit} 保持{StopHoldText}ms" +
                 (AutoRearm ? " · 自动重武装" : ""),
            1 => $"定时：开始后 {DurationText} s 自动停止",
            _ => "手动：手动开始，手动停止",
        };

        partial void OnModeIndexChanged(int value)
        {
            OnPropertyChanged(nameof(IsTriggerMode));
            OnPropertyChanged(nameof(TrigSummary));
        }
        partial void OnTrigSourceIndexChanged(int value)
        {
            OnPropertyChanged(nameof(TrigUnit));
            OnPropertyChanged(nameof(TrigSummary));
        }
        partial void OnTrigChIndexChanged(int value) => OnPropertyChanged(nameof(TrigSummary));
        partial void OnStartValTextChanged(string value) => OnPropertyChanged(nameof(TrigSummary));
        partial void OnStopValTextChanged(string value) => OnPropertyChanged(nameof(TrigSummary));
        partial void OnStartHoldTextChanged(string value) => OnPropertyChanged(nameof(TrigSummary));
        partial void OnStopHoldTextChanged(string value) => OnPropertyChanged(nameof(TrigSummary));
        partial void OnAutoRearmChanged(bool value) => OnPropertyChanged(nameof(TrigSummary));
        partial void OnDurationTextChanged(string value) => OnPropertyChanged(nameof(TrigSummary));

        public AcquireParams Build(byte tempRes)
        {
            byte pm = 0, tm = 0;
            for (int i = 0; i < 4; i++) if (PowerFlags[i]) pm |= (byte)(1 << i);
            for (int i = 0; i < 2; i++) if (TempFlags[i]) tm |= (byte)(1 << i);
            // 触发只在"触发"模式下武装——手动/定时模式若带触发标志，
            // 设备会按阈值发 TRIG 事件，UI 状态会被事件打乱（踩过）
            bool trig = ModeIndex == 2;
            return new AcquireParams
            {
                PowerMask = pm,
                TempMask = tm,
                RecordMode = (byte)ModeIndex,
                Tier = (byte)TierIndex,
                SampleRateHz = ParseUint(RateText, 1000),
                ReportIntervalMs = ParseUint(IntervalText, 100),
                DurationS = ParseUint(DurationText, 0),
                TempIntervalMs = 1000,
                TrigSource = (byte)TrigSourceIndex,
                TrigCh = (byte)TrigChIndex,
                TrigStartEn = trig,
                TrigStopEn = trig,
                TrigAutoRearm = AutoRearm,
                StartHoldMs = (ushort)Math.Max(1, ParseUint(StartHoldText, 50)),
                StopHoldMs = (ushort)Math.Max(1, ParseUint(StopHoldText, 50)),
                StartVal = (int)ParseInt(StartValText, 0),
                StopVal = (int)ParseInt(StopValText, 0),
            };
        }

        private static uint ParseUint(string s, uint def)
            => uint.TryParse(s?.Trim(), out var v) ? v : def;
        private static int ParseInt(string s, int def)
            => int.TryParse(s?.Trim(), out var v) ? v : def;
    }

    public partial class MainWindowViewModel : ObservableObject
    {
        public AcquisitionService Service { get; } = new();
        public LogViewModel Log { get; } = new();
        public ChartViewModel Chart { get; }
        public ObservableCollection<ChannelCardViewModel> Channels { get; }
        public ObservableCollection<TempCardViewModel> Temps { get; }
        public ObservableCollection<ChannelChartViewModel> ChannelCharts { get; }

        private DeviceSession? _session;
        private DispatcherTimer? _uiTimer;
        private ChartWindow? _chartWindow;
        private LogWindow? _logWindow;
        private TempWindow? _tempWindow;
        private PlaybackWindow? _playbackWindow;
        private ViewerWindow? _viewerWindow;
        private PdlFileWriter? _recorder;
        private bool _spoolSaved;
        private PdlSummary? _lastSummary;
        private readonly ChannelWindow?[] _channelWindows = new ChannelWindow[4];

        /// <summary>请求主窗口弹出配置对话框："trigger" / "timing"。</summary>
        public event Action<string>? RequestConfigDialog;

        /// <summary>主窗口引用（保存对话框的 owner）。</summary>
        public Window? Owner { get; set; }

        [ObservableProperty] private string _host = AppArgs.Host;
        [ObservableProperty] private int _port = AppArgs.Port;
        [ObservableProperty] private string _statusText = "未连接";
        [ObservableProperty] private string _connectText = "连接";
        [ObservableProperty] private string _deviceInfo = "";
        [ObservableProperty] private string _linkText = "";
        [ObservableProperty] private bool _isConnected;
        [ObservableProperty] private bool _isAcquiring;

        // CanExecute 依赖这两个标志：变了必须手动通知命令刷新，
        // 否则连接成功后“开始采集”仍停在创建时的禁用态（踩过：按钮永远点不了）
        partial void OnIsConnectedChanged(bool value)
        {
            ConnectCommand.NotifyCanExecuteChanged();
            AcquireCommand.NotifyCanExecuteChanged();
            AbortCommand.NotifyCanExecuteChanged();
        }
        partial void OnIsAcquiringChanged(bool value)
        {
            ConnectCommand.NotifyCanExecuteChanged();
            AcquireCommand.NotifyCanExecuteChanged();
            AbortCommand.NotifyCanExecuteChanged();
        }
        [ObservableProperty] private bool _unsavedSpool;
        [ObservableProperty] private string _spoolText = "";

        public AcquireViewModel Acquire { get; } = new();

        public MainWindowViewModel()
        {
            Channels = new ObservableCollection<ChannelCardViewModel>
            {
                new(0), new(1), new(2), new(3),
            };
            Temps = new ObservableCollection<TempCardViewModel>
            {
                new(0, Service.Temp[0]), new(1, Service.Temp[1]),
            };
            Chart = new ChartViewModel(Service);
            ChannelCharts = new ObservableCollection<ChannelChartViewModel>
            {
                new(0, Service), new(1, Service),
                new(2, Service), new(3, Service),
            };
            foreach (var c in ChannelCharts)
                c.DockRequested += (_, _) => DockChannelCore(c.Index);
            ApplyConfigsToChart();

            // 切到“定时/触发”模式时自动弹配置窗口（用户要求：配置要直观）
            Acquire.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(AcquireViewModel.ModeIndex))
                    RequestConfigDialog?.Invoke(Acquire.ModeIndex switch
                    {
                        2 => "trigger",
                        1 => "timing",
                        _ => "",
                    });
            };
        }

        private void ApplyConfigsToChart()
        {
            for (int i = 0; i < 4; i++)
            {
                Chart.Visibility[i].FullScaleV = Service.Power[i]?.FullScaleV ?? 1.0;
                ChannelCharts[i].FullScaleV = Service.Power[i]?.FullScaleV ?? 1.0;
                ChannelCharts[i].FullScaleA = (Service.Power[i]?.FsCurrentMa ?? 5000) / 1000.0;
            }
        }

        // ------------------------------------------------------------ 连接
        [RelayCommand(CanExecute = nameof(CanConnect))]
        private async Task ConnectAsync()
        {
            if (IsConnected) { Disconnect(); return; }
            try
            {
                StatusText = "连接中…";
                var tp = new TcpTransport(Host, Port);
                tp.Open();
                _session = new DeviceSession(tp);
                _session.Log += m => Log.Add("PROT", m);
                _session.LinkChanged += s => { };
                _session.EventReceived += OnEvent;
                _session.BlockPayload += b =>
                {
                    Service.HandleBlock(b);
                    _recorder?.AppendBlock(b);
                };
                _session.Start();

                uint nonce = (uint)Random.Shared.Next();
                var id = await _session.IdentifyAsync(nonce);
                if (id.ProtoMax < 4 || id.ProtoMin > 4)
                    Log.Add("WARN", $"协议版本 {id.ProtoMin}..{id.ProtoMax} 与上位机 1..4 不完全匹配");

                var cfgs = new ChannelConfig[id.PowerChannels];
                for (int ch = 0; ch < id.PowerChannels; ch++)
                {
                    cfgs[ch] = await _session.GetChConfigAsync(ch);
                    Channels[ch].Label = $"CH{ch}";
                    Channels[ch].RangeText =
                        $"0–{cfgs[ch].FullScaleV:F2} V · {cfgs[ch].LsbMv:F2} mV · {cfgs[ch].FsCurrentMa / 1000.0:F0} A";
                    Channels[ch].Ratio = 0;
                }
                for (int ch = 0; ch < id.TempChannels; ch++)
                    await _session.GetChConfigAsync(id.PowerChannels + ch);
                Service.SetConfig(cfgs);
                ApplyConfigsToChart();

                await _session.TimeSyncAsync();
                var temps = await _session.ReadTempOnceAsync();
                for (int i = 0; i < 2 && i < temps.Length; i++)
                    Temps[i].Celsius = temps[i] == short.MaxValue ? 0 : temps[i] / 16.0;

                DeviceInfo = $"固件 {id.FwText} · {id.PowerChannels} 功率 + {id.TempChannels} 温度 · 上限 {id.MaxSampleRateHz} Hz";
                IsConnected = true;
                ConnectText = "断开";
                StatusText = "已连接 · 空闲";
                Log.Add("INFO", $"识别成功 {DeviceInfo}（RTT {_session.RttMs:F1} ms）");
            }
            catch (Exception ex)
            {
                Log.Add("ERR", "连接失败：" + ex.Message);
                StatusText = "连接失败";
                ConnectText = "连接";
                Disconnect();
            }
            finally
            {
                ConnectCommand.NotifyCanExecuteChanged();
            }
        }

        private bool CanConnect => !IsAcquiring;

        private void Disconnect()
        {
            _session?.Dispose();
            _session = null;
            IsConnected = false;
            IsAcquiring = false;
            ConnectText = "连接";
            StatusText = "未连接";
            for (int i = 0; i < 4; i++)
                DockChannelCore(i);
        }

        // ------------------------------------------------------------ 采集
        [RelayCommand(CanExecute = nameof(CanAcquire))]
        private async Task AcquireAsync()
        {
            if (_session == null) return;
            var p = Acquire.Build(12);
            var err = p.Validate();
            if (err != "")
            {
                Log.Add("ERR", "参数校验：" + err);
                return;
            }
            try
            {
                var ack = await _session.AcquireAsync(p);
                IsAcquiring = true;
                Service.ResetStats();
                // 全程历史按实际生效速率刷新时间刻度
                Service.ChartTickMs = Service.ChartDiv / Math.Max(1u, ack.EffSampleRateHz) * 1000.0;
                // 触发模式先关历史门控，等 TRIG_STARTED 打开（否则会把等待期/触发前数据记进去）
                Service.GateFullHistory = Acquire.ModeIndex != 2;
                StartRecorder(p);
                StatusText = ack.State == Pdl.DevState.Armed ? "已武装 · 等待触发" : "采集中";
                Log.Add("INFO", $"ACQUIRE 实际生效 {ack.EffSampleRateHz} Hz / {ack.EffReportIntervalMs} ms" +
                                (ack.EffSampleRateHz != p.SampleRateHz ? "（设备已取整）" : ""));
            }
            catch (Exception ex)
            {
                Log.Add("ERR", "ACQUIRE 失败：" + ex.Message);
            }
            finally
            {
                ConnectCommand.NotifyCanExecuteChanged();
                AcquireCommand.NotifyCanExecuteChanged();
                AbortCommand.NotifyCanExecuteChanged();
            }
        }

        private bool CanAcquire => IsConnected && !IsAcquiring;

        [RelayCommand(CanExecute = nameof(CanAbort))]
        private async Task AbortAsync()
        {
            if (_session == null) return;
            try
            {
                StatusText = "正在收尾（排空缓冲）…";
                var f = await _session.AbortAsync(false);
                var extra = new byte[f.Payload.Length - 4];
                Array.Copy(f.Payload, 4, extra, 0, extra.Length);
                var a = AbortAck.Parse(extra);
                Log.Add("INFO", $"ABORT：{a.ReasonText}，tick {a.TotalTicks}，块 {a.TotalBlocks}，丢 {a.DroppedTicks}，时长 {a.DurationMs / 1000.0:F1} s");
            }
            catch (PdlStatusException pex) when (pex.Status == Pdl.St.BadState)
            {
                // 连接即可点停止（按钮恒可用），空闲时点它不是错误
                Log.Add("INFO", "设备当前空闲，无需停止");
                StatusText = "已连接 · 空闲";
            }
            catch (Exception ex)
            {
                Log.Add("ERR", "ABORT 失败：" + ex.Message);
            }
            finally
            {
                IsAcquiring = false;
                StatusText = "已连接 · 空闲";
                FinalizeRecorder("手动停止");
                ConnectCommand.NotifyCanExecuteChanged();
                AcquireCommand.NotifyCanExecuteChanged();
                AbortCommand.NotifyCanExecuteChanged();
            }
        }

        private bool CanAbort => IsConnected;   // 连接即可停——采集中绝不允许停止按钮消失（踩过）

        private void OnEvent(PdlEvent e)
        {
            _recorder?.AppendEvent(e.Code, e.Ch, e.Param, e.TMs);
            // 触发模式：全程历史只在触发窗口内记录（事件先于块到达，门控时序正确）
            if (Acquire.ModeIndex == 2)
            {
                if (e.Code == Pdl.Ev.TrigStarted) Service.GateFullHistory = true;
                else if (e.Code == Pdl.Ev.TrigStopped) Service.GateFullHistory = false;
            }
            if (e.Code == Pdl.Ev.AutoStop && IsAcquiring)
                FinalizeRecorder("定时到点自动结束");
            string name = e.Code switch
            {
                Pdl.Ev.TrigArmed => "TRIG_ARMED",
                Pdl.Ev.TrigStarted => "TRIG_STARTED",
                Pdl.Ev.TrigStopped => "TRIG_STOPPED",
                Pdl.Ev.AutoStop => "AUTO_STOP",
                Pdl.Ev.Aborted => "ABORTED",
                Pdl.Ev.Overrun => "OVERRUN",
                Pdl.Ev.Saturate => "SATURATE",
                Pdl.Ev.TempLost => "TEMP_LOST",
                Pdl.Ev.Overtemp => "OVERTEMP",
                _ => $"EVENT 0x{e.Code:X2}",
            };
            Log.Add("EVT", $"{name} ch={e.Ch} param={e.Param} t={e.TMs}ms");

            if (e.Code == Pdl.Ev.TrigStarted)
                Dispatcher.UIThread.Post(() => { IsAcquiring = true; StatusText = "触发命中 · 采集中"; });
            if (e.Code == Pdl.Ev.TrigStopped || e.Code == Pdl.Ev.AutoStop)
                Dispatcher.UIThread.Post(() =>
                {
                    IsAcquiring = false;
                    StatusText = "已连接 · 空闲（采集已结束）";
                    AcquireCommand.NotifyCanExecuteChanged();
                    AbortCommand.NotifyCanExecuteChanged();
                });
        }

        // ------------------------------------------------------------ 多窗口
        [RelayCommand]
        private void OpenChartWindow()
        {
            if (_chartWindow != null) { _chartWindow.Activate(); return; }
            _chartWindow = new ChartWindow { DataContext = this };
            _chartWindow.Closed += (_, _) => _chartWindow = null;
            _chartWindow.Show();
        }

        [RelayCommand]
        private void OpenLogWindow()
        {
            if (_logWindow != null) { _logWindow.Activate(); return; }
            _logWindow = new LogWindow { DataContext = this };
            _logWindow.Closed += (_, _) => _logWindow = null;
            _logWindow.Show();
        }

        // 温度曲线窗口：TH0/TH1 两路同图，与温度条迷你折线共用同一份历史
        [RelayCommand]
        private void OpenTempWindow()
        {
            if (_tempWindow != null) { _tempWindow.Activate(); return; }
            _tempWindow = new TempWindow
            {
                DataContext = this,
                Position = new PixelPoint(320, 540),
            };
            _tempWindow.Closed += (_, _) => _tempWindow = null;
            _tempWindow.Show();
        }

        // ------------------------------------------------------------ .pdl 会话记录
        void StartRecorder(AcquireParams p)
        {
            _recorder?.Discard();
            _recorder = new PdlFileWriter();
            var h = new PdlFileHeader
            {
                StartedUtc = DateTime.UtcNow,
                SampleRateHz = p.SampleRateHz,
                ReportIntervalMs = p.ReportIntervalMs,
                BinMs = p.BinMs,
                Tier = p.Tier,
                RecordMode = p.RecordMode,
                DeviceInfo = DeviceInfo,
                Power = Service.Power.Where(x => x != null).Select(x => x!).ToList(),
            };
            try
            {
                _recorder.Start(PdlFileWriter.NewSpoolPath(), h);
                _spoolSaved = false;
                UnsavedSpool = true;
                SpoolText = "会话记录中";
                Log.Add("INFO", "会话 spool：" + _recorder.FilePath);
            }
            catch (Exception ex)
            {
                Log.Add("ERR", "spool 创建失败（不影响实时显示）：" + ex.Message);
                _recorder = null;
            }
        }

        void FinalizeRecorder(string why)
        {
            var r = _recorder;
            if (r == null) return;
            if (r.Active)
            {
                _lastSummary = r.Finalize();
                SpoolText = $"会话已收尾（{why}）：{_lastSummary.BlockCount} 块 / " +
                            $"{_lastSummary.DurationMs / 1000.0:F1} s —— 未保存";
                Log.Add("INFO", "spool 收尾完成：" + r.FilePath);
            }
            UnsavedSpool = !_spoolSaved && r.FilePath != "" && File.Exists(r.FilePath);
        }

        [RelayCommand]
        private async Task SaveSessionAsync()
        {
            var r = _recorder;
            if (r == null || _spoolSaved) return;
            FinalizeRecorder("保存前");
            if (!File.Exists(r.FilePath)) { Log.Add("ERR", "spool 文件不存在"); return; }
            var dlg = new SaveFileDialog
            {
                Title = "保存 .pdl 记录文件",
                DefaultExtension = "pdl",
                InitialFileName = $"PDL_{DateTime.Now:yyyyMMdd_HHmmss}.pdl",
                Filters = { new FileDialogFilter { Name = "PDL 记录", Extensions = { "pdl" } } },
            };
            var path = Owner != null ? await dlg.ShowAsync(Owner) : null;
            if (string.IsNullOrWhiteSpace(path)) return;
            try
            {
                File.Copy(r.FilePath, path, overwrite: false);
                File.Delete(r.FilePath);
                _spoolSaved = true;
                UnsavedSpool = false;
                SpoolText = "已保存：" + path;
                Log.Add("INFO", "会话已保存：" + path);
            }
            catch (Exception ex)
            {
                Log.Add("ERR", "保存失败：" + ex.Message);
            }
        }

        [RelayCommand]
        private void DiscardSession()
        {
            var r = _recorder;
            if (r == null || _spoolSaved) return;
            r.Discard();
            UnsavedSpool = false;
            SpoolText = "会话已丢弃";
            Log.Add("INFO", "spool 已丢弃（未保存的数据不会恢复）");
        }

        /// <summary>退出前调用：没收尾的收尾，没保存的删除。</summary>
        public void Shutdown()
        {
            var r = _recorder;
            if (r == null) return;
            if (r.Active) r.Finalize();
            if (!_spoolSaved) r.Discard();
        }

        // ------------------------------------------------------------ 查看器（实时 + 文件，FL 式导航）
        [RelayCommand]
        private void OpenViewerWindow()
        {
            if (_viewerWindow != null) { _viewerWindow.Activate(); return; }
            var vm = new ViewerViewModel();
            if (_session != null)
                vm.AttachLive(Service);          // 连接着 = 实时模式，边采边看
            _viewerWindow = new ViewerWindow { DataContext = vm };
            _viewerWindow.Closed += (_, _) => _viewerWindow = null;
            _viewerWindow.Show();
        }

        // ------------------------------------------------------------ 回放
        [RelayCommand]
        private void OpenPlaybackWindow()
        {
            if (_playbackWindow != null) { _playbackWindow.Activate(); return; }
            _playbackWindow = new PlaybackWindow { DataContext = new PlaybackViewModel() };
            _playbackWindow.Closed += (_, _) => _playbackWindow = null;
            _playbackWindow.Show();
        }

        /// <summary>--demo-pdl：收尾 → 保存到固定路径 → 直接打开回放。</summary>
        public async Task DemoPdlAsync()
        {
            await Task.Delay(100);
            // 压测 UI 路径：弹窗 + 全程模式渲染
            PopChannelCommand.Execute(ChannelCharts[0]);
            OpenTempWindowCommand.Execute(null);
            await Task.Delay(600);
            foreach (var cc in ChannelCharts) cc.FullTimeMode = true;
            await Task.Delay(1200);
            foreach (var cc in ChannelCharts) cc.FullTimeMode = false;
            await AbortAsync();
            await Task.Delay(300);
            FinalizeRecorder("演示保存");
            var r = _recorder;
            if (r == null || !File.Exists(r.FilePath)) return;
            var path = Path.Combine(Path.GetTempPath(), "PDL_demo.pdl");
            File.Copy(r.FilePath, path, overwrite: true);
            _spoolSaved = true;
            UnsavedSpool = false;
            SpoolText = "已保存：" + path;
            Log.Add("INFO", "演示会话已保存：" + path);
            OpenPlaybackWindow();
            await Task.Delay(300);
            if (_playbackWindow?.DataContext is PlaybackViewModel pv)
            {
                pv.LoadFile(path);
                // 演示视窗缩放：1.5 s 后自动缩放到中段
                await Task.Delay(1500);
                double t = pv.TotalMs;
                if (t > 1000)
                {
                    pv.ViewStartMs = t * 0.10;
                    pv.ViewEndMs = t * 0.55;
                }
                await Task.Delay(2000);
                _playbackWindow?.Close();   // 演示完关掉，露出主窗口（验证按钮恢复可用）
            }
        }

        // ---------------------------------------------- 通道曲线：弹出 / 停靠
        private void PopChannelCore(int idx)
        {
            var c = ChannelCharts[idx];
            if (_channelWindows[idx] != null) { _channelWindows[idx].Activate(); return; }
            c.IsPopped = true;
            var w = new ChannelWindow
            {
                DataContext = c,
                Title = $"PDL · {c.Label} 曲线（独立窗口，与主界面共享缓冲）",
                Width = 780, Height = 440,
            };
            // 级联偏移，避免所有弹出窗都叠在同一个位置
            w.Position = new PixelPoint(160 + idx * 64, 160 + idx * 64);
            w.Closed += (_, _) => { _channelWindows[idx] = null; c.IsPopped = false; };
            _channelWindows[idx] = w;
            w.Show();
        }

        private void DockChannelCore(int idx)
        {
            _channelWindows[idx]?.Close();
            _channelWindows[idx] = null;
            ChannelCharts[idx].IsPopped = false;
        }

        [RelayCommand]
        private void PopChannel(object? o)
        {
            if (o is ChannelChartViewModel c) PopChannelCore(c.Index);
        }

        [RelayCommand]
        private void PopAllChannels()
        {
            for (int i = 0; i < 4; i++) PopChannelCore(i);
        }

        [RelayCommand]
        private void DockAllChannels()
        {
            for (int i = 0; i < 4; i++) DockChannelCore(i);
        }

        // 配置弹窗入口（顶栏/左栏按钮）
        [RelayCommand]
        private void OpenTriggerDialog() => RequestConfigDialog?.Invoke("trigger");

        [RelayCommand]
        private void OpenTimingDialog() => RequestConfigDialog?.Invoke("timing");

        [RelayCommand]
        private void ClearLog() => Log.Clear();

        // ------------------------------------------------------------ UI 定时刷新
        public void StartUiTimer()
        {
            _uiTimer = new DispatcherTimer(TimeSpan.FromMilliseconds(33),
                DispatcherPriority.Background, OnUiTick);
            _uiTimer.Start();
        }

        private void OnUiTick(object? sender, EventArgs e)
        {
            if (!IsConnected || Service == null) return;

            for (int i = 0; i < 4; i++)
            {
                var live = Service.Live[i];
                var vm = Channels[i];
                var cfg = Service.Power[i];
                vm.Voltage = live.VMv / 1000.0;
                vm.Current = live.IMa / 1000.0;
                vm.Power = live.PW;
                var (vpp, vmin, vmax, vavg) = live.Window();
                vm.Vpp = vpp;
                vm.Vmin = vmin / 1000.0;
                vm.Vmax = vmax / 1000.0;
                vm.RipplePct = Math.Abs(vavg) > 1e-9 ? vpp / Math.Abs(vavg) * 100.0 : 0;
                var (pavg, _) = live.PowerStats();
                vm.Pavg = pavg;
                vm.Ratio = cfg == null || cfg.FullScaleV <= 0 ? 0 : Math.Min(1.0, Math.Abs(vm.Voltage) / cfg.FullScaleV);
                vm.Saturated = vm.Ratio >= 0.999;
                ChannelCharts[i].LastVolt = vm.Voltage;
                ChannelCharts[i].LastAmp = vm.Current;
            }

            for (int i = 0; i < 2; i++)
            {
                Temps[i].Celsius = Service.Temp[i].Celsius;
                Temps[i].Present = Service.Temp[i].IsPresent;
            }

            LinkText = $"tick {Service.TotalTicks}   块 {Service.TotalBlocks}   丢点 {Service.Gaps}" +
                       (_session != null ? $"   RTT {_session.RttMs:F1} ms" : "");
        }
    }
}
