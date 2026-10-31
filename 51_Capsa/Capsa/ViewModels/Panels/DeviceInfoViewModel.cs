using System;
using Avalonia.Media;
using Capsa.Models;
using CommunityToolkit.Mvvm.ComponentModel;

namespace Capsa.ViewModels.Panels
{
    /// <summary>左侧属性 / 信息面板。</summary>
    public sealed partial class DeviceInfoViewModel : ViewModelBase
    {
        private DeviceInfo? _info;

        [ObservableProperty]
        private bool _isInitialized;

        [ObservableProperty]
        private string _deviceName = "—";

        [ObservableProperty]
        private string _uid = "—";

        [ObservableProperty]
        private string _chipModel = "—";

        [ObservableProperty]
        private string _chipType = "—";

        [ObservableProperty]
        private string _capacityText = "—";

        [ObservableProperty]
        private string _usageText = "—";

        [ObservableProperty]
        private double _usagePercent;

        [ObservableProperty]
        private string _lastWriteText = "—";

        [ObservableProperty]
        private string _lastAccessText = "—";

        [ObservableProperty]
        private string _healthScoreText = "—";

        [ObservableProperty]
        private double _healthScoreValue;

        [ObservableProperty]
        private string _healthLevelText = "Not detected";

        [ObservableProperty]
        private IBrush _healthBrush = HealthPalette.Error;

        [ObservableProperty]
        private IBrush _healthSoftBrush = HealthPalette.ErrorSoft;

        [ObservableProperty]
        private bool _isHealthy;

        [ObservableProperty]
        private bool _isWarning;

        [ObservableProperty]
        private bool _isError;

        public string StateText => IsInitialized ? "Initialized" : "Not initialized";

        public string HealthHint => "80-100 healthy / 60-80 warning / 0-60 critical";

        /// <summary>单片容量（字节）。</summary>
        public int CapacityBytes => _info?.CapacityBytes ?? 0;

        /// <summary>阵列总容量（8 片合计，字节），供读取流程与占用百分比使用。</summary>
        public int ArrayCapacityBytes => _info?.ArrayTotalBytes ?? 0;

        public void Apply(DeviceInfo? info)
        {
            _info = info;

            if (info is null)
            {
                Reset();
                return;
            }

            DeviceName = info.DeviceName;
            Uid = info.Uid;
            ChipModel = info.ChipModel;
            ChipType = info.ChipType;
            CapacityText = $"{ByteSize.Format(info.CapacityBytes)}  ·  {info.CapacityBytes:N0} B";
            UsagePercent = Math.Clamp(info.UsagePercent, 0d, 100d);
            UsageText =
                $"Used {ByteSize.Format(info.UsedBytes)} of {ByteSize.Format(info.ArrayTotalBytes)}  " +
                $"({info.UsagePercent:0.#}%)  ·  {info.UsedBytes:N0} B / {info.ArrayTotalBytes:N0} B";
            LastWriteText = FormatTime(info.LastWriteTime);
            LastAccessText = FormatTime(info.LastAccessTime);

            var level = HealthScale.FromScore(info.HealthScore);
            HealthScoreText = info.HealthScore.ToString();
            HealthScoreValue = info.HealthScore;
            HealthLevelText = HealthScale.ToDisplayText(level);
            HealthBrush = HealthScale.ToBrush(level);
            HealthSoftBrush = HealthScale.ToSoftBrush(level);
            IsHealthy = level == HealthLevel.Healthy;
            IsWarning = level == HealthLevel.Warning;
            IsError = level == HealthLevel.Error;

            IsInitialized = true;
            OnPropertyChanged(nameof(StateText));
            OnPropertyChanged(nameof(CapacityBytes));
            OnPropertyChanged(nameof(ArrayCapacityBytes));
        }

        /// <summary>Unix 秒缺失（0）时显示占位符。</summary>
        private static string FormatTime(DateTimeOffset value)
            => value.Year < 2000 ? "—" : value.ToString("yyyy-MM-dd HH:mm:ss");

        public void Reset()
        {
            _info = null;
            DeviceName = "—";
            Uid = "—";
            ChipModel = "—";
            ChipType = "—";
            CapacityText = "—";
            UsageText = "—";
            UsagePercent = 0;
            LastWriteText = "—";
            LastAccessText = "—";
            HealthScoreText = "—";
            HealthScoreValue = 0;
            HealthLevelText = "Not detected";
            HealthBrush = HealthPalette.Error;
            HealthSoftBrush = HealthPalette.ErrorSoft;
            IsHealthy = false;
            IsWarning = false;
            IsError = true;
            IsInitialized = false;
            OnPropertyChanged(nameof(StateText));
            OnPropertyChanged(nameof(CapacityBytes));
            OnPropertyChanged(nameof(ArrayCapacityBytes));
        }
    }
}
