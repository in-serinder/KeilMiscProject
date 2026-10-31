using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;
using Capsa.Services.Protocol;

namespace Capsa.Services
{
    /// <summary>
    /// 真实设备服务：GET_! 读取 24C02 元信息，UGS 读取已使用空间，
    /// 展开为 <see cref="DeviceInfo"/> 与 8 个槽位状态。
    /// </summary>
    public sealed class CapsaDeviceService : IDeviceService
    {
        private readonly Func<CapsaProtocol?> _link;

        public CapsaDeviceService(Func<CapsaProtocol?> link)
        {
            _link = link;
        }

        public event EventHandler<DeviceInfo?>? DeviceChanged;

        public Task<DeviceInfo?> ReadDeviceInfoAsync(CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                var protocol = _link();
                if (protocol is null || !protocol.IsOpen)
                {
                    return null;
                }

                var info = protocol.ReadInfo();
                if (info is null)
                {
                    // 未初始化（GET 返回 [SET_ERR]）
                    DeviceChanged?.Invoke(this, null);
                    return null;
                }

                // 已使用空间由 UGS 单独读取；读不到（老固件）按 0 处理。
                var used = protocol.ReadUsedBytes() ?? 0;
                var device = ToDeviceInfo(info, used);
                DeviceChanged?.Invoke(this, device);
                return device;
            }, cancellationToken);
        }

        public Task<DeviceInfo?> CheckHealthAsync(CancellationToken cancellationToken = default)
            => ReadDeviceInfoAsync(cancellationToken);

        public Task ReinitializeAsync(CancellationToken cancellationToken = default)
            => ReadDeviceInfoAsync(cancellationToken);

        public Task FormatAsync(CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                var protocol = _link();
                if (protocol is null || !protocol.IsOpen)
                {
                    return;
                }

                var info = protocol.ReadInfo();
                var chipBytes = info?.ChipBytes ?? 8192;
                var blank = new byte[chipBytes];
                Array.Fill(blank, (byte)0xFF);

                for (var chip = 0x50; chip <= 0x57; chip++)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    protocol.WriteMemory(chip, blank, null, cancellationToken);
                }

                // 全部擦成 0xFF 后，已使用空间归零。
                protocol.SetUsedBytes(0);
            }, cancellationToken);
        }

        public Task<IReadOnlyList<SlotStatus>> ReadSlotsAsync(CancellationToken cancellationToken = default)
        {
            return Task.Run<IReadOnlyList<SlotStatus>>(() =>
            {
                var protocol = _link();
                var info = protocol?.ReadInfo();
                var chipBytes = info?.ChipBytes ?? 8192;
                var totalUsed = info is null ? 0 : protocol?.ReadUsedBytes() ?? 0;

                var slots = new List<SlotStatus>(8);

                for (var i = 0; i < 8; i++)
                {
                    var address = 0x50 + i;
                    var protectedBit = info is not null && (info.WriteProtectMask & (1 << i)) != 0;
                    var healthy = info is null || (info.HealthMask & (1 << i)) != 0;

                    slots.Add(new SlotStatus
                    {
                        Address = address,
                        Present = info is not null,
                        CapacityBytes = chipBytes,
                        UsedBytes = info is null ? 0 : SliceUsedBytes(totalUsed, i, chipBytes),
                        HealthScore = info is null ? 0 : (healthy ? 100 : 30),
                        WriteProtected = protectedBit,
                        WriteEnableLatch = !protectedBit,
                    });
                }

                return slots;
            }, cancellationToken);
        }

        public Task<bool> SetUsedSpaceAsync(long usedBytes, CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                var protocol = _link();
                return protocol is { IsOpen: true } && protocol.SetUsedBytes(usedBytes);
            }, cancellationToken);
        }

        /// <summary>
        /// 把整卡已使用字节数按地址顺序切给第 <paramref name="index"/> 片：
        /// 0x50 最先写满，最后一片可能是部分占用。
        /// </summary>
        private static int SliceUsedBytes(long totalUsed, int index, int chipBytes)
        {
            if (chipBytes <= 0)
            {
                return 0;
            }

            var remaining = totalUsed - (long)index * chipBytes;
            return (int)Math.Clamp(remaining, 0, chipBytes);
        }

        private static DeviceInfo ToDeviceInfo(CapsaInfo info, long usedBytes)
        {
            var chipBytes = info.ChipBytes;
            var arrayCapacity = info.CapacityKb > 0 ? info.CapacityKb * 1024 : chipBytes * 8;

            return new DeviceInfo
            {
                DeviceName = string.IsNullOrWhiteSpace(info.Name) ? "CAPSA" : info.Name,
                Uid = info.Uid,
                ChipModel = $"24C{info.ModelKbit}",
                ChipType = info.MediumName,
                CapacityBytes = chipBytes,
                ArrayCapacityBytes = arrayCapacity,
                UsedBytes = (int)Math.Clamp(usedBytes, 0, Math.Min(int.MaxValue, arrayCapacity)),
                LastWriteTime = ToTime(info.ModifiedAt),
                LastAccessTime = ToTime(info.ReadAt),
                HealthScore = info.HealthyChips * 100 / 8,
            };
        }

        private static DateTimeOffset ToTime(long unixSeconds)
            => unixSeconds > 0
                ? DateTimeOffset.FromUnixTimeSeconds(unixSeconds).ToLocalTime()
                : DateTimeOffset.MinValue;
    }
}
