using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
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

        /// <summary>
        /// 端口输入框的文本。既能是下拉里选中的端口，也能是用户手敲的
        /// <c>/dev/ttyUSB0</c>、<c>/dev/serial/by-id/…</c>、<c>COM10</c>。
        ///
        /// 为什么需要它：枚举在两个平台上都不是"全集" ——
        /// Linux 会给不出 socat 虚拟口、容器里的设备、自定义 udev 名；
        /// Windows 的 COM 号也可能因为驱动问题漏掉。没有手动输入就无法兜底。
        /// </summary>
        [ObservableProperty]
        private string _portText = string.Empty;

        /// <summary>手动输入过的端口名：刷新列表后要能留下来，不能一刷新就消失。</summary>
        private string? _manualPortName;

        partial void OnSelectedPortChanged(SerialPortDescriptor? value)
        {
            if (value is not null)
            {
                PortText = value.PortName;
            }
        }

        /// <summary>
        /// 把输入框文本解析成端口条目；不在枚举结果里就新建一条（= 手动输入），
        /// 并记进 <see cref="_manualPortName"/> 供下次刷新后恢复。
        /// </summary>
        private SerialPortDescriptor? ResolvePort(string? text)
        {
            var name = (text ?? string.Empty).Trim();
            if (name.Length == 0)
            {
                return null;
            }

            var known = Ports.FirstOrDefault(p => string.Equals(p.PortName, name, StringComparison.OrdinalIgnoreCase));
            if (known is not null)
            {
                return known;
            }

            var created = new SerialPortDescriptor(name, "manual entry");
            Ports.Add(created);
            _manualPortName = created.PortName;
            return created;
        }

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

                // 刷新会清空列表：先记住当前指向谁，重新枚举后再按名字找回来
                var previous = !string.IsNullOrWhiteSpace(PortText) ? PortText.Trim() : SelectedPort?.PortName;
                SelectedPort = null;

                Ports.Clear();
                foreach (var port in ports)
                {
                    Ports.Add(port);
                }

                // 手敲过的端口补回列表 —— 否则用户输一次、刷新一次就丢一次
                if (!string.IsNullOrWhiteSpace(_manualPortName) &&
                    !Ports.Any(p => string.Equals(p.PortName, _manualPortName, StringComparison.OrdinalIgnoreCase)))
                {
                    Ports.Add(new SerialPortDescriptor(_manualPortName!, "manual entry"));
                }

                OnPropertyChanged(nameof(HasPorts));

                if (Ports.Count == 0)
                {
                    StatusText = "No serial ports found — type a port name to connect manually";
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
                }

                var restored = Ports.FirstOrDefault(p =>
                    string.Equals(p.PortName, previous, StringComparison.OrdinalIgnoreCase));

                if (restored is not null)
                {
                    SelectedPort = restored;
                }
                else
                {
                    // 名字枚举不到（手敲的 pts / 已拔掉的设备）：选中项退回一个真实端口，
                    // 但**输入框里的文本要留住** —— 用户刚敲的东西不能被刷新吃掉
                    SelectedPort = Ports.FirstOrDefault(p => p.IsCapsa) ?? Ports.FirstOrDefault();

                    if (!string.IsNullOrWhiteSpace(previous))
                    {
                        PortText = previous;
                    }
                }

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

                // 输入框里的文本优先：用户可能手敲了一个枚举不到的端口（Linux by-id / 虚拟口）
                var port = ResolvePort(PortText) ?? SelectedPort;
                if (port is null)
                {
                    StatusText = "Select or type a port first";
                    return;
                }

                SelectedPort = port;

                // 手动选择 / 手敲但尚未探测过：先做一次短测，失败弹窗提示
                if (!port.IsCapsa)
                {
                    StatusText = $"Verifying {port.PortName}…";
                    var verified = await _serial.ProbeAsync(port.PortName).ConfigureAwait(true);

                    if (!verified)
                    {
                        StatusText = "Port verification failed";
                        await AppServices.Messages.ShowErrorAsync(
                            "No answer from device",
                            BuildNoAnswerReport(port.PortName, _serial.LastProbeError)).ConfigureAwait(true);
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

        /// <summary>
        /// 探测失败时的排查清单。**必须按平台给**：Windows 的串口是独占的，
        /// 十有八九是串口助手开着；Linux 上最常见的是当前用户不在串口组
        /// （Debian 叫 dialout、Arch 叫 uucp，运行时探测），
        /// 以及 ModemManager 正在探测同一路 USB 串口（会把首帧吃掉）。
        /// </summary>
        private static string BuildNoAnswerReport(string portName, string? reason)
        {
            var text = new StringBuilder();
            text.Append(portName).Append(" 没有回应 TYPE-CAPASA。").Append(Environment.NewLine).Append(Environment.NewLine);

            if (!string.IsNullOrWhiteSpace(reason))
            {
                text.Append("原因：").Append(reason).Append(Environment.NewLine).Append(Environment.NewLine);
            }

            text.Append("请依次检查：").Append(Environment.NewLine);

            if (OperatingSystem.IsWindows())
            {
                text.Append("  • 串口助手 / PuTTY 等是否还占着这个端口（必须先关闭）；").Append(Environment.NewLine);
            }
            else
            {
                text.Append(UnixSerialGroup.BuildCheckLine()).Append(Environment.NewLine);
                text.Append("  • 是否有 minicom / screen / 另一个 Capsa 实例在收同一个端口；").Append(Environment.NewLine);
                text.Append("  • ModemManager 是否在探测该设备（udev 规则加 ENV{ID_MM_DEVICE_IGNORE}=\"1\"）；").Append(Environment.NewLine);
            }

            text.Append("  • USB-TTL 的 TX/RX 是否与板子交叉、GND 是否共地；").Append(Environment.NewLine);
            text.Append("  • 固件是否已烧录且正常运行（EA 打开后才有应答）；").Append(Environment.NewLine);
            text.Append("  • STC-ISP 下载时 IRC 是否为 22.1184MHz。");

            return text.ToString();
        }
    }
}
