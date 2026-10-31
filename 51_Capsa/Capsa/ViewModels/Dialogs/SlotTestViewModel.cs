using System;
using Avalonia.Media;
using Capsa.Models;
using CommunityToolkit.Mvvm.ComponentModel;

namespace Capsa.ViewModels.Dialogs
{
    /// <summary>健康检查窗口中的单颗芯片卡片（整卡为测试进度条）。</summary>
    public sealed partial class SlotTestViewModel : ViewModelBase
    {
        private ChipTestPhase _phase = ChipTestPhase.Idle;

        public SlotTestViewModel(int address)
        {
            Address = address;
        }

        public int Address { get; }

        public string AddressText => $"0x{Address:X2}";

        [ObservableProperty]
        private bool _isPresent;

        /// <summary>仅表示芯片容量大小，不表示已写入空间。</summary>
        [ObservableProperty]
        private string _capacityText = "—";

        /// <summary>单片容量（字节），用于整片测试的汇总文案。</summary>
        public int CapacityBytes { get; private set; }

        [ObservableProperty]
        private double _progress;

        [ObservableProperty]
        private string _progressText = "—";

        /// <summary>IDLE / WRITE / READ / HEALTHY / WARN / ERR / EMPTY。</summary>
        [ObservableProperty]
        private string _phaseText = "IDLE";

        [ObservableProperty]
        private IBrush _fillBrush = HealthPalette.MutedSoft;

        [ObservableProperty]
        private IBrush _accentBrush = HealthPalette.Muted;

        [ObservableProperty]
        private IBrush _badgeBrush = HealthPalette.Muted;

        [ObservableProperty]
        private IBrush _badgeSoftBrush = HealthPalette.MutedSoft;

        /// <summary>0 - 1，用于整卡填充的横向缩放。</summary>
        public double Ratio => Math.Clamp(Progress / 100d, 0d, 1d);

        public bool IsWriting => _phase == ChipTestPhase.Writing;

        public bool IsReading => _phase == ChipTestPhase.Reading;

        public void Apply(SlotStatus? status)
        {
            IsPresent = status?.Present ?? false;
            CapacityBytes = IsPresent && status is not null ? status.CapacityBytes : 0;
            CapacityText = IsPresent && status is not null
                ? ByteSize.Format(status.CapacityBytes)
                : "No chip";

            Reset();
        }

        public void Reset()
        {
            _phase = ChipTestPhase.Idle;
            Progress = 0;
            ProgressText = "—";
            PhaseText = IsPresent ? "IDLE" : "EMPTY";
            SetColors(HealthPalette.Muted, HealthPalette.MutedSoft);
        }

        /// <summary>切换测试相位：写入（紫）/ 读取（绿）。重复调用同一相位不会产生额外通知。</summary>
        public void EnterPhase(ChipTestPhase phase)
        {
            if (_phase == phase)
            {
                return;
            }

            _phase = phase;

            switch (phase)
            {
                case ChipTestPhase.Writing:
                    PhaseText = "WRITE";
                    SetColors(HealthPalette.Write, HealthPalette.WriteSoft);
                    Progress = 0;
                    break;
                case ChipTestPhase.Reading:
                    PhaseText = "READ";
                    SetColors(HealthPalette.Read, HealthPalette.ReadSoft);
                    Progress = 0;
                    break;
                default:
                    return;
            }

            ProgressText = "0%";
            OnPropertyChanged(nameof(IsWriting));
            OnPropertyChanged(nameof(IsReading));
        }

        public void SetProgress(double percent)
        {
            Progress = percent;
            ProgressText = $"{percent:0}%";
        }

        /// <summary>
        /// 测试完成：卡片整体染色为结果色
        /// （健康蓝 / 警告黄 / 中等橙 / 错误红）。
        /// </summary>
        public void Complete(ChipHealthGrade grade)
        {
            _phase = ChipTestPhase.Completed;

            PhaseText = grade switch
            {
                ChipHealthGrade.Healthy => "HEALTHY",
                ChipHealthGrade.Warning => "WARN",
                ChipHealthGrade.Medium => "MEDIUM",
                _ => "ERR",
            };

            var (accent, soft) = grade switch
            {
                ChipHealthGrade.Healthy => (HealthPalette.Ok, HealthPalette.OkSoft),
                ChipHealthGrade.Warning => (HealthPalette.Warn, HealthPalette.WarnSoft),
                ChipHealthGrade.Medium => (HealthPalette.Medium, HealthPalette.MediumSoft),
                _ => (HealthPalette.Error, HealthPalette.ErrorSoft),
            };

            SetColors(accent, soft);
            Progress = 100;
            ProgressText = "100%";
            OnPropertyChanged(nameof(IsWriting));
            OnPropertyChanged(nameof(IsReading));
        }

        public void MarkEmpty()
        {
            _phase = ChipTestPhase.Completed;
            Progress = 0;
            ProgressText = "—";
            PhaseText = "EMPTY";
            SetColors(HealthPalette.Muted, HealthPalette.MutedSoft);
        }

        private void SetColors(IBrush accent, IBrush soft)
        {
            AccentBrush = accent;
            FillBrush = soft;
            BadgeBrush = accent;
            BadgeSoftBrush = soft;
        }

        partial void OnProgressChanged(double value) => OnPropertyChanged(nameof(Ratio));
    }
}
