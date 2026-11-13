using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using PDL_upper.Models.Protocol;
using PDL_upper.Models.Transport;

namespace PDL_upper.Models
{
    /// <summary>
    /// 会话层：命令串行、SEQ 匹配、超时重试、事件与数据块分流。
    /// 规则与文档一致：ABORT 超时 2 s（设备要先排空缓冲），其余 200 ms × 3。
    /// </summary>
    public sealed class DeviceSession : IDisposable
    {
        private readonly ITransport _tp;
        private readonly FrameDecoder _dec = new();
        private readonly SemaphoreSlim _cmdGate = new(1, 1);
        private readonly ConcurrentDictionary<ushort, TaskCompletionSource<Frame>> _pending = new();
        private Thread? _ioThread;
        private volatile bool _run;
        private ushort _seq;
        private readonly byte[] _rx = new byte[4096];

        // 块重组
        private readonly List<byte> _blk = new();
        private bool _inBlock;

        public event Action<byte[]>? BlockPayload;      // 完整块载荷（首片头 + 数据）
        public event Action<PdlEvent>? EventReceived;
        public event Action<string>? Log;
        public event Action<string>? LinkChanged;

        public bool IsOpen => _tp.IsOpen;
        public string Describe => _tp.Describe;

        public DeviceSession(ITransport tp) { _tp = tp; }

        // ------------------------------------------------------------ 链路
        public void Start()
        {
            _run = true;
            _ioThread = new Thread(IoLoop) { Name = "pdl-io", IsBackground = true };
            _ioThread.Start();
            LinkChanged?.Invoke("已连接 " + _tp.Describe);
        }

        public void Dispose()
        {
            _run = false;
            try { _tp.Dispose(); } catch { }
            LinkChanged?.Invoke("已断开");
        }

        private void IoLoop()
        {
            while (_run)
            {
                int n;
                try { n = _tp.Read(_rx, 5); }
                catch { break; }
                if (n <= 0) continue;
                foreach (var f in _dec.Feed(_rx, n))
                {
                    try { Dispatch(f); }
                    catch (Exception ex)
                    {
                        Log?.Invoke("帧处理异常（已忽略）：" + ex.Message);
                        _inBlock = false; _blk.Clear();
                    }
                }
            }
            LinkChanged?.Invoke("链路断开");
        }

        private void Dispatch(Frame f)
        {
            switch (f.Type)
            {
                case Pdl.T.Ack:
                case Pdl.T.Nack:
                    if (_pending.TryRemove(f.Seq, out var tcs))
                        tcs.TrySetResult(f);
                    else
                        Log?.Invoke("收到无主应答 " + f);
                    break;

                case Pdl.T.Data:
                    Reassemble(f);
                    break;

                case Pdl.T.Event:
                    if (f.Payload.Length >= 8)
                    {
                        var e = new PdlEvent(f.Payload[0], f.Payload[1],
                            Pdl.U16(f.Payload, 2), Pdl.U32(f.Payload, 4));
                        EventReceived?.Invoke(e);
                    }
                    break;

                case Pdl.T.Heartbeat:
                    break;

                default:
                    Log?.Invoke("未知帧 " + f);
                    break;
            }
        }

        private void Reassemble(Frame f)
        {
            bool first = (f.Flags & Pdl.FF.First) != 0;
            bool more = (f.Flags & Pdl.FF.More) != 0;

            if (first)
            {
                _blk.Clear();
                _blk.AddRange(f.Payload);
                _inBlock = true;
            }
            else if (_inBlock)
            {
                _blk.AddRange(f.Payload);
            }
            else
            {
                Log?.Invoke($"SEQ 断裂，丢弃 {f.Payload.Length} B");
                return;
            }

            if (!more && _inBlock)
            {
                _inBlock = false;
                BlockPayload?.Invoke(_blk.ToArray());
            }
        }

        // ------------------------------------------------------------ 命令
        private byte NextSeq() { var s = _seq; _seq = (ushort)((s + 1) & 0xFFFF); return (byte)s; }

        private async Task<Frame> SendOnceAsync(byte cmd, byte[] payload, int timeoutMs)
        {
            ushort seq = NextSeq();
            var tcs = new TaskCompletionSource<Frame>(TaskCreationOptions.RunContinuationsAsynchronously);
            _pending[seq] = tcs;
            try
            {
                _tp.Write(Pdl.EncodeFrame(cmd, 0, seq, payload));
            }
            catch (Exception ex)
            {
                _pending.TryRemove(seq, out _);
                throw new InvalidOperationException("发送失败：" + ex.Message);
            }

            var done = await Task.WhenAny(tcs.Task, Task.Delay(timeoutMs));
            if (done != tcs.Task)
            {
                _pending.TryRemove(seq, out _);
                throw new TimeoutException($"命令 0x{cmd:X2} 超时（{timeoutMs} ms）");
            }
            return tcs.Task.Result;
        }

        /// <summary>串行发送并校验状态码；失败直接抛异常。</summary>
        public async Task<Frame> SendAsync(byte cmd, byte[] payload, int timeoutMs = 200, int retries = 3)
        {
            await _cmdGate.WaitAsync();
            try
            {
                Exception? last = null;
                for (int i = 0; i < retries; i++)
                {
                    try
                    {
                        var f = await SendOnceAsync(cmd, payload, timeoutMs);
                        if (f.Type == Pdl.T.Nack)
                            throw new InvalidOperationException("设备拒绝（NACK）");
                        if (f.Payload.Length >= 2 && f.Payload[1] != Pdl.St.Ok)
                        {
                            string msg = StatusText(f.Payload[1], f.Payload.Length >= 4 ? f.Payload[2] | f.Payload[3] << 8 : 0);
                            throw new PdlStatusException(f.Payload[1], msg);
                        }
                        return f;
                    }
                    catch (TimeoutException ex) { last = ex; }
                }
                throw last ?? new TimeoutException("命令超时");
            }
            finally { _cmdGate.Release(); }
        }

        private static string StatusText(byte st, int detail) => st switch
        {
            Pdl.St.BadParam => $"参数非法（载荷偏移 {detail}）",
            Pdl.St.BadState => "状态不允许（运行中禁改参数，需先停止）",
            Pdl.St.Busy => "设备忙",
            Pdl.St.CrcError => "设备收到 CRC 错帧",
            Pdl.St.LenError => "载荷长度不符",
            Pdl.St.Overflow => "设备缓冲溢出",
            Pdl.St.NotCalibrated => "设备未校准",
            _ => $"状态码 0x{st:X2}",
        };

        // ------------------------------------------------------------ 业务命令
        public async Task<IdentifyAck> IdentifyAsync(uint nonce)
        {
            var f = await SendAsync(Pdl.T.Identify, BitConverter.GetBytes(nonce), 300);
            var extra = new byte[f.Payload.Length - 4];
            Array.Copy(f.Payload, 4, extra, 0, extra.Length);
            var ack = IdentifyAck.Parse(extra);
            if (ack.Magic != "PDL1")
                throw new InvalidOperationException($"magic={ack.Magic}，不是本项目设备");
            if (ack.Nonce != nonce)
                throw new InvalidOperationException("nonce 不匹配，疑似串包或伪造应答");
            return ack;
        }

        public async Task<ChannelConfig> GetChConfigAsync(int ch)
        {
            var f = await SendAsync(Pdl.T.GetChConfig, new[] { (byte)ch });
            var extra = new byte[f.Payload.Length - 4];
            Array.Copy(f.Payload, 4, extra, 0, extra.Length);
            return ChannelConfig.Parse(extra, 0);
        }

        public async Task<AcquireAck> AcquireAsync(AcquireParams p)
        {
            var f = await SendAsync(Pdl.T.Acquire, p.Build());
            var extra = new byte[f.Payload.Length - 4];
            Array.Copy(f.Payload, 4, extra, 0, extra.Length);
            return AcquireAck.Parse(extra);
        }

        public Task<Frame> AbortAsync(bool discard) =>
            SendAsync(Pdl.T.Abort, new[] { (byte)(discard ? 1 : 0) }, 2000, 1);

        public Task<Frame> GetStatusRawAsync() => SendAsync(Pdl.T.GetStatus, Array.Empty<byte>());

        public async Task<StatusAck> GetStatusAsync()
        {
            var f = await GetStatusRawAsync();
            var extra = new byte[f.Payload.Length - 4];
            Array.Copy(f.Payload, 4, extra, 0, extra.Length);
            return StatusAck.Parse(extra);
        }

        public async Task<short[]> ReadTempOnceAsync()
        {
            var f = await SendAsync(Pdl.T.ReadTemp, Array.Empty<byte>(), 1200);
            return new[] { Pdl.I16(f.Payload, 4), Pdl.I16(f.Payload, 6) };
        }

        public async Task<long> TimeSyncAsync()
        {
            long host = DateTimeOffset.UtcNow.UtcTicks / 10;   // 单调近似：仅算 RTT 用
            var f = await SendAsync(Pdl.T.TimeSync, BitConverter.GetBytes((ulong)host));
            long dev = (long)Pdl.U64(f.Payload, 12);
            long echo = (long)Pdl.U64(f.Payload, 4);
            long rtt = host - echo;
            // 设备时钟是"上电起的微秒"，与 PC 时间没有共同基准，
            // 所以这里只测 RTT；绝对时间以 ACQUIRE 时刻的 PC 时间为锚点。
            RttMs = rtt / 1000.0;
            OffsetMs = 0;
            return dev;
        }

        public double OffsetMs { get; private set; }
        public double RttMs { get; private set; }
    }

    public sealed class PdlStatusException : Exception
    {
        public byte Status { get; }
        public PdlStatusException(byte st, string msg) : base(msg) { Status = st; }
    }
}
