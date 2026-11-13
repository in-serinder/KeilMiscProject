using System;
using System.Collections.Generic;

namespace PDL_upper.Models
{
    /// <summary>单功率通道的实时统计。所有方法线程安全。</summary>
    public sealed class ChannelLive
    {
        private readonly object _gate = new();
        private readonly double[] _ring = new double[2048];
        private int _head, _count;

        private long _ticks;
        private double _vSum, _pSum, _pSumsq;

        public void OnTick(double vMv, double iMa)
        {
            double pW = vMv * iMa / 1e6;
            lock (_gate)
            {
                VMv = vMv; IMa = iMa; PW = pW;
                _ring[_head] = vMv;
                _head = (_head + 1) % _ring.Length;
                if (_count < _ring.Length) _count++;
                _ticks++;
                _vSum += vMv;
                _pSum += pW;
                _pSumsq += pW * pW;
            }
        }

        public void Reset()
        {
            lock (_gate)
            {
                _ticks = 0; _vSum = 0; _pSum = 0; _pSumsq = 0; _count = 0; _head = 0;
                VMv = IMa = PW = 0;
            }
        }

        public double VMv { get; private set; }
        public double IMa { get; private set; }
        public double PW { get; private set; }
        public long Ticks { get { lock (_gate) return _ticks; } }

        /// <summary>近窗口 Vpp（mV）—— 毛刺不能被平均掉，这是稳定性测量的核心。</summary>
        public (double vpp, double vmin, double vmax, double vavg) Window()
        {
            lock (_gate)
            {
                if (_count == 0) return (0, 0, 0, 0);
                double mn = double.MaxValue, mx = double.MinValue, sum = 0;
                for (int i = 0; i < _count; i++)
                {
                    double v = _ring[i];
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                    sum += v;
                }
                return (mx - mn, mn, mx, sum / _count);
            }
        }

        public (double pavg, double prms) PowerStats()
        {
            lock (_gate)
            {
                if (_ticks == 0) return (0, 0);
                double avg = _pSum / _ticks;
                double rms = Math.Sqrt(_pSumsq / _ticks);
                return (avg, rms);
            }
        }
    }

    public sealed class TempLive
    {
        private readonly object _gate = new();
        private short _c16 = short.MaxValue;          // 0x7FFF = 探头不存在
        public bool Present = true;

        // 迷你折线 / 温度窗口共用的历史（每来一个新温度值记一个点）
        private const int HistCap = 720;              // 温度 1 s 一点 → 约 12 分钟
        private readonly double[] _hist = new double[HistCap];
        private readonly long[] _histT = new long[HistCap];   // 每点设备时间戳 ms
        private long _lastT;
        private int _hHead, _hCount;

        public void Set(short c16) => Set(c16, _lastT + 1000);   // 无时间戳按 1 s 递推

        public void Set(short c16, long tMs)
        {
            lock (_gate)
            {
                _c16 = c16;
                Present = c16 != short.MaxValue;
                _hist[_hHead] = _c16 / 16.0;
                _histT[_hHead] = tMs;
                _lastT = tMs;
                _hHead = (_hHead + 1) % HistCap;
                if (_hCount < HistCap) _hCount++;
            }
        }

        /// <summary>每点的时间戳（ms，设备时钟，与值一一对应）。</summary>
        public double[] SnapshotT()
        {
            lock (_gate)
            {
                var outBuf = new double[_hCount];
                int start = (_hHead - _hCount + HistCap) % HistCap;
                for (int i = 0; i < _hCount; i++) outBuf[i] = _histT[(start + i) % HistCap];
                return outBuf;
            }
        }

        public double Celsius { get { lock (_gate) return _c16 / 16.0; } }
        public bool IsPresent { get { lock (_gate) return Present; } }

        /// <summary>按时间顺序的历史 ℃ 序列（迷你折线与温度窗口共用）。</summary>
        public double[] Snapshot()
        {
            lock (_gate)
            {
                var outBuf = new double[_hCount];
                int start = (_hHead - _hCount + HistCap) % HistCap;
                for (int i = 0; i < _hCount; i++) outBuf[i] = _hist[(start + i) % HistCap];
                return outBuf;
            }
        }
    }

    /// <summary>四通道曲线缓冲（电压，单位 V）。UI 按 33 ms 拉快照，不在数据线程里画。</summary>
    public sealed class ChartBuffer
    {
        public const int Capacity = 2400;
        private readonly object _gate = new();
        private readonly double[][] _buf = new double[4][];
        private readonly double[][] _ibuf = new double[4][];   // 电流（A），与电压同节拍
        private readonly int[] _head = new int[4];
        private readonly int[] _count = new int[4];

        public ChartBuffer()
        {
            for (int i = 0; i < 4; i++) { _buf[i] = new double[Capacity]; _ibuf[i] = new double[Capacity]; }
        }

        public void Add(int ch, double volts, double amps)
        {
            lock (_gate)
            {
                var b = _buf[ch];
                var bi = _ibuf[ch];
                b[_head[ch]] = volts;
                bi[_head[ch]] = amps;
                _head[ch] = (_head[ch] + 1) % Capacity;
                if (_count[ch] < Capacity) _count[ch]++;
            }
        }

        /// <summary>电流快照（A），长度与 Snapshot(ch) 一致。</summary>
        public double[] SnapshotI(int ch)
        {
            lock (_gate)
            {
                int n = _count[ch];
                if (n == 0) return Array.Empty<double>();
                var b = _ibuf[ch];
                var outBuf = new double[n];
                int start = (_head[ch] - n + Capacity) % Capacity;
                for (int i = 0; i < n; i++) outBuf[i] = b[(start + i) % Capacity];
                return outBuf;
            }
        }

        /// <summary>返回按时间顺序排列的副本；count==0 表示无数据。</summary>
        public double[] Snapshot(int ch)
        {
            lock (_gate)
            {
                int n = _count[ch];
                if (n == 0) return Array.Empty<double>();
                var b = _buf[ch];
                var outBuf = new double[n];
                int start = (_head[ch] - n + Capacity) % Capacity;
                for (int i = 0; i < n; i++) outBuf[i] = b[(start + i) % Capacity];
                return outBuf;
            }
        }

        public void Clear()
        {
            lock (_gate)
            {
                for (int i = 0; i < 4; i++) { _count[i] = 0; _head[i] = 0; }
            }
        }
    }
}

    /// <summary>
    /// 全时间历史（min/max 对，超限自动对半压缩——金字塔 L1 浏览层）。
    /// 每个点代表 bucket 个曲线节拍，压缩后翻倍；时间 = 序号 × bucket × 曲线节拍。
    /// </summary>
    public sealed class FullHistory
    {
        private readonly object _gate = new();
        private readonly List<double> _min = new();
        private readonly List<double> _max = new();
        private double _pendMin = double.MaxValue, _pendMax = double.MinValue;
        private int _pending;
        private int _bucket = 1;                 // 每点包含的原始曲线样本数
        private const int CompactLimit = 200_000;

        public int Bucket { get { lock (_gate) return _bucket; } }
        public long PointCount { get { lock (_gate) return _min.Count; } }
        public bool IsEmpty { get { lock (_gate) return _min.Count == 0 && _pending == 0; } }

        public void Add(double v)
        {
            lock (_gate)
            {
                if (v < _pendMin) _pendMin = v;
                if (v > _pendMax) _pendMax = v;
                if (++_pending >= _bucket)
                {
                    _min.Add(_pendMin);
                    _max.Add(_pendMax);
                    _pendMin = double.MaxValue; _pendMax = double.MinValue;
                    _pending = 0;
                    if (_min.Count > CompactLimit) Compact();
                }
            }
        }

        private void Compact()
        {
            int n = _min.Count / 2;
            for (int i = 0; i < n; i++)
            {
                _min[i] = Math.Min(_min[2 * i], _min[2 * i + 1]);
                _max[i] = Math.Max(_max[2 * i], _max[2 * i + 1]);
            }
            _min.RemoveRange(n, _min.Count - n);
            _max.RemoveRange(n, _max.Count - n);
            _bucket *= 2;
        }

        /// <summary>(mins, maxs)；时长（样本数）= PointCount × Bucket。</summary>
        public (double[] mins, double[] maxs) Snapshot()
        {
            lock (_gate)
            {
                var mins = _min.ToArray();
                var maxs = _max.ToArray();
                // 尾部未满桶
                if (_pending > 0 && _pendMin != double.MaxValue)
                {
                    Array.Resize(ref mins, mins.Length + 1);
                    Array.Resize(ref maxs, maxs.Length + 1);
                    mins[^1] = _pendMin;
                    maxs[^1] = _pendMax;
                }
                return (mins, maxs);
            }
        }

        public void Clear()
        {
            lock (_gate)
            {
                _min.Clear(); _max.Clear();
                _pendMin = double.MaxValue; _pendMax = double.MinValue;
                _pending = 0; _bucket = 1;
            }
        }
    
}
