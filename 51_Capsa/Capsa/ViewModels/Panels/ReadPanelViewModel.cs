using System;
using System.ComponentModel;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Threading;
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
        private readonly ChipBoardViewModel _board;
        private readonly ITransferService _transfer;
        private readonly Func<IFilePickerService> _filesAccessor;

        /// <summary>从数据末尾还原出的原文件名，导出时作为默认文件名。</summary>
        private string _restoredFileName = string.Empty;

        public ReadPanelViewModel(
            DeviceInfoViewModel device,
            ChipBoardViewModel board,
            ITransferService transfer,
            Func<IFilePickerService> filesAccessor)
        {
            _device = device;
            _board = board;
            _transfer = transfer;
            _filesAccessor = filesAccessor;

            Session = new EditorSessionViewModel(isReadOnly: true)
            {
                EmptyHint = "Click \"Read & Verify\" to load data up to the last written position",
            };
            Session.BytesReplaced += OnSessionBytesReplaced;
            _device.PropertyChanged += OnDevicePropertyChanged;

            // 读流程结束后由传输服务把数据抛过来
            _transfer.ReadCompleted += OnReadCompleted;
        }

        public EditorSessionViewModel Session { get; }

        /// <summary>
        /// 装载读取结果：重建 HEX 控件必须在 UI 线程执行。
        /// 服务层已在 UI 线程触发事件，这里再兜一层，防止换实现后回归。
        /// </summary>
        private void OnReadCompleted(object? sender, byte[] raw)
        {
            // 数据末尾可能带着写入时附上的文件名，先还原原始文件内容
            var payload = FilePayloadCodec.Unwrap(raw);
            var name = string.IsNullOrEmpty(payload.FileName)
                ? "Device dump"
                : $"Device dump ({payload.FileName})";

            void Load()
            {
                _restoredFileName = payload.FileName ?? string.Empty;
                Session.LoadBytes(payload.Data, EditorChangeSource.Device, name);
            }

            if (Dispatcher.UIThread.CheckAccess())
            {
                Load();
            }
            else
            {
                Dispatcher.UIThread.Post(Load);
            }
        }

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

            // 编辑器里已是还原后的内容；若仍带着 trailer 也再拆一次，保证导出的是原文件
            var payload = FilePayloadCodec.Unwrap(data);
            var fileName = !string.IsNullOrEmpty(payload.FileName)
                ? payload.FileName!
                : (_restoredFileName.Length > 0 ? _restoredFileName : "dump.bin");

            var extension = payload.Extension
                            ?? FilePayloadCodec.NormalizeExtension(Path.GetExtension(fileName));

            await _filesAccessor()
                .SaveBinaryFileAsync(fileName, payload.Data, extension.Length > 0 ? extension : null)
                .ConfigureAwait(true);

            StatusText = $"Exported {ByteSize.Format(payload.Data.Length)} as {fileName} (restored)";
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

            // 上方存储卡联动：绿色覆盖层表示读取进度
            _board.BeginRead();

            var progress = new Progress<TransferProgress>(p =>
            {
                ProgressValue = p.Percent;
                ProgressText = $"{p.Percent:0}%";
                StatusText = p.Message;

                if (p.ChipAddress != 0)
                {
                    // 顺序读取：比当前片小的都已读完
                    for (var addr = 0x50; addr < p.ChipAddress; addr++)
                    {
                        var previous = _board.Find(addr);
                        if (previous is not null)
                        {
                            _board.ReportReadProgress(addr, previous.CapacityBytes);
                        }
                    }

                    _board.ReportReadProgress(p.ChipAddress, p.ChipBytes);
                }
            });

            try
            {
                // 只读到上次写入的位置：优先用 24C02 里的“已使用空间”（UGS）
                var capacity = _device.ArrayCapacityBytes > 0
                    ? _device.ArrayCapacityBytes
                    : (_device.CapacityBytes > 0 ? _device.CapacityBytes : 1024);

                var used = _device.UsedBytes;
                var length = used > 0 ? Math.Min(used, capacity) : capacity;

                var result = await _transfer.ReadAsync(length, progress).ConfigureAwait(true);

                if (result.Success)
                {
                    _board.ApplyReadCoverage(length);
                }

                StatusText = used > 0
                    ? $"{result.Message} · used space {ByteSize.Format(length)}"
                    : $"{result.Message} · used space is 0, read the whole array";
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
