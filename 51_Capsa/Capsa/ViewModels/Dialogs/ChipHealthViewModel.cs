using System;
using System.Collections.Concurrent;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Threading;
using Capsa.Models;
using Capsa.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Capsa.ViewModels.Dialogs
{
    /// <summary>
    /// “芯片健康检查”窗口：8 张卡片同步展示写入（紫）/ 读取（绿）两段测试进度，
    /// 结束后按健康 / 警告 / 错误给卡片染色。
    /// </summary>
    public sealed partial class ChipHealthViewModel : ViewModelBase
    {
        private readonly IDeviceService _device;
        private readonly IChipTestService _testService;

        private CancellationTokenSource? _cancellation;
        private bool _testRan;

        // 整片测试每片要发 512 个工作包，逐包刷 UI 会让界面落后于硬件；
        // 这里后台只登记“每片最新进度”，由定时器按固定节奏取一次。
        private readonly ConcurrentDictionary<int, ChipTestProgress> _pending = new();
        private readonly DispatcherTimer _pumpTimer;
        private string _pendingAction = string.Empty;

        // 传输速率：8 片整片测试的字节量已知，按“已完成片 + 当前片百分比”折算总进度。
        private readonly TransferSpeedometer _speed = new();
        private readonly System.Collections.Generic.Dictionary<int, long> _chipOffsets = new();
        private long _testTotalBytes;
        private bool _testFullChip = true;

        /// <summary>显示在底部的传输速率；跑完变成平均速率。</summary>
        [ObservableProperty]
        private string _speedText = "—";

        public ChipHealthViewModel(string portName)
            : this(portName, AppServices.Device, AppServices.ChipTest)
        {
        }

        public ChipHealthViewModel(string portName, IDeviceService device, IChipTestService testService)
        {
            _device = device;
            _testService = testService;

            PortName = string.IsNullOrWhiteSpace(portName) ? "Not connected" : portName;

            for (var i = 0; i < 8; i++)
            {
                Slots.Add(new SlotTestViewModel(0x50 + i));
            }

            _pumpTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(200) };
            _pumpTimer.Tick += (_, _) => Pump();
        }

        public string PortName { get; }

        public string PortBadge => $"{PortName}  ·  115200 baud";

        public ObservableCollection<SlotTestViewModel> Slots { get; } = new();

        [ObservableProperty]
        private bool _isRunning;

        /// <summary>是否追加 0x00 图案的读写校验。</summary>
        [ObservableProperty]
        private bool _testPattern00 = true;

        [ObservableProperty]
        private string _actionText = "Ready to check chip health";

        [ObservableProperty]
        private string _summaryText = "8 slots · idle";

        public bool CanStart => !IsRunning;

        public string PrimaryButtonText => IsRunning ? "Testing…" : "Start test";

        public string SecondaryButtonText => IsRunning ? "Cancel" : "Close";

        /// <summary>窗口关闭请求，参数表示本次是否执行过测试。</summary>
        public event EventHandler<bool>? CloseRequested;

        /// <summary>本次会话是否跑过测试（用于调用方决定是否刷新主界面）。</summary>
        public bool TestRan => _testRan;

        /// <summary>窗口打开时读取一次槽位信息（地址 / 容量 / 在位状态）。</summary>
        public async Task LoadAsync()
        {
            try
            {
                var slots = await _device.ReadSlotsAsync().ConfigureAwait(true);

                for (var i = 0; i < Slots.Count; i++)
                {
                    Slots[i].Apply(i < slots.Count ? slots[i] : null);
                }

                var present = Slots.Count(s => s.IsPresent);
                SummaryText = present == 0
                    ? "No chip detected · idle"
                    : $"{present} of 8 slots populated · full-chip overwrite (destructive) · idle";
            }
            catch (Exception ex)
            {
                Capsa.Diagnostics.ErrorLog.Add("Chip health · slot read", ex.Message, ex);
                ActionText = $"Failed to read slots: {ex.Message}";
            }
        }

        [RelayCommand]
        private async Task StartTestAsync() => await RunTestAsync(fullChip: true).ConfigureAwait(true);

        /// <summary>
        /// 快速检查：只测每片前 256 字节（图案写入 + 读回比对），秒级完成。
        /// 适合频繁自检 —— 它同样是破坏性的（图案会覆写所测区域），清 0 逻辑与整片一致。
        /// </summary>
        [RelayCommand]
        private async Task QuickCheckAsync() => await RunTestAsync(fullChip: false).ConfigureAwait(true);

        private async Task RunTestAsync(bool fullChip)
        {
            if (IsRunning)
            {
                return;
            }

            IsRunning = true;
            _testRan = true;
            NotifyState();

            foreach (var slot in Slots)
            {
                if (slot.IsPresent)
                {
                    slot.Reset();
                }
            }

            _pending.Clear();
            _pendingAction = string.Empty;
            SummaryText = $"{Slots.Count(s => s.IsPresent)} of 8 slots · " +
                          (fullChip ? "full-chip overwrite running" : "quick check (first 256 B) running");
            PrepareSpeedometer(fullChip);
            _pumpTimer.Start();

            _cancellation = new CancellationTokenSource();
            var progress = new SnapshotProgress(this);

            // ★ 芯片测试是破坏性的（覆写图案）：测试一开始就抹掉占用记录，
            //   而不是等测试成功后再清 —— 中途取消 / 崩溃 / 断线时，卡上同样
            //   不能残留"还有完整数据"的假象。数据完整性在第一个图案字节
            //   写进去的那一刻就已经没了。
            try
            {
                await _device.SetUsedSpaceAsync(0).ConfigureAwait(true);
            }
            catch (Exception ex)
            {
                Capsa.Diagnostics.ErrorLog.Add(
                    "Chip health",
                    "Failed to clear used space before test — card may still show stale usage",
                    ex);
            }

            try
            {
                var report = await _testService
                    .RunAsync(
                        new ChipTestOptions
                        {
                            TestPattern00 = TestPattern00,
                            TestLengthBytes = fullChip ? 0 : 256,
                        },
                        progress,
                        _cancellation.Token)
                    .ConfigureAwait(true);

                ApplyReport(report);

                if (report.Canceled)
                {
                    // 取消：只保留已完成芯片的结论，不覆盖为“失败”
                    ActionText = "Test canceled";
                    SummaryText = "Test canceled · results are partial";
                    return;
                }

                // 已被图案覆盖，实际占用归零，避免主界面继续显示旧的使用量。
                await _device.SetUsedSpaceAsync(0).ConfigureAwait(true);
            }
            catch (OperationCanceledException)
            {
                _pending.Clear();
                _pendingAction = string.Empty;
                ActionText = "Test canceled";
                SummaryText = "Test canceled · results are partial";
            }
            catch (Exception ex)
            {
                Capsa.Diagnostics.ErrorLog.Add("Chip health · test", ex.Message, ex);
                ActionText = $"Test failed: {ex.Message}";
            }
            finally
            {
                Pump();
                _pumpTimer.Stop();
                _speed.Finish();
                SpeedText = _speed.AverageText;   // 结束后显示平均速率
                IsRunning = false;
                _cancellation?.Dispose();
                _cancellation = null;
                NotifyState();
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

            CloseRequested?.Invoke(this, _testRan);
        }

        /// <summary>定时把“每片最新进度”搬到界面，保证显示不落后于当前动作。</summary>
        private void Pump()
        {
            if (_pendingAction.Length > 0)
            {
                ActionText = _pendingAction;
            }

            foreach (var address in _pending.Keys)
            {
                if (_pending.TryRemove(address, out var progress))
                {
                    ApplyProgress(progress);
                }
            }
        }

        /// <summary>按 8 片实际容量建立“已完成字节”偏移表，并重置速率计。</summary>
        private void PrepareSpeedometer(bool fullChip)
        {
            _testFullChip = fullChip;
            _chipOffsets.Clear();

            long offset = 0;
            foreach (var slot in Slots)
            {
                if (!slot.IsPresent || slot.CapacityBytes <= 0)
                {
                    continue;
                }

                _chipOffsets[slot.Address] = offset;

                // 快速检查只测每片前 256 字节，速率折算按实际参与字节数来
                var tested = fullChip
                    ? slot.CapacityBytes
                    : Math.Min(256, slot.CapacityBytes);
                offset += tested;
            }

            _testTotalBytes = offset;

            _speed.Reset();
            SpeedText = "—";

            if (_testTotalBytes > 0)
            {
                _speed.Start(_testTotalBytes);
            }
        }

        /// <summary>把单片进度折算成整卡总进度后采样速率。</summary>
        private void SampleSpeed(ChipTestProgress progress, SlotTestViewModel slot)
        {
            if (_testTotalBytes <= 0 || slot.CapacityBytes <= 0)
            {
                return;
            }

            if (!_chipOffsets.TryGetValue(progress.Address, out var before))
            {
                return;
            }

            var chipPercent = progress.Phase == ChipTestPhase.Completed
                ? 100d
                : Math.Clamp(progress.Percent, 0d, 100d);

            // 快速检查只测每片前 256 字节：片内百分比按 256 折算，与 _chipOffsets 口径一致
            var tested = _testFullChip ? slot.CapacityBytes : Math.Min(256, slot.CapacityBytes);
            var done = before + tested * chipPercent / 100d;
            SpeedText = _speed.Sample(done * 100d / _testTotalBytes);
        }

        private void ApplyProgress(ChipTestProgress progress)
        {
            if (!IsRunning)
            {
                return;
            }


            var slot = Slots.FirstOrDefault(s => s.Address == progress.Address);
            if (slot is null)
            {
                return;
            }

            SampleSpeed(progress, slot);

            switch (progress.Phase)
            {
                case ChipTestPhase.Writing:
                case ChipTestPhase.Reading:
                    slot.EnterPhase(progress.Phase);
                    slot.SetProgress(progress.Percent);
                    break;

                case ChipTestPhase.Completed:
                    if (!slot.IsPresent)
                    {
                        slot.MarkEmpty();
                    }
                    else if (progress.Grade is { } grade)
                    {
                        slot.Complete(grade);
                    }

                    break;
            }
        }

        private void ApplyReport(ChipTestReport report)
        {
            // 丢弃尚未上屏的中间进度，避免覆盖刚算出的最终结论
            _pending.Clear();
            _pendingAction = string.Empty;

            foreach (var outcome in report.Outcomes)
            {
                var slot = Slots.FirstOrDefault(s => s.Address == outcome.Address);
                if (slot is null)
                {
                    continue;
                }

                if (outcome.Present)
                {
                    slot.Complete(outcome.Grade);
                }
                else
                {
                    slot.MarkEmpty();
                }
            }

            ActionText =
                $"Test complete · {report.HealthyCount} healthy · {report.WarningCount} warning · " +
                $"{report.MediumCount} medium · {report.ErrorCount} error";
            var chipBytes = Slots.FirstOrDefault(s => s.CapacityBytes > 0)?.CapacityBytes ?? 0;
            var scope = chipBytes > 0 ? $"{ByteSize.Format(chipBytes)}/chip · " : string.Empty;
            SummaryText = TestPattern00
                ? $"{scope}whole chip overwritten with 0x00 + 0xFF, read back and compared"
                : $"{scope}whole chip overwritten with 0xFF, read back and compared";
        }

        /// <summary>后台线程只登记最新值，不直接碰 UI。</summary>
        private sealed class SnapshotProgress : IProgress<ChipTestProgress>
        {
            private readonly ChipHealthViewModel _owner;

            public SnapshotProgress(ChipHealthViewModel owner) => _owner = owner;

            public void Report(ChipTestProgress value)
            {
                _owner._pending[value.Address] = value;
                Volatile.Write(ref _owner._pendingAction, value.Action);
            }
        }

        private void NotifyState()
        {
            OnPropertyChanged(nameof(CanStart));
            OnPropertyChanged(nameof(PrimaryButtonText));
            OnPropertyChanged(nameof(SecondaryButtonText));
        }
    }
}
