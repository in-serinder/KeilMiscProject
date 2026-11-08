using System;
using Avalonia.Media;
using Capsa.Models;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Capsa.ViewModels.Panels
{
    /// <summary>单个存储单元卡片（0x50 - 0x57）。</summary>
    public sealed partial class ChipSlotViewModel : ViewModelBase
    {
        public ChipSlotViewModel(int address)
        {
            Address = address;
        }

        public int Address { get; }

        public string AddressText => $"0x{Address:X2}";

        [ObservableProperty]
        private bool _isPresent;

        [ObservableProperty]
        private double _occupancyPercent;

        [ObservableProperty]
        private int _healthScore;

        [ObservableProperty]
        private int _capacityBytes;

        [ObservableProperty]
        private int _usedBytes;

        /// <summary>写保护标志 WP。</summary>
        [ObservableProperty]
        private bool _writeProtected;

        /// <summary>写使能锁存 WEL。</summary>
        [ObservableProperty]
        private bool _writeEnableLatch;

        public HealthLevel HealthLevel => IsPresent ? HealthScale.FromScore(HealthScore) : HealthLevel.Error;

        public string HealthText => HealthScale.ToBadgeText(HealthLevel);

        public IBrush HealthBrush => HealthScale.ToBrush(HealthLevel);

        public IBrush HealthSoftBrush => HealthScale.ToSoftBrush(HealthLevel);

        public string OccupancyText => IsPresent ? ByteSize.Percent(OccupancyPercent) : "--";

        /// <summary>0 - 1 的占用比例，用于卡片整块填充的横向缩放。</summary>
        public double OccupancyRatio => Math.Clamp(OccupancyPercent / 100d, 0d, 1d);

        /// <summary>
        /// 卡片占用填充色：被写保护的芯片整块变黄（容量被剔出有效空间），
        /// 否则保持蓝色。
        /// </summary>
        public IBrush CardFillBrush => WriteProtected ? HealthPalette.Warn : HealthPalette.CardFill;

        /// <summary>卡片底色层：保护时整块铺淡黄（与占用多少无关），否则透明。</summary>
        public IBrush CardBackgroundBrush => WriteProtected ? HealthPalette.CardProtectedBg : Brushes.Transparent;

        // ---- 读取可视化：绿色覆盖层 ----

        /// <summary>读取模式下显示绿色覆盖层；退出读取模式后隐藏。</summary>
        [ObservableProperty]
        private bool _isReadActive;

        /// <summary>该片已读出的百分比（0-100）。</summary>
        [ObservableProperty]
        private double _readPercent;

        /// <summary>0 - 1 的读取进度，用于绿色覆盖层的横向缩放。</summary>
        public double ReadRatio => Math.Clamp(ReadPercent / 100d, 0d, 1d);

        /// <summary>读取到该片时按字节数刷新绿色覆盖层。</summary>
        public void SetReadBytes(int readBytes)
        {
            var bytes = Math.Clamp(readBytes, 0, Math.Max(0, CapacityBytes));
            ReadPercent = CapacityBytes > 0 ? Math.Round(bytes * 100d / CapacityBytes, 1) : 0;
        }

        public string CapacityText => IsPresent
            ? $"{ByteSize.Format(UsedBytes)} / {ByteSize.Format(CapacityBytes)}"
            : "No chip";

        // ---- 写保护 / 写使能 ----

        public IBrush WpBrush => WriteProtected ? HealthPalette.Warn : HealthPalette.Muted;

        public IBrush WpSoftBrush => WriteProtected ? HealthPalette.WarnSoft : HealthPalette.MutedSoft;

        public IBrush WelBrush => WriteEnableLatch ? HealthPalette.Ok : HealthPalette.Muted;

        public IBrush WelSoftBrush => WriteEnableLatch ? HealthPalette.OkSoft : HealthPalette.MutedSoft;

        public string WpTooltip => WriteProtected
            ? "WP = 1 · write protected"
            : "WP = 0 · writable";

        public string WelTooltip => WriteEnableLatch
            ? "WEL = 1 · write enable latch set"
            : "WEL = 0 · write enable latch clear";

        public string WpToggleTooltip => WriteProtected
            ? "Click to clear write protection (WP → 0)"
            : "Click to write-protect this chip (WP → 1)";

        // ---- WP 点击切换 ----

        /// <summary>WP 徽章被点击（由 ChipBoardViewModel 汇总给上层处理）。</summary>
        public event Action<ChipSlotViewModel>? WpToggleRequested;

        [RelayCommand]
        private void ToggleWp()
        {
            if (IsPresent)
            {
                WpToggleRequested?.Invoke(this);
            }
        }

        public void Apply(SlotStatus? status)
        {
            if (status is null)
            {
                Reset();
                return;
            }

            IsPresent = status.Present;
            OccupancyPercent = status.OccupancyPercent;
            HealthScore = status.HealthScore;
            CapacityBytes = status.CapacityBytes;
            UsedBytes = status.UsedBytes;
            WriteProtected = status.WriteProtected;
            WriteEnableLatch = status.WriteEnableLatch;

            RaiseDerived();
        }

        /// <summary>
        /// 写入过程中的实时占用：卡片进度条跟着该片已写字节数移动，
        /// 全部写完后由 UGS 读回的正式值覆盖。
        /// </summary>
        public void SetLiveBytes(int writtenBytes)
        {
            var bytes = Math.Clamp(writtenBytes, 0, Math.Max(0, CapacityBytes));
            UsedBytes = bytes;
            OccupancyPercent = CapacityBytes > 0
                ? Math.Round(bytes * 100d / CapacityBytes, 1)
                : 0;
        }

        public void Reset()
        {
            IsPresent = false;
            OccupancyPercent = 0;
            HealthScore = 0;
            CapacityBytes = 0;
            UsedBytes = 0;
            WriteProtected = false;
            WriteEnableLatch = false;
            RaiseDerived();
        }

        partial void OnIsPresentChanged(bool value) => RaiseDerived();

        partial void OnHealthScoreChanged(int value) => RaiseDerived();

        partial void OnOccupancyPercentChanged(double value) => RaiseDerived();

        partial void OnCapacityBytesChanged(int value) => RaiseDerived();

        partial void OnUsedBytesChanged(int value) => RaiseDerived();

        partial void OnReadPercentChanged(double value) => OnPropertyChanged(nameof(ReadRatio));

        partial void OnWriteProtectedChanged(bool value) => RaiseRaiseWriteFlags();

        partial void OnWriteEnableLatchChanged(bool value) => RaiseRaiseWriteFlags();

        private void RaiseRaiseWriteFlags()
        {
            OnPropertyChanged(nameof(WpBrush));
            OnPropertyChanged(nameof(CardFillBrush));
            OnPropertyChanged(nameof(WpToggleTooltip));
            OnPropertyChanged(nameof(WpSoftBrush));
            OnPropertyChanged(nameof(WelBrush));
            OnPropertyChanged(nameof(WelSoftBrush));
            OnPropertyChanged(nameof(WpTooltip));
            OnPropertyChanged(nameof(WelTooltip));
        }

        private void RaiseDerived()
        {
            OnPropertyChanged(nameof(HealthLevel));
            OnPropertyChanged(nameof(HealthText));
            OnPropertyChanged(nameof(HealthBrush));
            OnPropertyChanged(nameof(HealthSoftBrush));
            OnPropertyChanged(nameof(OccupancyText));
            OnPropertyChanged(nameof(OccupancyRatio));
            RaiseRaiseWriteFlags();
            OnPropertyChanged(nameof(CapacityText));
        }
    }
}
