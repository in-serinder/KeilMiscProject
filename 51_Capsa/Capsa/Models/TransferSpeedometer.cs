using System;
using System.Diagnostics;

namespace Capsa.Models
{
    /// <summary>
    /// 传输速率计：用「总进度百分比 + 经过时间」估算字节速率。
    /// <para>
    /// 实时值做指数平滑，避免串口 150 ms 一报让数字剧烈跳动；
    /// <see cref="Start"/> 会把上一阶段（如“写入”）计入累计，<see cref="Finish"/> 收尾后再取
    /// <see cref="AverageText"/> 即为整次传输（含回读校验）的平均速率。
    /// </para>
    /// 各阶段进度都会从 0 重新算起，这里把指针回退识别为“新阶段”而不是负速率。
    /// </summary>
    public sealed class TransferSpeedometer
    {
        private readonly Stopwatch _watch = new();

        /// <summary>平滑系数：新样本占 35%，历史占 65%。</summary>
        private const double NewSampleWeight = 0.35;

        private long _totalBytes;
        private double _lastBytes;
        private double _lastSeconds;
        private double _smoothed;       // 当前阶段的平滑速率（字节/秒）

        private double _bankedBytes;    // 已完成阶段的字节数
        private double _bankedSeconds;  // 已完成阶段的耗时

        public bool IsRunning => _watch.IsRunning;

        /// <summary>清空累计（每次新的传输任务开始时调用）。</summary>
        public void Reset()
        {
            _watch.Stop();
            _bankedBytes = 0;
            _bankedSeconds = 0;
            _lastBytes = 0;
            _lastSeconds = 0;
            _smoothed = 0;
        }

        /// <summary>
        /// 开始（或开始下一阶段）测量。<paramref name="totalBytes"/> 是本阶段要传输的总字节数；
        /// 上一阶段的数据会先计入累计，供 <see cref="AverageText"/> 使用。
        /// </summary>
        public void Start(long totalBytes)
        {
            Bank();

            _totalBytes = Math.Max(0, totalBytes);
            _lastBytes = 0;
            _lastSeconds = 0;
            _smoothed = 0;
            _watch.Restart();
        }

        /// <summary>结束测量：把当前阶段计入累计并停表。</summary>
        public void Finish()
        {
            if (!_watch.IsRunning)
            {
                return;
            }

            Bank();
            _watch.Stop();
        }

        /// <summary>整次传输的平均速率（字节/秒）。</summary>
        public double AverageBytesPerSecond
            => _bankedSeconds <= 0 ? 0 : _bankedBytes / _bankedSeconds;

        /// <summary>完成后显示的平均速率，如 <c>avg 1.42 KB/s</c>；数据不足时返回 <c>—</c>。</summary>
        public string AverageText
            => AverageBytesPerSecond < 1
                ? "—"
                : $"avg {ByteSize.Format((long)Math.Round(AverageBytesPerSecond))}/s";

        /// <summary>
        /// 用总进度百分比采样，返回形如 <c>12.4 KB/s</c> 的文本；
        /// 数据不足（刚开始 / 速率为 0）时返回 <c>—</c>。
        /// </summary>
        public string Sample(double percent)
        {
            if (!_watch.IsRunning || _totalBytes <= 0)
            {
                return "—";
            }

            var now = _watch.Elapsed.TotalSeconds;
            var bytes = _totalBytes * Math.Clamp(percent, 0d, 100d) / 100d;
            var delta = bytes - _lastBytes;
            var dt = now - _lastSeconds;

            if (delta < 0)
            {
                // 阶段切换（写入 0-100 → 校验 0-100）：指针归零不算负速率
                delta = 0;
            }

            _lastBytes = bytes;
            _lastSeconds = now;

            if (dt > 0.02)
            {
                var instant = delta / dt;
                _smoothed = _smoothed <= 0
                    ? instant
                    : _smoothed * (1 - NewSampleWeight) + instant * NewSampleWeight;
            }

            if (_smoothed < 1)
            {
                return "—";
            }

            return ByteSize.Format((long)Math.Round(_smoothed)) + "/s";
        }

        /// <summary>把当前阶段的数据计入累计。</summary>
        private void Bank()
        {
            if (!_watch.IsRunning)
            {
                return;
            }

            var seconds = _watch.Elapsed.TotalSeconds;
            if (seconds <= 0)
            {
                return;
            }

            _bankedBytes += _lastBytes;
            _bankedSeconds += seconds;
        }
    }
}
