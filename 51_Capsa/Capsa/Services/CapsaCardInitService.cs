using System;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;
using Capsa.Services.Protocol;

namespace Capsa.Services
{
    /// <summary>
    /// 真实初始化流程：SET 写入元信息 → GET 回读校准 → 逐片读写测试 → 用健康掩码再次 SET。
    /// 写保护默认全关（WP = 00000000）。
    /// </summary>
    public sealed class CapsaCardInitService : ICardInitService
    {
        private readonly Func<CapsaProtocol?> _link;
        private readonly IChipTestService _chipTest;

        public CapsaCardInitService(Func<CapsaProtocol?> link, IChipTestService chipTest)
        {
            _link = link;
            _chipTest = chipTest;
        }

        public async Task<CardInitResult> InitializeAsync(
            CardInitRequest request,
            IProgress<CardInitProgress>? progress = null,
            CancellationToken cancellationToken = default)
        {
            var protocol = _link();
            if (protocol is null || !protocol.IsOpen)
            {
                throw new InvalidOperationException("Serial port is not connected");
            }

            var chipBytes = request.Chip.Bytes;
            var model = ParseModel(request.Chip.Name);
            var uid = RandomUid();
            var now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();

            // 1) 写入初始化信息（写保护默认全关，健康掩码先占位全 1）
            progress?.Report(new CardInitProgress(CardInitStage.WritingInfo, 4, "Writing initialization info"));

            var info = new CapsaInfo
            {
                Name = CapsaInfo.Sanitize(request.DeviceName),
                WriteProtectMask = 0x00,
                HealthMask = 0xFF,
                CapacityKb = chipBytes * 8 / 1024,
                ModelKbit = model,
                ModifiedAt = now,
                ReadAt = now,
                Medium = (int)request.Medium.Value,
                Uid = uid,
            };

            if (!protocol.WriteInfo(info))
            {
                throw new InvalidOperationException("SET rejected by device ([SET_ERR])");
            }

            progress?.Report(new CardInitProgress(CardInitStage.Calibrating, 30, "Calibrating parameters"));

            var readBack = protocol.ReadInfo();
            if (readBack is null)
            {
                throw new InvalidOperationException("GET failed after SET");
            }

            if (!string.Equals(readBack.Uid, uid, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException("UID read-back mismatch");
            }

            // 2) 逐片读写测试，得到健康掩码
            progress?.Report(new CardInitProgress(CardInitStage.TestingChips, 55, "Testing chips"));

            var chipProgress = new Progress<ChipTestProgress>(p =>
            {
                var index = Math.Clamp(p.Address - 0x50, 0, 7);
                var overall = 55 + ((index * 100d + Math.Clamp(p.Percent, 0, 100)) / 8d) * 0.33;
                progress?.Report(new CardInitProgress(CardInitStage.TestingChips, overall, p.Action));
            });

            var report = await _chipTest.RunAsync(
                new ChipTestOptions { TestPattern00 = true, TestLengthBytes = 256 },
                chipProgress,
                cancellationToken).ConfigureAwait(true);

            var healthy = report.Outcomes.Count(o => o.Present && o.Grade == ChipHealthGrade.Healthy);
            var healthMask = report.HealthMask;

            // 3) 回写健康掩码与阵列容量
            progress?.Report(new CardInitProgress(CardInitStage.ChipsPassed, 90, "Chip test passed"));

            info.HealthMask = healthMask;
            info.CapacityKb = chipBytes * Math.Max(healthy, 0) / 1024;
            info.ModifiedAt = DateTimeOffset.UtcNow.ToUnixTimeSeconds();

            if (!protocol.WriteInfo(info))
            {
                throw new InvalidOperationException("SET (health mask) rejected by device");
            }

            // 4) 已使用空间归零。
            // 首次 SET 会写 0x2C~0x31，但“对已初始化卡重新初始化”走的是后续 SET，
            // 协议规定它不碰该字段，所以必须显式发 UG_0，否则界面会继续显示旧占用。
            protocol.SetUsedBytes(0);

            // 5) 数据区已被读写测试覆写：旧哈希失效，清成 8 个 0。
            //    注意只清哈希——0x3A~0x3D 的累计写入次数是存储卡的寿命指标，
            //    重新初始化绝不清零（协议 SET / first_set 都不碰该区域）。
            protocol.SetDataInfo(new string('0', 16), null);

            progress?.Report(new CardInitProgress(CardInitStage.Completed, 100, "Initialization complete"));

            return new CardInitResult
            {
                DeviceName = info.Name,
                Chip = request.Chip,
                Medium = request.Medium,
                InitFlag = 1,
                HealthyChips = healthy,
                ArrayCapacityBytes = (long)chipBytes * healthy,
                Uid = uid,
                InitializedAt = DateTimeOffset.Now,
            };
        }

        /// <summary>"24C256" → 256（Kbit）。</summary>
        private static int ParseModel(string chipName)
        {
            // "24C256" -> 256（取 'C' 之后的部分）
            var index = chipName.IndexOf('C');
            var text = index >= 0 ? chipName[(index + 1)..] : chipName;

            return int.TryParse(text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var value) && value > 0
                ? value
                : 64;
        }

        /// <summary>协议 UID 为 6 字节（12 位十六进制）。</summary>
        private static string RandomUid()
        {
            var buffer = new byte[6];
            Random.Shared.NextBytes(buffer);
            return Convert.ToHexString(buffer);
        }
    }
}
