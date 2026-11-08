using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Media;
using Capsa.Diagnostics;
using Capsa.Models;
using Capsa.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Capsa.ViewModels.Dialogs
{
    /// <summary>
    /// “初始化存储卡”窗口：上半部分填写设备参数，下半部分展示预初始化后的属性参数，
    /// 底部为进度与操作按钮。
    /// </summary>
    public sealed partial class InitializeCardViewModel : ViewModelBase
    {
        private readonly ICardInitService _service;

        private CancellationTokenSource? _cancellation;
        private CardInitResult? _result;

        public InitializeCardViewModel(string portName)
            : this(portName, AppServices.CardInit)
        {
        }

        public InitializeCardViewModel(string portName, ICardInitService service)
        {
            _service = service;
            PortName = string.IsNullOrWhiteSpace(portName) ? "Not connected" : portName;

            DeviceName = "CAPSA-CARD-01";
            SelectedChip = ChipOptions.FirstOrDefault(c => c.Name == "24C64") ?? ChipOptions[0];
            SelectedMedium = MediumOptions[0];

            RefreshPreview();
        }

        // ===== 顶部：当前串口 =====

        public string PortName { get; }

        public string PortBadge => $"{PortName}  ·  115200 baud";

        // ===== 上半部分：可选参数 =====

        public ObservableCollection<ChipDefinition> ChipOptions { get; } = new(ChipCatalog.All);

        public ObservableCollection<StorageMediumOption> MediumOptions { get; } = new(StorageMediumCatalog.All);

        [ObservableProperty]
        private string _deviceName = string.Empty;

        [ObservableProperty]
        private ChipDefinition? _selectedChip;

        [ObservableProperty]
        private StorageMediumOption? _selectedMedium;

        // ===== 进度 =====

        [ObservableProperty]
        private double _progressValue;

        [ObservableProperty]
        private string _progressText = "0%";

        [ObservableProperty]
        private string _stageText = "Ready to initialize";

        [ObservableProperty]
        private bool _isRunning;

        [ObservableProperty]
        private bool _isCompleted;

        /// <summary>最近一次失败（非取消）。界面据此显示"Details"按钮，可再次打开错误详情。</summary>
        [ObservableProperty]
        private bool _hasError;

        private bool HasValidInput =>
            !string.IsNullOrWhiteSpace(DeviceName)
            && SelectedChip is not null
            && SelectedMedium is not null;

        public bool CanInitialize => !IsRunning && (IsCompleted || HasValidInput);

        public string PrimaryButtonText => IsCompleted ? "Done" : "Initialize";

        public string SecondaryButtonText => IsCompleted ? "Close" : "Cancel";

        // ===== 下半部分：属性参数 =====

        [ObservableProperty]
        private string _initFlagText = "0";

        [ObservableProperty]
        private IBrush _initFlagBrush = HealthPalette.Muted;

        [ObservableProperty]
        private string _healthCheckText = "—";

        [ObservableProperty]
        private IBrush _healthBrush = HealthPalette.Muted;

        [ObservableProperty]
        private IBrush _healthSoftBrush = HealthPalette.MutedSoft;

        [ObservableProperty]
        private string _healthRangeText = "0 - 8";

        [ObservableProperty]
        private string _cardNameText = "—";

        [ObservableProperty]
        private string _arrayCapacityText = "—";

        [ObservableProperty]
        private string _arrayCapacityHint = "chip capacity × healthy chips";

        [ObservableProperty]
        private string _mediumText = "—";

        [ObservableProperty]
        private string _modelText = "—";

        [ObservableProperty]
        private string _uidText = "—";

        [ObservableProperty]
        private string _initTimeText = "—";

        /// <summary>结果产生后（或用户关闭窗口时）触发，参数为初始化结果。</summary>
        public event EventHandler<CardInitResult?>? CloseRequested;

        private void RefreshPreview()
        {
            CardNameText = string.IsNullOrWhiteSpace(DeviceName) ? "—" : DeviceName.Trim();
            ModelText = SelectedChip?.Name ?? "—";
            MediumText = SelectedMedium?.DisplayName ?? "—";

            // 未初始化前按满阵列（8 颗）预估容量；芯片测试后按实际正常数量重算。
            ArrayCapacityText = SelectedChip is null
                ? "—"
                : ByteSize.Format((long)SelectedChip.Bytes * 8);

            OnPropertyChanged(nameof(CanInitialize));
        }

        // ===== 命令 =====

        [RelayCommand]
        private async Task InitializeAsync()
        {
            if (IsCompleted)
            {
                CloseRequested?.Invoke(this, _result);
                return;
            }

            if (!CanInitialize || SelectedChip is null || SelectedMedium is null)
            {
                return;
            }

            IsRunning = true;
            IsCompleted = false;
            HasError = false;
            ProgressValue = 0;
            ProgressText = "0%";
            StageText = "Writing initialization info";
            OnPropertyChanged(nameof(CanInitialize));

            _cancellation = new CancellationTokenSource();

            var request = new CardInitRequest
            {
                PortName = PortName,
                DeviceName = DeviceName.Trim(),
                Chip = SelectedChip,
                Medium = SelectedMedium,
            };

            var progress = new Progress<CardInitProgress>(OnProgress);

            try
            {
                _result = await _service.InitializeAsync(request, progress, _cancellation.Token)
                    .ConfigureAwait(true);

                ApplyResult(_result);
            }
            catch (OperationCanceledException)
            {
                StageText = "Initialization canceled";
                ProgressValue = 0;
                ProgressText = "0%";
                _result = null;
            }
            catch (Exception ex)
            {
                // ★ 报错不再是一行截断文本：完整异常 + 最近协议交互进全局日志，
                //   弹窗里可整段复制；关掉弹窗后还能用 "Details" 按钮重新打开。
                var context = $"port {PortName} · chip {SelectedChip.Name} · medium {SelectedMedium.DisplayName}";
                ErrorLog.Add("Card initialization", ex.Message, ex, context);
                HasError = true;
                StageText = $"Initialization failed: {ex.Message}";
                _result = null;

                await AppServices.Messages
                    .ShowErrorAsync("Card initialization failed", ex.Message)
                    .ConfigureAwait(true);
            }
            finally
            {
                IsRunning = false;
                _cancellation?.Dispose();
                _cancellation = null;
                OnPropertyChanged(nameof(CanInitialize));
                OnPropertyChanged(nameof(PrimaryButtonText));
                OnPropertyChanged(nameof(SecondaryButtonText));
            }
        }

        [RelayCommand]
        private void Cancel()
        {
            if (IsRunning)
            {
                _cancellation?.Cancel();
                return;
            }

            CloseRequested?.Invoke(this, _result);
        }

        /// <summary>重新打开最近一次失败的详情（弹窗被关掉后仍可查看 / 复制）。</summary>
        [RelayCommand]
        private async Task ShowErrorDetailsAsync()
        {
            var latest = Capsa.Diagnostics.ErrorLog.Latest;
            if (latest is null)
            {
                return;
            }

            await AppServices.Messages
                .ShowErrorAsync(latest.Source, latest.Summary)
                .ConfigureAwait(true);
        }

        private void OnProgress(CardInitProgress progress)
        {
            ProgressValue = progress.Percent;
            ProgressText = $"{progress.Percent:0}%";
            StageText = progress.Message;

            if (progress.Stage == CardInitStage.TestingChips)
            {
                HealthCheckText = "Testing…";
                HealthBrush = HealthPalette.Warn;
                HealthSoftBrush = HealthPalette.WarnSoft;
            }
        }

        private void ApplyResult(CardInitResult result)
        {
            InitFlagText = result.InitFlag.ToString();
            InitFlagBrush = HealthPalette.Ok;

            // 健康检查值 0 - 7，映射到统一的健康色阶（80 / 60 分档）。
            var score = (int)Math.Round(result.HealthyChips * 100d / 8d);
            var level = HealthScale.FromScore(score);
            HealthCheckText = $"{result.HealthyChips} / 8";
            HealthBrush = HealthScale.ToBrush(level);
            HealthSoftBrush = HealthScale.ToSoftBrush(level);

            CardNameText = result.DeviceName;
            ArrayCapacityText = ByteSize.Format(result.ArrayCapacityBytes);
            ArrayCapacityHint = $"{ByteSize.Format(result.Chip.Bytes)} × {result.HealthyChips} chips";
            MediumText = result.Medium.DisplayName;
            ModelText = result.Chip.Name;
            UidText = result.Uid;
            InitTimeText = result.InitializedAt.ToString("yyyy-MM-dd HH:mm:ss");

            ProgressValue = 100;
            ProgressText = "100%";
            StageText = "Initialization complete";

            IsCompleted = true;
            OnPropertyChanged(nameof(PrimaryButtonText));
            OnPropertyChanged(nameof(SecondaryButtonText));
        }

        partial void OnDeviceNameChanged(string value) => RefreshPreview();

        partial void OnSelectedChipChanged(ChipDefinition? value) => RefreshPreview();

        partial void OnSelectedMediumChanged(StorageMediumOption? value) => RefreshPreview();

        partial void OnIsCompletedChanged(bool value) => OnPropertyChanged(nameof(CanInitialize));

        /// <summary>供窗口在关闭前读取结果。</summary>
        public CardInitResult? Result => _result;
    }
}
