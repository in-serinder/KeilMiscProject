using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using PWMCtrol.Models;

namespace PWMCtrol.ViewModels;

/// <summary>主窗口：连接管理、模式权限、命令下发、ECHO 分发与日志。</summary>
public partial class MainWindowViewModel : ViewModelBase, IChannelHost
{
    private const int MaxLogEntries = 400;

    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    private static readonly Dictionary<string, string> ErrorText = new()
    {
        ["E01"] = "通道号非法",
        ["E02"] = "频率越界",
        ["E03"] = "占空比越界",
        ["E04"] = "持续时间非法",
        ["E05"] = "ADC 参数非法",
        ["E06"] = "字段数量错误",
        ["E07"] = "未知命令 / 帧超长",
        ["E08"] = "预制程序号非法",
        ["E09"] = "通道故障锁存，请先停止",
        ["E11"] = "存储失败",
        ["E12"] = "该频率下分辨率不足",
        ["E13"] = "设备当前不在上位机模式",
    };

    private readonly DeviceSession _session = new();
    private int _rxCount;
    private int _txCount;

    public MainWindowViewModel()
    {
        for (var i = 0; i < 3; i++)
        {
            Channels.Add(new ChannelViewModel(i, this));
        }

        _session.FrameReceived += OnFrameReceived;
        _session.EchoReceived += OnEchoReceived;
        _session.ConnectionChanged += OnConnectionChanged;

        RefreshPorts();
        AddLog(LogKind.Info, "就绪：选择串口 → 连接 → 设备识别");
    }

    public ObservableCollection<ChannelViewModel> Channels { get; } = new();

    public ObservableCollection<string> Ports { get; } = new();

    public ObservableCollection<LogEntry> Logs { get; } = new();

    [ObservableProperty]
    private string? _selectedPort;

    [ObservableProperty]
    private bool _isConnected;

    [ObservableProperty]
    private bool _isIdentified;

    [ObservableProperty]
    private string _linkText = "未连接";

    [ObservableProperty]
    private int _mode = 3;

    [ObservableProperty]
    private string _deviceInfo = "型号 —　固件 —　主频 —";

    [ObservableProperty]
    private string _counterText = "RX 0 / TX 0";

    [ObservableProperty]
    private int _probeMillivolts;

    [ObservableProperty]
    private string _probeChars = "  0";

    [ObservableProperty]
    private string _probeDots = "000";

    [ObservableProperty]
    private string _probeText = "0 mV / 0.00 V";

    public bool CanControl => IsConnected && IsIdentified && Mode == 3;

    public string ModeHint => CanControl
        ? "上位机模式：命令生效"
        : "当前非上位机模式，参数只能看不能改（或未连接）";

    public string ConnectButtonText => IsConnected ? "断开" : "连接";

    public Avalonia.Media.IBrush LinkBrush => IsConnected
        ? new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#4EC97A"))
        : new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#E06A6A"));

    partial void OnIsConnectedChanged(bool value) => RaiseGateChanged();

    partial void OnIsIdentifiedChanged(bool value) => RaiseGateChanged();

    partial void OnModeChanged(int value) => RaiseGateChanged();

    private void RaiseGateChanged()
    {
        OnPropertyChanged(nameof(CanControl));
        OnPropertyChanged(nameof(ModeHint));
        OnPropertyChanged(nameof(ConnectButtonText));
        OnPropertyChanged(nameof(LinkBrush));
    }

    // ------------------------------------------------------------ 电压探头

    partial void OnProbeMillivoltsChanged(int value)
    {
        var clamped = Math.Clamp(value, 0, 5000);
        var centivolts = Math.Min(999, (int)Math.Round(clamped / 10.0));
        ProbeChars = centivolts.ToString("000", Inv);
        ProbeDots = "100";                                  // 小数点在第 1 位后 → 3.32
        ProbeText = $"{clamped} mV / {clamped / 1000.0:0.00} V";
    }

    /// <summary>按需读一次探头（板载只有一路 ADC，走 ADCREAD_0）。</summary>
    [RelayCommand]
    private async Task ReadProbeAsync()
    {
        if (!IsConnected)
        {
            AddLog(LogKind.Warn, "未连接：无法读取电压探头");
            return;
        }

        var reply = await SendAsync("ADCREAD_0");
        if (reply is null || reply.StartsWith("ERR_", StringComparison.Ordinal))
        {
            LogReply("ADCREAD_0", reply);
            return;
        }

        // ADC_0_<raw>_<mV>
        var parts = reply.Split('_');
        if (parts.Length >= 4 && int.TryParse(parts[3], out var mv))
        {
            ProbeMillivolts = mv;
            AddLog(LogKind.Rx, $"电压探头 {mv} mV");
        }
    }

    // ---------------------------------------------------------------- 连接

    [RelayCommand]
    private void RefreshPorts()
    {
        var current = SelectedPort;
        Ports.Clear();
        foreach (var port in SerialTransport.AvailablePorts())
        {
            Ports.Add(port);
        }

        if (Ports.Count == 0)
        {
            AddLog(LogKind.Warn, "未自动发现串口，可直接在端口框里手写（如 COM5）");
            return;
        }

        SelectedPort = Ports.Contains(current ?? string.Empty) ? current : Ports[0];
        AddLog(LogKind.Info, $"发现 {Ports.Count} 个串口");
    }

    [RelayCommand]
    private async Task ConnectAsync()
    {
        if (IsConnected)
        {
            Disconnect();
            return;
        }

        if (string.IsNullOrWhiteSpace(SelectedPort))
        {
            AddLog(LogKind.Error, "请先选择串口");
            return;
        }

        if (!_session.Open(SelectedPort))
        {
            AddLog(LogKind.Error, $"打开 {SelectedPort} 失败");
            return;
        }

        IsConnected = true;
        LinkText = SelectedPort;

        var ok = await _session.IdentifyAsync();
        IsIdentified = ok;
        if (!ok)
        {
            AddLog(LogKind.Error, "未收到 [YES]：检查端口是否被占用、波特率 115200、设备是否在线");
            return;
        }

        AddLog(LogKind.Info, "设备识别成功：TYPE-PWMCTROL");

        var ver = await SendAsync("VER");
        var info = await SendAsync("INFO");
        DeviceInfo = BuildDeviceInfo(ver, info);

        var mode = await SendAsync("MODE?");
        if (mode is not null && mode.StartsWith("MODE_", StringComparison.Ordinal)
            && int.TryParse(mode.AsSpan(5), out var m) && m is >= 1 and <= 3)
        {
            Mode = m;
        }

        await SendAsync("ECHO_1");
        await SendAsync("RATE_50");
    }

    [RelayCommand]
    private void Disconnect()
    {
        _session.Close();
        IsConnected = false;
        IsIdentified = false;
        LinkText = "未连接";
        AddLog(LogKind.Info, "已断开串口");
    }

    private static string BuildDeviceInfo(string? ver, string? info)
    {
        var version = ver is not null && ver.StartsWith("VER_", StringComparison.Ordinal)
            ? ver[4..]
            : "—";

        var fosc = "—";
        if (info is not null)
        {
            var parts = info.Split('_');
            if (parts.Length >= 5)
            {
                fosc = (parts[4].Length > 3 ? parts[4][..3] + "." + parts[4][3..] : parts[4]) + " MHz";
            }
        }

        return $"型号 PWMCTROL　固件 {version}　主频 {fosc}";
    }

    // ---------------------------------------------------------------- 命令

    [RelayCommand]
    private async Task StopAllAsync()
    {
        if (!IsConnected)
        {
            return;
        }

        var reply = await SendAsync("STOP");
        if (reply == "OK_STOP")
        {
            AddLog(LogKind.Info, "全部通道已停止");
            foreach (var channel in Channels)
            {
                channel.IsRunning = false;
                channel.StateText = "停止";
            }
        }
        else
        {
            LogReply("STOP", reply);
        }
    }

    [RelayCommand]
    private async Task SetModeAsync(string? mode)
    {
        if (!IsConnected || !int.TryParse(mode, out var target) || target is < 1 or > 3)
        {
            return;
        }

        var reply = await SendAsync($"MODE_{target}");
        if (reply == $"OK_MODE_{target}")
        {
            Mode = target;
            AddLog(LogKind.Info, $"已切换到 Mode{target}");
        }
        else
        {
            LogReply($"MODE_{target}", reply);
        }
    }

    [RelayCommand]
    private void ClearLog() => Logs.Clear();

    /// <summary>界面预览：只填本地示例值，用于看设计稿，不碰串口。</summary>
    public void LoadUiDemo()
    {
        var presets = new (double Freq, double Duty)[]
        {
            (1500, 50.0),
            (20000, 62.0),
            (480, 88.0),
        };

        for (var i = 0; i < Channels.Count && i < presets.Length; i++)
        {
            var channel = Channels[i];
            channel.Frequency = presets[i].Freq;
            channel.Duty = presets[i].Duty;
            channel.ActualFrequency = presets[i].Freq;
            channel.ActualDuty = presets[i].Duty;
            channel.IsRunning = true;
            channel.StateText = "运行";
            channel.AdcEnabled = i == 0;
        }

        ProbeMillivolts = 3320;
        DeviceInfo = "型号 PWMCTROL　固件 1.1.0　主频 22.118 MHz（界面预览，未连接）";
        AddLog(LogKind.Info, "界面预览模式：数值为示例，未打开串口");
    }

    /// <summary>联调演示：切到 Mode3 后下发三组典型参数，用于验证 ECHO 回显链路。</summary>
    public async Task RunDemoAsync()
    {
        if (!IsConnected)
        {
            return;
        }

        await SetModeAsync("3");

        var presets = new (double Freq, double Duty)[]
        {
            (1500, 50.0),
            (20000, 62.0),
            (480, 88.0),
        };

        for (var i = 0; i < Channels.Count && i < presets.Length; i++)
        {
            Channels[i].Frequency = presets[i].Freq;
            Channels[i].Duty = presets[i].Duty;
            Channels[i].AdcEnabled = i == 0;
            await ApplyAsync(Channels[i]);
        }
    }

    public async Task ApplyAsync(ChannelViewModel channel)
    {
        if (!CanControl)
        {
            AddLog(LogKind.Warn, "设备不在上位机模式，命令未下发");
            return;
        }

        var frequency = channel.Frequency.ToString("0.###", Inv);
        var duty = channel.Duty.ToString("0.0", Inv);
        var duration = channel.Forever ? "F" : channel.DurationMs.ToString(Inv);
        var adc = channel.AdcEnabled ? "1" : "0";
        var body = $"CONFIG_{channel.Index}_{frequency}_{duty}_{duration}_{adc}";

        var reply = await SendAsync(body);
        if (reply is null)
        {
            channel.MarkTimedOut();
            LogReply(body, null);
            return;
        }

        if (reply.StartsWith("OK_", StringComparison.Ordinal))
        {
            AddLog(LogKind.Info, $"CH{channel.Index} 生效：{reply}");
            ApplyActualFromReply(channel, reply);
        }
        else
        {
            LogReply(body, reply);
        }
    }

    public async Task StopAsync(ChannelViewModel channel)
    {
        if (!IsConnected)
        {
            return;
        }

        var body = $"STOP_{channel.Index}";
        var reply = await SendAsync(body);
        if (reply == $"OK_STOP_{channel.Index}")
        {
            channel.IsRunning = false;
            channel.StateText = "停止";
            AddLog(LogKind.Info, $"CH{channel.Index} 已停止");
        }
        else
        {
            LogReply(body, reply);
        }
    }

    /// <summary>OK_0_1000.02_50.0_1 → 取中间两段作为生效值。</summary>
    private static void ApplyActualFromReply(ChannelViewModel channel, string reply)
    {
        var parts = reply.Split('_');
        if (parts.Length < 4)
        {
            return;
        }

        if (double.TryParse(parts[2], NumberStyles.Float, Inv, out var freq))
        {
            channel.ActualFrequency = freq;
        }

        if (double.TryParse(parts[3], NumberStyles.Float, Inv, out var duty))
        {
            channel.ActualDuty = duty;
        }
    }

    private void LogReply(string command, string? reply)
    {
        if (reply is null)
        {
            AddLog(LogKind.Error, $"{command} → 超时无应答");
            return;
        }

        if (reply.StartsWith("ERR_", StringComparison.Ordinal))
        {
            var code = reply.Split('_').ElementAtOrDefault(1) ?? "?";
            var text = ErrorText.TryGetValue(code, out var desc) ? desc : "未知错误";
            AddLog(LogKind.Error, $"{command} → [{reply}] {text}");
            return;
        }

        AddLog(LogKind.Rx, $"{command} → [{reply}]");
    }

    // ---------------------------------------------------------------- 收帧

    /// <summary>所有下行命令都走这里，顺便统计发送数。</summary>
    private async Task<string?> SendAsync(string body)
    {
        _txCount++;
        CounterText = $"RX {_rxCount} / TX {_txCount}";
        return await _session.SendAsync(body);
    }

    private void OnFrameReceived(string body)
    {
        // ECHO 是 20Hz 周期流，只计数、不刷 UI，否则每帧都要重建字符串
        if (body.StartsWith("ECHO_", StringComparison.Ordinal))
        {
            return;
        }

        Dispatcher.UIThread.Post(() =>
        {
            _rxCount++;
            CounterText = $"RX {_rxCount} / TX {_txCount}";
            if (body.StartsWith("EVT_", StringComparison.Ordinal))
            {
                AddLog(LogKind.Warn, $"事件 [{body}]");
            }
        });
    }

    private void OnEchoReceived(ChannelEcho echo)
    {
        Dispatcher.UIThread.Post(() =>
        {
            if (echo.Index >= 0 && echo.Index < Channels.Count)
            {
                Channels[echo.Index].ApplyEcho(echo);
            }

            // 板载只有一路探头：任何通道带来的读数都刷到同一块电压表
            if (echo.Millivolts.HasValue)
            {
                ProbeMillivolts = echo.Millivolts.Value;
            }
        });
    }

    private void OnConnectionChanged(bool open, string message)
    {
        Dispatcher.UIThread.Post(() =>
        {
            if (!open)
            {
                IsConnected = false;
                IsIdentified = false;
                LinkText = "未连接";
            }

            AddLog(open ? LogKind.Info : LogKind.Warn, $"串口：{message}");
        });
    }

    private void AddLog(LogKind kind, string text)
    {
        Logs.Add(LogEntry.Create(kind, text));
        while (Logs.Count > MaxLogEntries)
        {
            Logs.RemoveAt(0);
        }
    }
}
