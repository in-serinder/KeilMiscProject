using System;
using System.Diagnostics;

namespace Capsa.Services
{
    /// <summary>
    /// 串口传输的进度节流器：只有累计满 <see cref="Interval"/> 才允许向界面上报一次。
    /// 一个 8 KB 分片要发 512 个 16 B 包，若每包都刷新 UI，界面线程会被回调淹没，
    /// 表现就是“进度半天不动，随后一下拉满”。按时间节流后进度条才会匀速走。
    /// </summary>
    internal sealed class ProgressThrottle
    {
        private readonly long _intervalTicks;
        private long _lastTicks;

        public ProgressThrottle(TimeSpan interval)
        {
            Interval = interval;
            _intervalTicks = Math.Max(1, (long)(interval.TotalSeconds * Stopwatch.Frequency));
            _lastTicks = Stopwatch.GetTimestamp();
        }

        /// <summary>两次上报之间的最小间隔。</summary>
        public TimeSpan Interval { get; }

        /// <summary>已经过的包数（用于日志 / 调试）。</summary>
        public int Packets { get; private set; }

        /// <summary>
        /// 记一个包并判断是否到达上报时机。
        /// <paramref name="force"/> = true 时忽略时间间隔（用于分片开始 / 结束、整体完成）。
        /// </summary>
        public bool ShouldReport(bool force = false)
        {
            Packets++;

            var now = Stopwatch.GetTimestamp();
            if (!force && now - _lastTicks < _intervalTicks)
            {
                return false;
            }

            _lastTicks = now;
            return true;
        }
    }
}
