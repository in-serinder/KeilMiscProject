using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using Capsa.Models;
using Capsa.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Capsa.ViewModels.Shell
{
    /// <summary>顶部栏：品牌 + 串口选择（自动探测 Capsa 设备并打星号）+ 连接状态。</summary>
    public sealed partial class TopBarViewModel : ViewModelBase
    {
        private readonly ISerialPortService _serial;

        public TopBarViewModel(ISerialPortService serial)
        {
            _serial = serial;

            // 串口服务的心跳检测到掉线（USB 拔出 / 无应答）会发这个事件：
            // 顶栏切回"未连接"，并通知主界面复位设备面板与卡片。
            _serial.ConnectionChanged += OnSerialConnectionChanged;
        }

        private void OnSerialConnectionChanged(object? sender, SerialConnectionChangedEventArgs e)
        {
            if (e.IsConnected || !IsConnected)
            {
                return;      // 只处理"已连接 -> 掉线"这一种跳变
            }

            IsConnected = false;
            StatusText = string.IsNullOrWhiteSpace(e.Reason)
                ? $"Disconnected · {e.PortName}"
                : $"Device lost ({e.Reason}) · {e.PortName}";

            ConnectedChanged?.Invoke(this, false);
        }

        public ObservableCollection<SerialPortDescriptor> Ports { get; } = new();

        [ObservableProperty]
        private SerialPortDescriptor? _selectedPort;

        [ObservableProperty]
        private bool _isConnected;

        [ObservableProperty]
        private bool _isBusy;

        [ObservableProperty]
        private string _statusText = "Not connected";

        /// <summary>连接状态变化（仅在真正发生切换时触发）。</summary>
        public event EventHandler<bool>? ConnectedChanged;

        public bool HasPorts => Ports.Count > 0;

        /// <summary>全局错误日志条数（>0 时顶栏显示 Logs 按钮）。</summary>
        public int ErrorCount => Capsa.Diagnostics.ErrorLog.Entries.Count;

        /// <summary>随时可打开全部错误日志（可复制）。错误弹窗关闭后还能从这里找回。</summary>
        [RelayCommand]
        private async Task ShowLogsAsync()
        {
            var window = Capsa.Views.Dialogs.ErrorDetailsWindow.ForFullLog();
            var lifetime = Avalonia.Application.Current?.ApplicationLifetime
                as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime;

            if (lifetime?.MainWindow is { } owner)
            {
                await window.ShowDialog(owner).ConfigureAwait(true);
            }
            else
            {
                window.Show();
            }

            OnPropertyChanged(nameof(ErrorCount));
            OnPropertyChanged(nameof(HasErrors));
        }

        public bool HasErrors => ErrorCount > 0;

        // ------------------------------------------------------------------ //
        // 刷新：枚举 + 逐口探测
        // ------------------------------------------------------------------ //

        [RelayCommand]
        public async Task RefreshPortsAsync()
        {
            if (IsConnected || IsBusy)
            {
                return;
            }

            IsBusy = true;
            StatusText = "Scanning serial ports…";

            try
            {
                var ports = await _serial.EnumeratePortsAsync().ConfigureAwait(true);

                Ports.Clear();
                foreach (var port in ports)
                {
                    Ports.Add(port);
                }

                OnPropertyChanged(nameof(HasPorts));

                if (Ports.Count == 0)
                {
                    StatusText = "No serial ports found";
                    return;
                }

                var found = 0;
                string? firstError = null;

                foreach (var port in Ports)
                {
                    StatusText = $"Probing {port.PortName}…";
                    port.IsCapsa = await _serial.ProbeAsync(port.PortName).ConfigureAwait(true);

                    if (!port.IsCapsa)
                    {
                        firstError ??= _serial.LastProbeError;
                        continue;
                    }

                    found++;
                    SelectedPort ??= port;
                }

                SelectedPort ??= Ports.FirstOrDefault();

                StatusText = found > 0
                    ? $"Found {found} Capsa device(s)"
                    : (firstError is null ? "No Capsa device detected" : $"No device: {firstError}");
            }
            catch (Exception ex)
            {
                StatusText = $"Scan failed: {ex.Message}";
            }
            finally
            {
                IsBusy = false;
            }
        }

        // ------------------------------------------------------------------ //
        // 连接 / 断开
        // ------------------------------------------------------------------ //

        [RelayCommand]
        private async Task ToggleConnectionAsync()
        {
            if (IsBusy)
            {
                return;
            }

            IsBusy = true;

            try
            {
                if (IsConnected)
                {
                    await _serial.DisconnectAsync().ConfigureAwait(true);
                    IsConnected = false;
                    StatusText = "Not connected";
                    ConnectedChanged?.Invoke(this, false);
                    return;
                }

                if (SelectedPort is null)
                {
                    StatusText = "Select a port first";
                    return;
                }

                var port = SelectedPort;

                // 手动选择但尚未探测过：先做一次短测，失败弹窗提示
                if (!port.IsCapsa)
                {
                    StatusText = $"Verifying {port.PortName}…";
                    var verified = await _serial.ProbeAsync(port.PortName).ConfigureAwait(true);

                    if (!verified)
                    {
                        StatusText = "Port verification failed";
                        var reason = _serial.LastProbeError;
                        await AppServices.Messages.ShowErrorAsync(
                            "No answer from device",
                            $"{port.PortName} 没有回应 TYPE-CAPASA。\r\n\r\n" +
                            (string.IsNullOrWhiteSpace(reason) ? string.Empty : $"原因：{reason}\r\n\r\n") +
                            "请依次检查：\r\n" +
                            "  • 串口助手 / PuTTY 等是否还占着这个端口（必须先关闭）；\r\n" +
                            "  • USB-TTL 的 TX/RX 是否与板子交叉、GND 是否共地；\r\n" +
                            "  • 固件是否已烧录且正常运行（EA 打开后才有应答）；\r\n" +
                            "  • STC-ISP 下载时 IRC 是否为 22.1184MHz。").ConfigureAwait(true);
                        return;
                    }

                    port.IsCapsa = true;
                }

                StatusText = "Connecting…";
                await _serial.ConnectAsync(port.PortName).ConfigureAwait(true);

                IsConnected = true;
                StatusText = $"Connected {port.PortName}";
                ConnectedChanged?.Invoke(this, true);
            }
            catch (Exception ex)
            {
                IsConnected = false;
                StatusText = "Connection failed";
                await AppServices.Messages.ShowErrorAsync("Connection failed", ex.Message).ConfigureAwait(true);
                ConnectedChanged?.Invoke(this, false);
            }
            finally
            {
                IsBusy = false;
            }
        }
    }
}
