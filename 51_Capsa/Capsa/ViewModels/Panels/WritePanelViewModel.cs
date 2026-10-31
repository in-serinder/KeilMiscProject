using System;
using System.ComponentModel;
using System.IO;
using System.Threading;
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
    /// 写入模式面板：编辑器模式单选框 + 工具栏 + 编辑器 + 进度 &amp; “校验 &amp; 写入”。
    /// </summary>
    public sealed partial class WritePanelViewModel : ViewModelBase
    {
        private readonly DeviceInfoViewModel _device;
        private readonly ChipBoardViewModel _board;
        private readonly IDeviceService _deviceService;
        private readonly ITransferService _transfer;
        private readonly Func<IFilePickerService> _filesAccessor;
        private readonly Func<Task> _refreshDeviceAsync;

        // 进度上屏：后台只写“最新快照”，UI 用定时器按固定节奏取一次。
        // 这样即使界面渲染偶尔繁忙，进度条也不会落后于实际传输。
        private readonly DispatcherTimer _progressTimer;
        private readonly IProgress<TransferProgress> _writeProgress;
        private readonly IProgress<TransferProgress> _verifyProgress;

        /// <summary>写入阶段占进度条的比例，其余留给回读校验。</summary>
        private const double WriteShare = 0.85;

        // 快照必须是“整体替换”：直接共享结构体会出现撕裂读（进度 41% 但卡片 1%）。
        // 实际写入卡里的文件名（≤40 B，超长已退回 dump.<ext>）：附在数据末尾供导出还原
        private string _payloadFileName = string.Empty;

        private ProgressSnapshot? _latest;
        private int _hasPending;

        public WritePanelViewModel(
            DeviceInfoViewModel device,
            ChipBoardViewModel board,
            IDeviceService deviceService,
            ITransferService transfer,
            Func<IFilePickerService> filesAccessor,
            Func<Task> refreshDeviceAsync)
        {
            _device = device;
            _board = board;
            _deviceService = deviceService;
            _transfer = transfer;
            _filesAccessor = filesAccessor;
            _refreshDeviceAsync = refreshDeviceAsync;

            Session = new EditorSessionViewModel
            {
                EmptyHint = "Import any file — its extension is stored at the end of the payload",
            };
            Session.BytesReplaced += OnSessionBytesReplaced;
            _device.PropertyChanged += OnDevicePropertyChanged;

            _writeProgress = new SnapshotProgress(this, phase: 0);
            _verifyProgress = new SnapshotProgress(this, phase: 1);

            _progressTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(200) };
            _progressTimer.Tick += (_, _) => FlushProgress();
        }

        public EditorSessionViewModel Session { get; }

        [ObservableProperty]
        private double _progressValue;

        [ObservableProperty]
        private string _progressText = "0%";

        [ObservableProperty]
        private string _statusText = "Waiting for payload";

        [ObservableProperty]
        private string _contentSummary = "Buffer empty";

        [ObservableProperty]
        private bool _isBusy;

        /// <summary>设备已初始化且当前没有任务在跑。</summary>
        public bool IsReady => _device.IsInitialized;

        /// <summary>打开“芯片健康检查”窗口，逐颗芯片做 0xFF / 0x00 读写校验。</summary>
        [RelayCommand]
        private async Task CheckHealthAsync()
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
                await _deviceService.CheckHealthAsync().ConfigureAwait(true);
                await _refreshDeviceAsync().ConfigureAwait(true);
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

        /// <summary>打开“初始化存储卡”窗口，完成后把结果应用到主界面。</summary>
        [RelayCommand]
        private async Task ReinitializeAsync()
        {
            if (IsBusy)
            {
                return;
            }

            var port = AppServices.Serial.ConnectedPortName;
            if (string.IsNullOrWhiteSpace(port))
            {
                StatusText = "Connect a serial port before initializing the card";
                return;
            }

            var result = await AppServices.InitializeDialog.ShowAsync(port).ConfigureAwait(true);
            if (result is null)
            {
                StatusText = "Initialization canceled";
                return;
            }

            IsBusy = true;
            StatusText = "Applying initialization result…";
            try
            {
                await _deviceService.ReinitializeAsync().ConfigureAwait(true);
                await _refreshDeviceAsync().ConfigureAwait(true);

                // 用初始化窗口的真实结果覆盖左侧属性面板（名称 / UID / 容量 / 健康分）。
                _device.Apply(BuildDeviceInfo(result));

                StatusText = $"Card {result.DeviceName} initialized · UID {result.Uid}";
            }
            catch (Exception ex)
            {
                StatusText = $"Failed to apply init result: {ex.Message}";
            }
            finally
            {
                IsBusy = false;
            }
        }

        [RelayCommand]
        private async Task FormatAsync()
        {
            if (!IsReady || IsBusy)
            {
                return;
            }

            IsBusy = true;
            ProgressValue = 0;
            ProgressText = "0%";
            StatusText = "Formatting storage card…";
            try
            {
                await _deviceService.FormatAsync().ConfigureAwait(true);
                await _deviceService.SetUsedSpaceAsync(0).ConfigureAwait(true);
                await _refreshDeviceAsync().ConfigureAwait(true);
                ProgressValue = 100;
                ProgressText = "100%";
                StatusText = "Storage card formatted";
            }
            catch (Exception ex)
            {
                StatusText = $"Format failed: {ex.Message}";
            }
            finally
            {
                IsBusy = false;
            }
        }

        [RelayCommand]
        private async Task ImportFileAsync()
        {
            if (IsBusy)
            {
                return;
            }

            var picked = await _filesAccessor().PickBinaryFileAsync().ConfigureAwait(true);
            if (picked is null)
            {
                return;
            }

            var (name, data) = picked.Value;
            ApplyImportedFile(name, data);
        }

        /// <summary>
        /// 装载一个导入文件：记住扩展名（写入时附在数据末尾），并把内容放进缓冲区。
        /// 独立出来便于走查 / 测试直接调用，无需打开文件对话框。
        /// </summary>
        internal void ApplyImportedFile(string name, byte[] data)
        {
            _payloadFileName = FilePayloadCodec.BuildStoredName(name);
            Session.LoadBytes(data, EditorChangeSource.Import, name);

            StatusText = _payloadFileName.Length > 0
                ? $"Imported {name} ({ByteSize.Format(data.Length)}) · stored name \"{_payloadFileName}\""
                : $"Imported {name} ({ByteSize.Format(data.Length)})";
        }

        [RelayCommand]
        private async Task WriteVerifyAsync()
        {
            if (!IsReady || IsBusy)
            {
                return;
            }

            var content = Session.Snapshot();
            if (content.Length == 0)
            {
                StatusText = "Buffer is empty — nothing to write";
                return;
            }

            // 把原文件扩展名附在数据末尾：读取时据此还原文件格式
            var payload = FilePayloadCodec.Wrap(content, _payloadFileName);

            var capacity = _device.ArrayCapacityBytes;
            if (capacity > 0 && payload.Length > capacity)
            {
                StatusText =
                    $"File too large · {ByteSize.Format(content.Length)} + trailer exceeds " +
                    $"{ByteSize.Format(capacity)} array capacity";
                return;
            }

            IsBusy = true;
            ProgressValue = 0;
            ProgressText = "0%";
            StatusText = "Preparing to write…";

            // 整卡进度归零：上一轮写过的卡片不能带着旧填充进场
            _board.BeginWrite();

            StartProgressPump();

            try
            {
                var result = await _transfer.WriteAsync(payload, _writeProgress).ConfigureAwait(true);
                if (!result.Success)
                {
                    StatusText = result.Message;

                    // 半途失败：用设备真实状态还原卡片，避免停在半格
                    await _refreshDeviceAsync().ConfigureAwait(true);
                    return;
                }

                // 写完立即回读校验，确保与缓冲区一致
                StatusText = "Verifying written data…";
                var verify = await _transfer.VerifyAsync(payload, _verifyProgress).ConfigureAwait(true);
                if (!verify.Success)
                {
                    StatusText = verify.Message;
                    await _refreshDeviceAsync().ConfigureAwait(true);
                    return;
                }

                // 校验通过：把实际占用上报到 24C02（UG），再用 UGS 回填界面
                StatusText = "Recording used space…";
                var recorded = await _deviceService.SetUsedSpaceAsync(payload.Length).ConfigureAwait(true);

                ProgressValue = 100;
                ProgressText = "100%";
                await _refreshDeviceAsync().ConfigureAwait(true);

                StatusText = recorded
                    ? $"Write & verify OK · {ByteSize.Format(content.Length)} + {payload.Length - content.Length} B trailer · " +
                      $"{ByteSize.Format(payload.Length)} used space recorded"
                    : $"Write & verify OK · used space not recorded (UG failed)";
            }
            catch (Exception ex)
            {
                StatusText = $"Write failed: {ex.Message}";

                // 失败时用设备真实状态覆盖实时进度，避免卡片停在半格
                await _refreshDeviceAsync().ConfigureAwait(true);
            }
            finally
            {
                // 收尾时丢弃排队中的进度快照，避免“Verifying…”覆盖最终结论
                StopProgressPump();
                Interlocked.Exchange(ref _hasPending, 0);
                Volatile.Write(ref _latest, null);
                IsBusy = false;
            }
        }

        // ---- 进度上屏 ----

        private void StartProgressPump()
        {
            Interlocked.Exchange(ref _hasPending, 0);
            _progressTimer.Start();
        }

        private void StopProgressPump() => _progressTimer.Stop();

        /// <summary>把后台线程记录的最新快照搬到界面（定时器 / 收尾时调用）。</summary>
        private void FlushProgress()
        {
            if (Interlocked.Exchange(ref _hasPending, 0) == 0)
            {
                return;
            }

            var snapshot = Volatile.Read(ref _latest);
            if (snapshot is null)
            {
                return;
            }

            var p = snapshot.Value;

            if (snapshot.Phase == 0)
            {
                ProgressValue = p.Percent * WriteShare;
                ProgressText = $"{ProgressValue:0}%";
                StatusText = p.Message;

                // 8 片组成阵列：按地址顺序写入，比当前片小的都已经写满；
                // 当前这一片按片内百分比驱动卡片进度条。
                if (p.ChipAddress != 0)
                {
                    for (var addr = 0x50; addr < p.ChipAddress; addr++)
                    {
                        _board.MarkFull(addr);
                    }

                    _board.ReportProgress(p.ChipAddress, p.ChipBytes);
                }
            }
            else
            {
                ProgressValue = (WriteShare + p.Percent / 100d * (1 - WriteShare)) * 100;
                ProgressText = $"{ProgressValue:0}%";
                StatusText = p.Message;
            }
        }

        /// <summary>后台线程只保存最新值，绝不在传输线程里碰 UI。</summary>
        private sealed class SnapshotProgress : IProgress<TransferProgress>
        {
            private readonly WritePanelViewModel _owner;
            private readonly int _phase;

            public SnapshotProgress(WritePanelViewModel owner, int phase)
            {
                _owner = owner;
                _phase = phase;
            }

            public void Report(TransferProgress value)
            {
                // 引用赋值是原子的，避免 UI 读到“半新半旧”的结构体
                Volatile.Write(ref _owner._latest, new ProgressSnapshot(value, _phase));
                Interlocked.Exchange(ref _owner._hasPending, 1);
            }
        }

        /// <summary>一次进度快照 + 所处阶段。</summary>
        private sealed record ProgressSnapshot(TransferProgress Value, int Phase);

        private void OnSessionBytesReplaced(object? sender, EditorChangeSource source)
        {
            ContentSummary = Session.ByteLength == 0
                ? "Buffer empty"
                : $"{Session.SizeText} · {Session.SourceName}";

            if (Session.ByteLength > 0 && source is EditorChangeSource.Import)
            {
                ProgressValue = 0;
                ProgressText = "0%";
            }
        }

        /// <summary>把初始化结果转换为左侧面板使用的设备信息。</summary>
        private static DeviceInfo BuildDeviceInfo(CardInitResult result) => new()
        {
            DeviceName = result.DeviceName,
            Uid = result.Uid,
            ChipModel = result.Chip.Name,
            ChipType = result.Medium.DisplayName,
            CapacityBytes = (int)Math.Min(int.MaxValue, result.ArrayCapacityBytes),
            UsedBytes = 0,
            LastWriteTime = result.InitializedAt,
            LastAccessTime = result.InitializedAt,
            HealthScore = (int)Math.Round(result.HealthyChips * 100d / 8d),
        };

        private void OnDevicePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName is nameof(DeviceInfoViewModel.IsInitialized))
            {
                OnPropertyChanged(nameof(IsReady));
            }
        }
    }
}
