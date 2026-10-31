using System;
using System.ComponentModel;
using System.Threading.Tasks;
using Capsa.Models;
using Capsa.Services;
using Capsa.ViewModels.Editors;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Capsa.ViewModels.Panels
{
    /// <summary>
    /// 读取模式面板：写入模式的镜像，编辑器只读，底部为进度 + “读取 &amp; 校验”。
    /// </summary>
    public sealed partial class ReadPanelViewModel : ViewModelBase
    {
        private readonly DeviceInfoViewModel _device;
        private readonly ITransferService _transfer;
        private readonly Func<IFilePickerService> _filesAccessor;

        public ReadPanelViewModel(
            DeviceInfoViewModel device,
            ITransferService transfer,
            Func<IFilePickerService> filesAccessor)
        {
            _device = device;
            _transfer = transfer;
            _filesAccessor = filesAccessor;

            Session = new EditorSessionViewModel(isReadOnly: true)
            {
                EmptyHint = "Click \"Read & Verify\" to load the full chip content",
            };
            Session.BytesReplaced += OnSessionBytesReplaced;
            _device.PropertyChanged += OnDevicePropertyChanged;

            // 读流程结束后由传输服务把数据抛过来
            _transfer.ReadCompleted += (_, data)
                => Session.LoadBytes(data, EditorChangeSource.Device, "Device dump");
        }

        public EditorSessionViewModel Session { get; }

        [ObservableProperty]
        private double _progressValue;

        [ObservableProperty]
        private string _progressText = "0%";

        [ObservableProperty]
        private string _statusText = "Waiting to read chip";

        [ObservableProperty]
        private string _contentSummary = "Not read yet";

        [ObservableProperty]
        private bool _isBusy;

        public bool IsReady => _device.IsInitialized;

        [RelayCommand]
        private async Task HealthCheckAsync()
        {
            if (!IsReady || IsBusy)
            {
                return;
            }

            var port = AppServices.Serial.ConnectedPortName;
            if (string.IsNullOrWhiteSpace(port))
            {
                StatusText = "Connect a serial port before checking chip health";
                return;
            }

            var testRan = await AppServices.HealthCheckDialog.ShowAsync(port).ConfigureAwait(true);
            if (!testRan)
            {
                return;
            }

            IsBusy = true;
            StatusText = "Applying health check results…";
            try
            {
                var info = await AppServices.Device.CheckHealthAsync().ConfigureAwait(true);
                if (info is not null)
                {
                    _device.Apply(info);
                }

                StatusText = $"Health check complete · {_device.HealthScoreText} ({_device.HealthLevelText})";
            }
            catch (Exception ex)
            {
                StatusText = $"Health check failed: {ex.Message}";
            }
            finally
            {
                IsBusy = false;
            }
        }

        [RelayCommand]
        private async Task ExportAsync()
        {
            var data = Session.Snapshot();
            if (data.Length == 0)
            {
                StatusText = "Nothing to export";
                return;
            }

            await _filesAccessor().SaveBinaryFileAsync("dump.bin", data).ConfigureAwait(true);
            StatusText = $"Exported {ByteSize.Format(data.Length)}";
        }

        [RelayCommand]
        private async Task ReadVerifyAsync()
        {
            if (!IsReady || IsBusy)
            {
                return;
            }

            IsBusy = true;
            ProgressValue = 0;
            ProgressText = "0%";
            StatusText = "Preparing to read…";

            var progress = new Progress<TransferProgress>(p =>
            {
                ProgressValue = p.Percent;
                ProgressText = $"{p.Percent:0}%";
                StatusText = p.Message;
            });

            try
            {
                // 整卡读取：8 片合计容量（单片容量 × 芯片数）
                var capacity = _device.ArrayCapacityBytes > 0
                    ? _device.ArrayCapacityBytes
                    : (_device.CapacityBytes > 0 ? _device.CapacityBytes : 1024);
                var result = await _transfer.ReadAsync(capacity, progress).ConfigureAwait(true);
                StatusText = result.Message;
                ProgressValue = 100;
                ProgressText = "100%";
            }
            catch (Exception ex)
            {
                StatusText = $"Read failed: {ex.Message}";
            }
            finally
            {
                IsBusy = false;
            }
        }

        private void OnSessionBytesReplaced(object? sender, EditorChangeSource source)
        {
            ContentSummary = Session.ByteLength == 0
                ? "Not read yet"
                : $"{Session.SizeText} · {Session.SourceName}";
        }

        private void OnDevicePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName is nameof(DeviceInfoViewModel.IsInitialized))
            {
                OnPropertyChanged(nameof(IsReady));
            }
        }
    }
}
