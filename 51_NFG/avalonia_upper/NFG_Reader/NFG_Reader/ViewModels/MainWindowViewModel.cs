using System;
using System.Collections.ObjectModel;
using System.Text;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using NFG_Reader.Models;

namespace NFG_Reader.ViewModels
{
    /// <summary>
    /// Front-end backing view-model for the main window.
    /// This file currently carries UI-state only (no serial I/O wired yet).
    /// </summary>
    public partial class MainWindowViewModel : ViewModelBase
    {
        public const string DeviceName = "AS6C62256-55PCN";
        public const string DeviceDensity = "256 Kbit";
        public const string DeviceOrganization = "32K x 8 bit  (32768 bytes / 32 KB)";
        public const string DeviceAddressRange = "0x0000 ~ 0x7FFF";
        public const string RepoUrl = "https://github.com/in-serinder/KeilMiscProject/tree/master/51_NFG";

        // Instance wrappers so XAML compiled bindings can consume the constants.
        public string DeviceNameText => DeviceName;
        public string DeviceDensityText => DeviceDensity;
        public string DeviceOrganizationText => DeviceOrganization;
        public string DeviceAddressRangeText => DeviceAddressRange;
        public string RepositoryUrl => RepoUrl;

        public MainWindowViewModel()
        {
            // Design-time sample ports; the real list is filled on refresh.
            ComPorts = new ObservableCollection<ComPortInfo>
            {
                                new ComPortInfo("COM3", "USB-SERIAL CH340"),
                new ComPortInfo("COM7", "Silicon Labs CP210x USB to UART"),
                new ComPortInfo("COM12", "Prolific USB-to-Serial Comm Port")
            };
        }

        // -------------------- Connection --------------------

        public ObservableCollection<ComPortInfo> ComPorts { get; }

        [ObservableProperty]
        private ComPortInfo? _selectedComPort;

                [ObservableProperty]
        private bool _isConnected;

        /// <summary>Password supplied via the authentication dialog for the current port.</summary>
        [ObservableProperty]
        private string _password = string.Empty;

        /// <summary>Small status caption shown beneath the port combo box.</summary>
        public string ConnectionStatusText => IsConnected
            ? $"Connected to {SelectedComPort?.PortName}"
            : "Disconnected";

        /// <summary>Indicator dot color for the connection status line.</summary>
        public IBrush StatusBrush => IsConnected
            ? new SolidColorBrush(Color.Parse("#FF16A34A"))
            : new SolidColorBrush(Color.Parse("#FFDC2626"));

        partial void OnIsConnectedChanged(bool value)
        {
            OnPropertyChanged(nameof(ConnectionStatusText));
            OnPropertyChanged(nameof(StatusBrush));
        }

        partial void OnSelectedComPortChanged(ComPortInfo? value) => OnPropertyChanged(nameof(ConnectionStatusText));
        // -------------------- Read tab --------------------

        [ObservableProperty]
        private string _readFileName = "No file loaded";
        [ObservableProperty]
        private long _readFileLength;

        public string ReadFileSizeText => ReadFileLength <= 0
            ? "0 B  (0 bytes)"
            : $"{FormatBytes(ReadFileLength)}  ({ReadFileLength:N0} bytes)";

        partial void OnReadFileLengthChanged(long value) => OnPropertyChanged(nameof(ReadFileSizeText));
        [ObservableProperty]
        private string _readTextContent = string.Empty;

        public string ReadTextCountText => $"{ReadTextContent.Length:N0} chars";

        partial void OnReadTextContentChanged(string value) => OnPropertyChanged(nameof(ReadTextCountText));

        /// <summary>Raw bytes for the read-tab hex viewer (populated once a read completes).</summary>
        [ObservableProperty]
        private byte[] _readBytes = Array.Empty<byte>();

        partial void OnReadBytesChanged(byte[] value)
        {
            OnPropertyChanged(nameof(ReadFileSizeText));
            RebuildReadText();
        }

        public ObservableCollection<string> Encodings { get; } = new()
        {
            "UTF-8", "UTF-16 LE", "UTF-16 BE", "ASCII", "GB2312", "Latin-1"
        };

        [ObservableProperty]
        private string _selectedEncoding = "UTF-8";

        partial void OnSelectedEncodingChanged(string value) => RebuildReadText();

        /// <summary>
        /// Decodes the raw read bytes with the currently selected encoding.
        /// Never throws: an unavailable code page falls back to UTF-8 so the
        /// hex viewer is never blanked by an encoding error.
        /// </summary>
        private void RebuildReadText()
        {
            if (ReadBytes.Length == 0)
            {
                ReadTextContent = string.Empty;
                return;
            }

            try
            {
                ReadTextContent = ResolveEncoding(SelectedEncoding).GetString(ReadBytes);
            }
            catch
            {
                ReadTextContent = Encoding.UTF8.GetString(ReadBytes);
            }
        }

        // -------------------- Write tab --------------------

        [ObservableProperty]
        private string _writeTextContent = string.Empty;

        public string WriteTextCountText => $"{WriteTextContent.Length:N0} chars";

        partial void OnWriteTextContentChanged(string value)
        {
            OnPropertyChanged(nameof(WriteTextCountText));
            RebuildWriteBytes();
        }

        /// <summary>Encoding used to convert the typed text into bytes for the hex view.</summary>
        [ObservableProperty]
        private string _writeEncoding = "UTF-8";

        partial void OnWriteEncodingChanged(string value) => RebuildWriteBytes();

        /// <summary>Raw bytes derived from <see cref="WriteTextContent"/> (feeds the hex viewer).</summary>
        [ObservableProperty]
        private byte[] _writeBytes = Array.Empty<byte>();

        public string WriteHexSizeText => WriteBytes.Length == 0
            ? "0 B  (0 bytes)"
            : $"{FormatBytes(WriteBytes.Length)}  ({WriteBytes.Length:N0} bytes)";

        private void RebuildWriteBytes()
        {
            try
            {
                WriteBytes = string.IsNullOrEmpty(WriteTextContent)
                    ? Array.Empty<byte>()
                    : ResolveEncoding(WriteEncoding).GetBytes(WriteTextContent);
            }
            catch
            {
                WriteBytes = Array.Empty<byte>();
            }

            OnPropertyChanged(nameof(WriteHexSizeText));
        }

        private static Encoding ResolveEncoding(string name) => name switch
        {
            "ASCII" => Encoding.ASCII,
            "UTF-16 LE" => Encoding.Unicode,
            "UTF-16 BE" => Encoding.BigEndianUnicode,
            "GB2312" => GetLegacyEncoding(936),      // GBK (superset of GB2312)
            "Latin-1" => Encoding.Latin1,
            _ => Encoding.UTF8
        };

        /// <summary>
        /// Returns a legacy code page if it is available, otherwise falls back to UTF-8.
        /// Requires CodePagesEncodingProvider to be registered at startup.
        /// </summary>
        private static Encoding GetLegacyEncoding(int codePage)
        {
            try
            {
                return Encoding.GetEncoding(codePage);
            }
            catch
            {
                return Encoding.UTF8;
            }
        }

        // -------------------- Shared progress --------------------

        [ObservableProperty]
        private double _progressValue;

        [ObservableProperty]
        private string _progressText = "Ready";

        public int ProgressPercent => (int)Math.Round(ProgressValue);

        partial void OnProgressValueChanged(double value) => OnPropertyChanged(nameof(ProgressPercent));

        // -------------------- Commands (UI stubs) --------------------

        [RelayCommand]
        private void RefreshPorts()
        {
            // TODO: enumerate System.IO.Ports.SerialPort.GetPortNames()
        }

        [RelayCommand]
        private void Read() => ProgressText = "Read started...";

        [RelayCommand]
        private void Write() => ProgressText = "Write started...";

        [RelayCommand]
        private void SaveTextAsTxt()
        {
            // TODO: file save dialog + write ReadTextContent
        }

        // -------------------- Helpers --------------------

        private static string FormatBytes(long bytes)
        {
            string[] units = { "B", "KB", "MB", "GB" };
            double size = bytes;
            int unit = 0;
            while (size >= 1024 && unit < units.Length - 1)
            {
                size /= 1024;
                unit++;
            }
            return unit == 0 ? $"{size:0} {units[unit]}" : $"{size:0.##} {units[unit]}";
        }
    }
}

