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
                    UiThread.Post(() => DeviceChanged?.Invoke(this, null));
                    return null;
                }

                // 已使用空间由 UGS 单独读取；读不到（老固件）按 0 处理。
                var used = protocol.ReadUsedBytes() ?? 0;

                // 数据哈希 / 写入次数由 DIG 单独读取（老固件不支持时留空）。
                var dataInfo = protocol.ReadDataInfo();

                var device = ToDeviceInfo(info, used, dataInfo);
                UiThread.Post(() => DeviceChanged?.Invoke(this, device));
                return device;
            }, cancellationToken);
        }

        public Task<DeviceInfo?> CheckHealthAsync(CancellationToken cancellationToken = default)
            => ReadDeviceInfoAsync(cancellationToken);

        public Task ReinitializeAsync(CancellationToken cancellationToken = default)
            => ReadDeviceInfoAsync(cancellationToken);

        public Task FormatAsync(IProgress<TransferProgress>? progress = null, CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                var protocol = _link();
                if (protocol is null || !protocol.IsOpen)
                {
                    throw new InvalidOperationException("Serial port is not connected");
                }

                var info = protocol.ReadInfo();
                var chipBytes = info?.ChipBytes ?? 8192;
                var mask = info?.WriteProtectMask ?? 0;

                // 格式化 = 所有可写空间写 0x00；写保护片跳过（写不进去）。
                var chips = new List<int>();
                for (var c = 0x50; c <= 0x57; c++)
                {
                    if ((mask & (1 << (c - 0x50))) == 0)
                    {
                        chips.Add(c);
                    }
                }

                var blank = new byte[chipBytes];
                Array.Fill(blank, (byte)0x00);

                for (var i = 0; i < chips.Count; i++)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    var chip = chips[i];

                    var ok = protocol.WriteMemory(
                        chip,
                        blank,
                        (current, _) =>
                        {
                            // 片内进度映射到整体：整体 = (i + current/100) / n * 92
                            var overall = (i * 100d + Math.Clamp(current, 0, 100)) / chips.Count * 92d;
                            progress?.Report(new TransferProgress(
                                overall,
                                $"Formatting 0x{chip:X2}… {current:0}%",
                                chip,
                                (int)(current / 100d * chipBytes)));
                        },
                        cancellationToken);

                    if (!ok)
                    {
                        throw new InvalidOperationException($"Format failed at 0x{chip:X2}");
                    }

                    progress?.Report(new TransferProgress(
                        (i + 1) * 92d / chips.Count,
                        $"0x{chip:X2} formatted",
                        chip,
                        chipBytes));
                }

                // 已使用空间归零。
                protocol.SetUsedBytes(0);

                // 数据已擦除（全 0x00），旧哈希失效：清成 8 个 0，但累计写入次数保留
                // （次数是存储卡寿命指标，格式化 / 重新初始化都不能把它抹掉）。
                protocol.SetDataInfo(new string('0', 16), null);

                progress?.Report(new TransferProgress(100, "Format complete"));
            }, cancellationToken);
        }

        public Task<bool> SetWriteProtectBitAsync(int chipAddress, bool protect, CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                var protocol = _link();
                return protocol is { IsOpen: true } && protocol.SetWriteProtectBit(chipAddress, protect);
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

        public Task<bool> SetDataInfoAsync(string hashHex, long writeCount, CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                var protocol = _link();
                return protocol is { IsOpen: true } && protocol.SetDataInfo(hashHex, writeCount);
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

        public Task<bool> ConfigureCardAsync(CapsaInfo config, CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                var protocol = _link();
                if (protocol is not { IsOpen: true })
                {
                    return false;
                }

                // 读-改-写：健康掩码 / WP 掩码以卡上现有值为准。
                // 格式化不是健康检查，不能顺手把所有片子"修复"成健康；
                // WP 是 PCF8574 的硬件状态，也不能被一次 SET 抹掉。
                var current = protocol.ReadInfo();
                if (current is not null)
                {
                    config.HealthMask = current.HealthMask;
                    config.WriteProtectMask = current.WriteProtectMask;
                    if (string.IsNullOrWhiteSpace(config.Name))
                    {
                        config.Name = current.Name;
                    }
                    if (string.IsNullOrWhiteSpace(config.Uid))
                    {
                        config.Uid = current.Uid;
                    }
                }

                return protocol.WriteInfo(config);
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

        private static DeviceInfo ToDeviceInfo(
            CapsaInfo info,
            long usedBytes,
            (string Hash, long WriteCount)? dataInfo = null)
        {
            var chipBytes = info.ChipBytes;

            // ★ ArrayCapacityBytes 必须是硬件总量（8 片口径），不能用 24C02 的 cap_kb：
            //   固件现在把 cap_kb 维护成"有效容量"（可写片 × 单片，随 WP 实时重算），
            //   而 DeviceInfo.ArrayTotalBytes 是占用百分比 / 进度条黄蓝两段的基准
            //   （永远 8 片），有效容量由 ArrayEffectiveBytes 自己扣减 ——
            //   两个口径混用会出现双重扣除。cap_kb 仅作交叉校验，不进容量字段。
            return new DeviceInfo
            {
                DeviceName = string.IsNullOrWhiteSpace(info.Name) ? "CAPSA" : info.Name,
                Uid = info.Uid,
                ChipModel = $"24C{info.ModelKbit}",
                ChipType = info.MediumName,
                CapacityBytes = chipBytes,
                ArrayCapacityBytes = chipBytes * 8,
                UsedBytes = (int)Math.Clamp(usedBytes, 0, Math.Min(int.MaxValue, chipBytes * 8)),
                LastWriteTime = ToTime(info.ModifiedAt),
                LastAccessTime = ToTime(info.ReadAt),
                HealthScore = info.HealthyChips * 100 / 8,
                WriteProtectMask = info.WriteProtectMask,
                // 全 0 哈希是格式化 / 重新初始化后的占位值，视为“无记录”
                DataHash = DataIntegrity.IsNoRecordHash(dataInfo?.Hash) ? string.Empty : dataInfo!.Value.Hash,
                WriteCount = dataInfo?.WriteCount ?? 0,
            };
        }

        private static DateTimeOffset ToTime(long unixSeconds)
            => unixSeconds > 0
                ? DateTimeOffset.FromUnixTimeSeconds(unixSeconds).ToLocalTime()
                : DateTimeOffset.MinValue;
    }
}
