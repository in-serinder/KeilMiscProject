using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services
{
    /// <summary>
    /// 存储卡 / 芯片信息接口：读取设备信息、健康检查、重新初始化、格式化、
    /// 以及 8 个从地址（0x50-0x57）的单元状态。
    /// </summary>
    public interface IDeviceService
    {
        /// <summary>设备信息（未初始化时返回 null）。实现方保证在 UI 线程触发。</summary>
        event EventHandler<DeviceInfo?>? DeviceChanged;

        Task<DeviceInfo?> ReadDeviceInfoAsync(CancellationToken cancellationToken = default);

        /// <summary>健康检查：返回刷新后的设备信息（健康分会更新）。</summary>
        Task<DeviceInfo?> CheckHealthAsync(CancellationToken cancellationToken = default);

        /// <summary>重新初始化存储卡（重新握手 / 重新识别容量）。</summary>
        Task ReinitializeAsync(CancellationToken cancellationToken = default);

        /// <summary>
        /// 格式化存储卡：把所有可写空间写 0x00，占用归零。
        /// <paramref name="progress"/> 上报整体百分比与当前片。
        /// </summary>
        Task FormatAsync(IProgress<TransferProgress>? progress = null, CancellationToken cancellationToken = default);

        /// <summary>
        /// 翻转某片的写保护位（经 SET 更新 24C02 的 WP 掩码并下发 PCF8574）。
        /// 被保护片从有效容量中剔除，写入 / 读取会跳过它。
        /// </summary>
        Task<bool> SetWriteProtectBitAsync(int chipAddress, bool protect, CancellationToken cancellationToken = default);

        /// <summary>读取 0x50 - 0x57 共 8 个单元的状态。</summary>
        Task<IReadOnlyList<SlotStatus>> ReadSlotsAsync(CancellationToken cancellationToken = default);

        /// <summary>
        /// 用 <c>SET_</c>（9 字段）把整张卡的属性写回 24C02：
        /// 名称 / WP 掩码 / 健康掩码 / 阵列容量 / 型号 / 修改时间 / 读取时间 / 介质 / UID。
        /// 格式化前后各用一次 —— 未初始化的卡靠它建立属性（否则固件拒绝开写入会话），
        /// 已初始化的卡靠它刷新时间戳。健康掩码与 WP 掩码以卡上现有值为准，不被覆盖。
        /// </summary>
        Task<bool> ConfigureCardAsync(Protocol.CapsaInfo config, CancellationToken cancellationToken = default);

        /// <summary>
        /// 通过 UG 命令更新 24C02 中的“当前已使用空间”（单位 B）。
        /// 由上位机在写入完成后计算并上报，下位机只存储、不统计。
        /// </summary>
        Task<bool> SetUsedSpaceAsync(long usedBytes, CancellationToken cancellationToken = default);

        /// <summary>
        /// 通过 DI 命令写入整卡数据哈希（16 位十六进制）与累计写入次数。
        /// 哈希由上位机计算、次数由上位机 +1，下位机只存储。
        /// </summary>
        Task<bool> SetDataInfoAsync(string hashHex, long writeCount, CancellationToken cancellationToken = default);
    }
}
