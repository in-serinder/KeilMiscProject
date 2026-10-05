using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using _24Cxx_Copier_Porg.Models;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace _24Cxx_Copier_Porg.ViewModels;

/// <summary>
/// View-model for the port-selection window.
///
/// On load (and on Refresh) it enumerates every serial port together with its
/// description, then probes each port in parallel by sending
/// <c>&gt;TYPE_ECHO&lt;\r\n</c> and waiting for <c>&gt;YES&lt;</c>. The port that
/// answers is the target lower device: it is placed first in the list,
/// auto-selected and enables the OK button.
/// </summary>
public partial class MainWindowViewModel : ViewModelBase
{
    private CancellationTokenSource? _scanCts;

    public MainWindowViewModel()
    {
        RefreshCommand = new AsyncRelayCommand(RefreshAsync);
        OkCommand = new AsyncRelayCommand(OkAsync, CanOk);
        CloseCommand = new RelayCommand(Close);

        // Kick off the initial scan as soon as the window is created.
        _ = RefreshAsync();
    }

    // ---- Properties (interface) ----

    /// <summary>Ports shown in the drop-down (target device first).</summary>
    public ObservableCollection<ComPortInfo> AvailablePorts { get; } = new();

    /// <summary>Currently selected port.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(OkCommand))]
    private ComPortInfo? _selectedPort;

    /// <summary>True while a scan/probe is in progress.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(OkCommand))]
    private bool _isScanning;

    /// <summary>Status line shown under the list.</summary>
    [ObservableProperty]
    private string _statusText = "Scanning ports…";

    /// <summary>
    /// Small detection note shown next to the drop-down, e.g.
    /// "Device auto-discovered" or "Device detected". Empty when nothing found.
    /// </summary>
    [ObservableProperty]
    private string _detectionText = string.Empty;

    /// <summary>True when the currently selected port is a detected device.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(OkCommand))]
    private bool _isDeviceDetected;

    /// <summary>
    /// Set while we are probing the manually-selected port (inhibits the
    /// selection-change probe from re-entering).
    /// </summary>
    private bool _suppressSelectionProbe;

    /// <summary>Raised when the user confirms a port; the view opens the panel.</summary>
    public event EventHandler<ComPortInfo>? PortConfirmed;

    /// <summary>Raised when the user asks to close the window.</summary>
    public event EventHandler? CloseRequested;

    // ---- Commands (interface) ----
    public IAsyncRelayCommand RefreshCommand { get; }
    public IAsyncRelayCommand OkCommand { get; }
    public IRelayCommand CloseCommand { get; }

    // ---- Scanning logic ----

    /// <summary>
    /// Enumerates all serial ports, probes them concurrently and marks the one
    /// that answers <c>&gt;YES&lt;</c> as the target device.
    ///
    /// All observable updates are marshalled to the UI thread, because
    /// <c>await ... ConfigureAwait(false)</c> otherwise leaves the continuation
    /// running on a thread-pool thread where touching bound properties throws.
    /// </summary>
    private async Task RefreshAsync()
    {
        // Cancel any previous scan and start a new one.
        _scanCts?.Cancel();
        _scanCts = new CancellationTokenSource();
        var token = _scanCts.Token;

        IsScanning = true;
        StatusText = "Scanning ports…";
        AvailablePorts.Clear();
        SelectedPort = null;

        try
        {
            var entries = await Task.Run(SerialPortInfoProvider.GetPorts, token)
                .ConfigureAwait(true);
            if (entries.Count == 0)
            {
                StatusText = "No serial ports found. Check the connection and the CH340 driver.";
                return;
            }

            // Build the UI entries first (keeps original order for probing).
            var infos = entries
                .Select(e => new ComPortInfo
                {
                    PortName = e.PortName,
                    Description = e.Description,
                })
                .ToList();

            StatusText = $"Probing {infos.Count} port(s)…";

            // Probe all ports in parallel with a short timeout each. The probe
            // itself never touches bound properties, so it is safe off-thread.
            var probeTasks = infos
                .Select(info => ProbeAsync(info, token))
                .ToArray();

            var results = await Task.WhenAll(probeTasks).ConfigureAwait(true);
            for (var i = 0; i < infos.Count; i++)
            {
                infos[i].IsDevice = results[i];
            }

            if (token.IsCancellationRequested)
            {
                return;
            }

            // Sort target device(s) first, then by port number.
            var ordered = infos
                .OrderByDescending(i => i.IsDevice)
                .ThenBy(i => ExtractPortNumber(i.PortName))
                .ToList();

            AvailablePorts.Clear();
            foreach (var info in ordered)
            {
                AvailablePorts.Add(info);
            }

            var device = ordered.FirstOrDefault(i => i.IsDevice);
            if (device is not null)
            {
                _suppressSelectionProbe = true;
                SelectedPort = device;
                _suppressSelectionProbe = false;

                IsDeviceDetected = true;
                DetectionText = "Device auto-discovered";
                StatusText = $"Target device found on {device.PortName}.";
            }
            else
            {
                // No device answered: still allow manual selection.
                _suppressSelectionProbe = true;
                SelectedPort = ordered.FirstOrDefault();
                _suppressSelectionProbe = false;

                IsDeviceDetected = false;
                DetectionText = string.Empty;
                StatusText = "No lower device answered TYPE_ECHO. Select a port manually.";
            }
        }
        catch (OperationCanceledException)
        {
            // Superseded by a newer scan; ignore.
        }
        catch (Exception ex)
        {
            StatusText = $"Scan failed: {ex.Message}";
        }
        finally
        {
            if (!token.IsCancellationRequested)
            {
                IsScanning = false;
            }
        }
    }

    /// <summary>
    /// Probes a single port and returns whether it is the target device. Runs
    /// entirely off the UI thread and does not touch any bound property.
    /// </summary>
    private static Task<bool> ProbeAsync(ComPortInfo info, CancellationToken token)
    {
        return Task.Run(
            () => SerialObjectHelperModel.ProbeDeviceAsync(info.PortName, 800, token),
            token);
    }

    /// <summary>
    /// When the user picks a port from the drop-down, immediately try to
    /// connect and probe it (unless the selection came from the auto-scan).
    /// </summary>
    async partial void OnSelectedPortChanged(ComPortInfo? value)
    {
        if (_suppressSelectionProbe)
        {
            return;
        }

        if (value is null)
        {
            IsDeviceDetected = false;
            DetectionText = string.Empty;
            return;
        }

        // A freshly auto-scanned entry already knows its result.
        if (value.IsDevice)
        {
            IsDeviceDetected = true;
            DetectionText = "Device detected";
            return;
        }

        IsDeviceDetected = false;
        DetectionText = "Checking device…";

        var info = value;
        var token = _scanCts?.Token ?? CancellationToken.None;

        info.IsProbing = true;
        bool ok;
        try
        {
            ok = await Task.Run(
                () => SerialObjectHelperModel.ProbeDeviceAsync(info.PortName, 800, token),
                token).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            info.IsProbing = false;
            return;
        }
        catch
        {
            ok = false;
        }

        info.IsProbing = false;
        info.IsDevice = ok;

        // Only reflect the result if the user hasn't changed selection since.
        if (!ReferenceEquals(SelectedPort, info))
        {
            return;
        }

        IsDeviceDetected = ok;
        DetectionText = ok ? "Device detected" : "No device detected";
    }

    // ---- Commands ----

    private bool CanOk() => !IsScanning && SelectedPort is not null && IsDeviceDetected;

    private Task OkAsync()
    {
        if (SelectedPort is not null)
        {
            PortConfirmed?.Invoke(this, SelectedPort);
        }

        return Task.CompletedTask;
    }

    private void Close() => CloseRequested?.Invoke(this, EventArgs.Empty);

    private static int ExtractPortNumber(string portName)
    {
        if (portName.StartsWith("COM", StringComparison.OrdinalIgnoreCase)
            && int.TryParse(portName.AsSpan(3), out var n))
        {
            return n;
        }

        return int.MaxValue;
    }
}

/// <summary>
/// Data model for a single COM port entry shown in the ComboBox.
/// </summary>
public partial class ComPortInfo : ObservableObject
{
    /// <summary>Port name, e.g. "COM3".</summary>
    public string PortName { get; set; } = string.Empty;

    /// <summary>Friendly description, e.g. "USB-SERIAL CH340 (COM3)".</summary>
    public string Description { get; set; } = string.Empty;

    /// <summary>True while this port is being probed.</summary>
    [ObservableProperty]
    private bool _isProbing;

    /// <summary>True when this port answered <c>TYPE_ECHO</c> with <c>YES</c>.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(DisplayText))]
    private bool _isDevice;

    /// <summary>Label shown in the drop-down (device marker when applicable).</summary>
    public string DisplayText => IsDevice ? $"★ {PortName}" : PortName;

    public override string ToString() => PortName;
}
