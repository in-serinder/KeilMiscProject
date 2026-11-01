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

        /// <summary>写入过程中的实时占用（字节）；-1 表示没有进行中的实时值。</summary>
        private long _liveUsed = -1;

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

        /// <summary>已使用空间（字节，来自 UGS）。读取流程据此只读上次写入的范围。</summary>
        public int UsedBytes => _liveUsed >= 0 ? (int)_liveUsed : (_info?.UsedBytes ?? 0);

        /// <summary>整卡数据哈希（16 位十六进制），来自 DI / DIG。</summary>
        public string DataHash => _info?.DataHash ?? string.Empty;

        /// <summary>累计写入次数，来自 DI / DIG。</summary>
        public long WriteCount => _info?.WriteCount ?? 0;

        /// <summary>属性面板里显示的“哈希 + 次数”紧凑文本。</summary>
        [ObservableProperty]
        private string _integrityText = "—";

        [ObservableProperty]
        private string _integrityTooltip = "Hash / write count not available";

        public void Apply(DeviceInfo? info)
        {
            _info = info;
            _liveUsed = -1;   // 真实状态覆盖实时估算值

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
            // 哈希缺失时仍然显示累计写入次数：它是寿命指标，
            // 初始化 / 格式化都不会把它清零，必须让用户看得到。
            IntegrityText = string.IsNullOrWhiteSpace(info.DataHash)
                ? (info.WriteCount > 0 ? $"No record  #{info.WriteCount}" : "No record")
                : $"{info.DataHash}  #{info.WriteCount}";
            IntegrityTooltip = string.IsNullOrWhiteSpace(info.DataHash)
                ? (info.WriteCount > 0
                    ? $"No data hash recorded · write count #{info.WriteCount} (card lifetime)"
                    : "Hash / write count not recorded on this card yet")
                : $"Data hash {info.DataHash} · write #{info.WriteCount}";

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
            OnPropertyChanged(nameof(UsedBytes));
            OnPropertyChanged(nameof(DataHash));
            OnPropertyChanged(nameof(WriteCount));
        }

        /// <summary>
        /// 写入过程中按已写入字节数实时刷新占用进度条与文字。
        /// 不修改缓存的设备信息；写入结束后由 <see cref="Apply"/> 用真实 UGS 值覆盖。
        /// </summary>
        public void SetLiveUsedBytes(long usedBytes)
        {
            var total = _info?.ArrayTotalBytes ?? 0;
            if (total <= 0)
            {
                return;
            }

            if (usedBytes < 0)
            {
                usedBytes = 0;
            }
            if (usedBytes > total)
            {
                usedBytes = total;
            }
            if (_liveUsed == usedBytes)
            {
                return;   // 进度没变就别刷 UI
            }

            _liveUsed = usedBytes;
            var percent = usedBytes * 100d / total;
            UsagePercent = Math.Clamp(percent, 0d, 100d);
            UsageText =
                $"Used {ByteSize.Format(usedBytes)} of {ByteSize.Format(total)}  " +
                $"({percent:0.#}%)  ·  {usedBytes:N0} B / {total:N0} B  ·  writing…";
            OnPropertyChanged(nameof(UsedBytes));
        }

        /// <summary>Unix 秒缺失（0）时显示占位符。</summary>
        private static string FormatTime(DateTimeOffset value)
            => value.Year < 2000 ? "—" : value.ToString("yyyy-MM-dd HH:mm:ss");

        public void Reset()
        {
            _info = null;
            _liveUsed = -1;
            DeviceName = "—";
            Uid = "—";
            ChipModel = "—";
            ChipType = "—";
            CapacityText = "—";
            UsageText = "—";
            UsagePercent = 0;
            LastWriteText = "—";
            LastAccessText = "—";
            IntegrityText = "—";
            IntegrityTooltip = "Hash / write count not available";
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
            OnPropertyChanged(nameof(UsedBytes));
            OnPropertyChanged(nameof(DataHash));
            OnPropertyChanged(nameof(WriteCount));
        }
    }
}
