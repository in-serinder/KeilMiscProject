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
                foreach (var port in Ports)
                {
                    StatusText = $"Probing {port.PortName}…";
                    port.IsCapsa = await _serial.ProbeAsync(port.PortName).ConfigureAwait(true);

                    if (!port.IsCapsa)
                    {
                        continue;
                    }

                    found++;
                    SelectedPort ??= port;
                }

                SelectedPort ??= Ports.FirstOrDefault();

                StatusText = found > 0
                    ? $"Found {found} Capsa device(s)"
                    : "No Capsa device detected";
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
                        await AppServices.Messages.ShowErrorAsync(
                            "Not a Capsa device",
                            $"{port.PortName} did not answer TYPE-CAPASA.\r\n\r\n" +
                            "Please check:\r\n" +
                            "  • the USB / serial cable and the COM bridge (e.g. COM1 ↔ COM2),\r\n" +
                            "  • the lower-machine simulator is running on the paired port,\r\n" +
                            "  • no other application is holding the port.").ConfigureAwait(true);
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
