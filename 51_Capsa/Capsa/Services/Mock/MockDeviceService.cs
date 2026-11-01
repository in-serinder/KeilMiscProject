using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services.Mock
{
    /// <summary>
    /// 设备信息 Mock：返回一块 24C256 的示例数据，并维护 8 个单元的占用 / 健康状态。
    /// 真实实现替换点为 <see cref="AppServices.Device"/>。
    /// </summary>
    public sealed class MockDeviceService : IDeviceService
    {
        private const int CapacityBytes = 32 * 1024;
        private const int ArrayCapacityBytes = CapacityBytes * 8;
        private static readonly int[] SlotUsed = { 12288, 4096, 0, 20480, 8192, 0, 3072, 16384 };
        private static readonly int[] SlotHealth = { 92, 74, 0, 88, 55, 0, 81, 96 };
        private static readonly int[] SlotCapacity = { 32768, 8192, 0, 32768, 16384, 0, 4096, 32768 };
        private static readonly bool[] SlotWriteProtected = { false, true, false, false, true, false, false, false };
        private static readonly bool[] SlotWriteEnable = { true, false, false, true, false, false, true, true };

        private DeviceInfo? _current;
        private bool _formatted;
        private long? _usedOverride;
        private string _dataHash = "A1B2C3D4E5F60708";
        private long _writeCount = 12;

        public event EventHandler<DeviceInfo?>? DeviceChanged;

        public async Task<DeviceInfo?> ReadDeviceInfoAsync(CancellationToken cancellationToken = default)
        {
            await Task.Delay(260, cancellationToken).ConfigureAwait(true);
            return Publish(BuildInfo());
        }

        public async Task<DeviceInfo?> CheckHealthAsync(CancellationToken cancellationToken = default)
        {
            await Task.Delay(700, cancellationToken).ConfigureAwait(true);

            // 模拟健康检查后分数小幅波动。
            var baseline = _current ?? BuildInfo();
            var jitter = Random.Shared.Next(-3, 4);
            var score = Math.Clamp(baseline.HealthScore + jitter, 0, 100);

            return Publish(BuildInfo(score));
        }

        public async Task ReinitializeAsync(CancellationToken cancellationToken = default)
        {
            await Task.Delay(900, cancellationToken).ConfigureAwait(true);
            _formatted = false;

            // 重新初始化：数据被清掉，已用空间归零、旧哈希失效（真实固件由 UG_0 完成）。
            // 累计写入次数不清——它是存储卡的寿命指标，任何初始化都不应篡改。
            _usedOverride = 0;
            _dataHash = string.Empty;
            Publish(BuildInfo(usedBytes: 0));
        }

        public async Task FormatAsync(CancellationToken cancellationToken = default)
        {
            await Task.Delay(1100, cancellationToken).ConfigureAwait(true);
            _formatted = true;
            _usedOverride = 0;

            // 格式化只擦数据区：哈希随数据一起失效，累计写入次数保留。
            _dataHash = string.Empty;
            Publish(BuildInfo(usedBytes: 0));
        }

        public async Task<IReadOnlyList<SlotStatus>> ReadSlotsAsync(CancellationToken cancellationToken = default)
        {
            await Task.Delay(120, cancellationToken).ConfigureAwait(true);
            return BuildSlots();
        }

        public Task<bool> SetDataInfoAsync(string hashHex, long writeCount, CancellationToken cancellationToken = default)
        {
            var raw = (hashHex ?? string.Empty).Trim().ToUpperInvariant();
            // 全 0 哈希是“已清空”的占位值，统一按空处理
            _dataHash = DataIntegrity.IsNoRecordHash(raw) ? string.Empty : raw;
            _writeCount = Math.Max(0, writeCount);

            // 有数据重新落盘后，格式化标记失效，哈希应恢复显示
            if (!string.IsNullOrWhiteSpace(_dataHash))
            {
                _formatted = false;
            }

            Publish(BuildInfo());
            return Task.FromResult(true);
        }

        public Task<bool> SetUsedSpaceAsync(long usedBytes, CancellationToken cancellationToken = default)
        {
            _usedOverride = Math.Max(0, usedBytes);
            Publish(BuildInfo());
            return Task.FromResult(true);
        }

        private DeviceInfo BuildInfo(int? healthScore = null, int? usedBytes = null)
        {
            var now = DateTimeOffset.Now;
            var used = usedBytes ?? CurrentUsedBytes();

            return new DeviceInfo
            {
                DeviceName = "Capsa Storage Card",
                Uid = "3F9C-E1D4-77B2-0A58",
                ChipModel = "AT24C256",
                ChipType = "I2C EEPROM",
                CapacityBytes = CapacityBytes,
                ArrayCapacityBytes = ArrayCapacityBytes,
                UsedBytes = used,
                LastWriteTime = now.AddMinutes(-42),
                LastAccessTime = now.AddMinutes(-3),
                HealthScore = healthScore ?? 92,
                DataHash = _formatted ? string.Empty : _dataHash,
                // 写入次数是寿命指标：格式化 / 重新初始化都不归零
                WriteCount = _writeCount,
            };
        }

        private int CurrentUsedBytes()
        {
            if (_formatted)
            {
                return 0;
            }

            if (_usedOverride is { } overrideValue)
            {
                return (int)Math.Clamp(overrideValue, 0, ArrayCapacityBytes);
            }

            // 未上报过 UG 时用各单元占用之和作为示例值。
            var sum = 0;
            foreach (var value in SlotUsed)
            {
                sum += value;
            }

            return sum;
        }

        private SlotStatus[] BuildSlots()
        {
            var slots = new SlotStatus[8];
            var totalUsed = CurrentUsedBytes();

            for (var i = 0; i < slots.Length; i++)
            {
                var present = SlotCapacity[i] > 0;
                var used = !present
                    ? 0
                    : _usedOverride is not null
                        // 有 UG 上报值时按地址顺序切分，与真实设备一致。
                        ? (int)Math.Clamp(totalUsed - (long)i * CapacityBytes, 0, SlotCapacity[i])
                        : SlotUsed[i];
                var health = !present ? 0 : (_formatted ? 100 : SlotHealth[i]);

                slots[i] = new SlotStatus
                {
                    Address = 0x50 + i,
                    Present = present,
                    CapacityBytes = SlotCapacity[i],
                    UsedBytes = used,
                    HealthScore = health,
                    WriteProtected = present && !_formatted && SlotWriteProtected[i],
                    WriteEnableLatch = present && !_formatted && SlotWriteEnable[i],
                };
            }

            return slots;
        }

        private DeviceInfo Publish(DeviceInfo info)
        {
            _current = info;
            DeviceChanged?.Invoke(this, info);
            return info;
        }
    }
}
