using System;
using System.Globalization;
using System.Threading.Tasks;
using Avalonia.Media;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using PWMCtrol.Models;

namespace PWMCtrol.ViewModels;

/// <summary>通道卡片需要的外部能力，由主窗口实现，避免双向强引用。</summary>
public interface IChannelHost
{
    bool CanControl { get; }

    Task ApplyAsync(ChannelViewModel channel);

    Task StopAsync(ChannelViewModel channel);
}

/// <summary>单个 PWM 通道：本地设定值 + 旋钮步进 + 设备回报的生效值。</summary>
public partial class ChannelViewModel : ViewModelBase
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
    private static readonly string[] AccentHex = { "#3FA9F5", "#4EC97A", "#E8B339" };

    private readonly IChannelHost _host;
    private readonly DispatcherTimer _debounce;
    private bool _applying;

    public ChannelViewModel(int index, IChannelHost host)
    {
        Index = index;
        _host = host;
        Title = $"CH{index}";
        Accent = new SolidColorBrush(Color.Parse(AccentHex[index % AccentHex.Length]));

        _debounce = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(320) };
        _debounce.Tick += async (_, _) =>
        {
            _debounce.Stop();
            await AutoApplyAsync();
        };

        UpdateDisplay();
    }

    public int Index { get; }

    public string Title { get; }

    public IBrush Accent { get; }

    public string EditTargetText => EditDuty ? "占空比" : "频率";

    /// <summary>波形预览用：运行中显示设备生效值，否则显示本地设定值。</summary>
    public double PreviewFrequency => IsRunning && ActualFrequency > 0 ? ActualFrequency : Frequency;

    public double PreviewDuty => IsRunning ? ActualDuty : Duty;

    /// <summary>生效值的一行摘要，频率用 kHz 便于阅读。</summary>
    public string ActualText => !IsRunning
        ? "未运行"
        : (ActualFrequency >= 1000
            ? $"{ActualFrequency / 1000.0:0.00} kHz / {ActualDuty:0.0} %"
            : $"{ActualFrequency:0} Hz / {ActualDuty:0.0} %");

    public string RangeHint => EditDuty
        ? "0.0 ~ 100.0 %（步进 1%）"
        : "1 Hz ~ 999 kHz（步进随量级自适应）";

    [ObservableProperty]
    private double _frequency = 1000;

    [ObservableProperty]
    private double _duty;

    [ObservableProperty]
    private bool _forever = true;

    [ObservableProperty]
    private int _durationMs = 5000;

    [ObservableProperty]
    private bool _adcEnabled;

    [ObservableProperty]
    private bool _isRunning;

    [ObservableProperty]
    private string _stateText = "停止";

    [ObservableProperty]
    private double _actualFrequency;

    [ObservableProperty]
    private double _actualDuty;

    [ObservableProperty]
    private int _millivolts;

    [ObservableProperty]
    private bool _adcAvailable;

    [ObservableProperty]
    private bool _editDuty;

    [ObservableProperty]
    private string _displayChars = "       ";

    [ObservableProperty]
    private string _displayDots = "0000000";

    partial void OnFrequencyChanged(double value) => UpdateDisplay();

    partial void OnDutyChanged(double value) => UpdateDisplay();

    partial void OnActualFrequencyChanged(double value) => UpdateDisplay();

    partial void OnActualDutyChanged(double value) => UpdateDisplay();

    partial void OnIsRunningChanged(bool value) => UpdateDisplay();

    partial void OnEditDutyChanged(bool value)
    {
        OnPropertyChanged(nameof(EditTargetText));
        OnPropertyChanged(nameof(RangeHint));
    }

    /// <summary>旋钮步进：编辑焦点在频率时按量级跳，在占空比时按 1% 跳。</summary>
    [RelayCommand]
    private void Step(int delta)
    {
        if (delta == 0)
        {
            return;
        }

        if (EditDuty)
        {
            Duty = Math.Clamp(Math.Round(Duty + delta, 1), 0, 100);
        }
        else
        {
            var step = FrequencyStep(Frequency);
            Frequency = Math.Clamp(Math.Round(Frequency + delta * step), 1, 999000);
        }

        _debounce.Stop();
        _debounce.Start();
    }

    /// <summary>单击旋钮：在"调频率 / 调占空比"之间切换。</summary>
    [RelayCommand]
    private void ToggleEdit() => EditDuty = !EditDuty;

    [RelayCommand]
    private Task Apply() => _host.ApplyAsync(this);

    [RelayCommand]
    private Task Stop() => _host.StopAsync(this);

    /// <summary>把设备上报的生效值刷到卡片上。</summary>
    public void ApplyEcho(ChannelEcho echo)
    {
        ActualFrequency = echo.Frequency;
        ActualDuty = echo.Duty;
        IsRunning = echo.State == 'R';
        StateText = echo.State switch
        {
            'R' => "运行",
            'E' => "到期",
            'F' => "故障",
            _ => "停止",
        };

        AdcAvailable = echo.Millivolts.HasValue;
        if (echo.Millivolts.HasValue)
        {
            Millivolts = echo.Millivolts.Value;
        }
    }

    public void MarkTimedOut()
    {
        StateText = "无响应";
        IsRunning = false;
    }

    private async Task AutoApplyAsync()
    {
        if (_applying || !_host.CanControl)
        {
            return;
        }

        _applying = true;
        try
        {
            await _host.ApplyAsync(this);
        }
        finally
        {
            _applying = false;
        }
    }

    private static double FrequencyStep(double f) => f switch
    {
        < 100 => 1,
        < 1000 => 10,
        < 10000 => 100,
        < 100000 => 1000,
        _ => 10000,
    };

    /// <summary>按固件规则拼 7 位显示：频率 3 位 + 分隔 + 占空比 3 位。</summary>
    private void UpdateDisplay()
    {
        var freq = IsRunning && ActualFrequency > 0 ? ActualFrequency : Frequency;
        var duty = IsRunning ? ActualDuty : Duty;

        var (freqChars, dot) = FormatFrequency(freq);
        var dutyChars = ((int)Math.Round(Math.Clamp(duty, 0, 100)))
            .ToString(Inv)
            .PadLeft(3);

        var text = freqChars + " " + dutyChars;
        var dots = new char[text.Length];
        Array.Fill(dots, '0');
        if (dot is >= 1 and <= 3)
        {
            dots[dot - 1] = '1';
        }

        DisplayChars = text;
        DisplayDots = new string(dots);
        OnPropertyChanged(nameof(PreviewFrequency));
        OnPropertyChanged(nameof(PreviewDuty));
        OnPropertyChanged(nameof(ActualText));
    }

    /// <summary>与下位机一致：&lt;1000Hz 用整数；&gt;=1000Hz 用 kHz 三位有效数字，小数点位置即单位。</summary>
    private static (string Text, int Dot) FormatFrequency(double f)
    {
        f = Math.Clamp(f, 1, 999000);
        if (f < 999.5)
        {
            return (((int)Math.Round(f)).ToString(Inv).PadLeft(3, ' '), 0);
        }

        var k = f / 1000.0;
        if (k < 9.995)
        {
            var s = k.ToString("0.00", Inv);
            return (s.Replace(".", string.Empty), 1);
        }

        if (k < 99.95)
        {
            var s = k.ToString("0.0", Inv);
            return (s.Replace(".", string.Empty), 2);
        }

        return (((int)Math.Round(k)).ToString(Inv).PadLeft(3, ' '), 3);
    }
}
