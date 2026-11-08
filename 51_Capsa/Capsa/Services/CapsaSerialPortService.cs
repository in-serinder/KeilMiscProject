using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;
using Capsa.Services.Ports;
using Capsa.Services.Protocol;

namespace Capsa.Services
{
    /// <summary>
    /// 真实串口服务：负责枚举 / 探测 / 连接，并持有 <see cref="CapsaProtocol"/> 供其它服务复用。
    /// </summary>
    public sealed class CapsaSerialPortService : ISerialPortService
    {
        private readonly object _gate = new();
        private CapsaProtocol? _protocol;

        // ---- 掉线检测（心跳）-------------------------------------------------
        // 每 3 s 检查一次：端口还在不在系统里；若最近 1.5 s 没有任何命令活动
        // （空闲），再发一次 TYPE-CAPASA 确认能通讯。连续 2 次无应答 -> 断开，
        // 界面通过 ConnectionChanged(false) 回到未连接态。
        // 读写会话期间每包都有活动（LastActivityMs 持续刷新），心跳自动让路。
        private Timer? _heartbeat;
        private int _missedBeats;

        /// <summary>上一次心跳 tick 的时间（tick 间隔自监控用，-1 = 尚未 tick 过）。</summary>
        private long _lastTickMs = -1;
        private const int HeartbeatPeriodMs = 3000;
        private const int BusyWindowMs = 1500;

        public bool IsConnected => _protocol?.IsOpen == true;

        public string? ConnectedPortName => IsConnected ? _protocol!.PortName : null;

        /// <summary>当前连接使用的协议对象（未连接为 null）。</summary>
        public CapsaProtocol? Protocol
        {
            get
            {
                lock (_gate)
                {
                    return _protocol;
                }
            }
        }

        public event EventHandler<SerialConnectionChangedEventArgs>? ConnectionChanged;

        // ------------------------------------------------------------------ //
        // 枚举 / 探测
        // ------------------------------------------------------------------ //

        public Task<IReadOnlyList<SerialPortDescriptor>> EnumeratePortsAsync(CancellationToken cancellationToken = default)
        {
            return Task.Run<IReadOnlyList<SerialPortDescriptor>>(() =>
            {
                var list = new List<SerialPortDescriptor>();

                try
                {
                    // 按长度排：Linux 上 ttyUSB0 … ttyUSB10 按字典序会把 10 排到 2 前面
                    foreach (var name in SerialPort.GetPortNames()
                                 .OrderBy(n => n.Length)
                                 .ThenBy(n => n, StringComparer.OrdinalIgnoreCase))
                    {
                        list.Add(new SerialPortDescriptor(name, PortCatalogue.Describe(name)));
                    }
                }
                catch (Exception)
                {
                    // 枚举失败按空列表处理
                }

                return list;
            }, cancellationToken);
        }

        public Task<bool> ProbeAsync(string portName, CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                LastProbeError = null;

                // Linux 上允许只敲 ttyUSB0 / serial/by-id/…，统一补成 /dev 全路径
                var port = PortFailure.NormalizePortName(portName);
                if (string.IsNullOrWhiteSpace(port))
                {
                    LastProbeError = "未选择串口";
                    return false;
                }

                try
                {
                    using var probe = new CapsaProtocol(port) { ProbeTimeoutMs = 600 };
                    probe.Open();

                    // 单次握手不可靠：USB 串口桥开门瞬间、以及 MCU 正在上电初始化时
                    // 都可能吞掉第一帧。重试 3 次（每次之间清缓冲），能覆盖这两种情况。
                    for (var attempt = 0; attempt < 3; attempt++)
                    {
                        cancellationToken.ThrowIfCancellationRequested();

                        if (probe.Ping(probe.ProbeTimeoutMs))
                        {
                            return true;
                        }

                        probe.ClearReceiveBuffer();
                        Thread.Sleep(80);
                    }

                    LastProbeError = $"{port} 连续 3 次没有回 [YES]" + PortInspector.Suffix(port);
                    return false;
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                    throw;
                }
                catch (Exception ex)
                {
                    // 分类后文案才是平台正确的：Windows 的 UnauthorizedAccessException = 被占用，
                    // Linux 上同一个异常 = 不在串口组（组名由 UnixSerialGroup 运行时探测）。
                    LastProbeError = PortFailure.Classify(ex, port).Message + PortInspector.Suffix(port);
                    return false;
                }
            }, cancellationToken);
        }

        /// <summary>最近一次探测失败的原因（成功时为 null）。</summary>
        public string? LastProbeError { get; private set; }

        // ------------------------------------------------------------------ //
        // 连接管理
        // ------------------------------------------------------------------ //

        public Task ConnectAsync(string portName, int baudRate = 115200, CancellationToken cancellationToken = default)
        {
            // 归一化在进后台线程之前：Linux 上补 /dev 前缀，Windows 原样
            var port = PortFailure.NormalizePortName(portName);

            return Task.Run(() =>
            {
                lock (_gate)
                {
                    if (_protocol is not null)
                    {
                        _protocol.Dispose();
                        _protocol = null;
                    }

                    var protocol = new CapsaProtocol(port, baudRate);
                    try
                    {
                        protocol.Open();
                    }
                    catch (Exception ex)
                    {
                        protocol.Dispose();
                        // 把原始异常放进 InnerException，ErrorLog 里仍能看到堆栈；
                        // 对外文案按平台分类（Windows "被占用" / Linux "权限"）
                        throw new InvalidOperationException(
                            PortFailure.Classify(ex, port).Message + PortInspector.Suffix(port), ex);
                    }

                    var handshaken = false;
                    for (var attempt = 0; attempt < 3 && !handshaken; attempt++)
                    {
                        cancellationToken.ThrowIfCancellationRequested();
                        handshaken = protocol.Ping(800);

                        if (!handshaken)
                        {
                            protocol.ClearReceiveBuffer();
                            Thread.Sleep(80);
                        }
                    }

                    if (!handshaken)
                    {
                        protocol.Dispose();
                        throw new InvalidOperationException(
                            $"{port} 连续 3 次没有回应 TYPE-CAPASA" + PortInspector.Suffix(port) + "。\r\n" +
                            "请确认端口没有被其它程序占用、且固件已烧录并运行。");
                    }

                    _protocol = protocol;
                }

                _missedBeats = 0;
                StartHeartbeat();

                UiThread.Post(() => ConnectionChanged?.Invoke(
                    this, new SerialConnectionChangedEventArgs(true, portName)));
            }, cancellationToken);
        }

        public Task DisconnectAsync(CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                string? previous;
                StopHeartbeat();

                lock (_gate)
                {
                    previous = _protocol?.PortName;
                    _protocol?.Dispose();
                    _protocol = null;
                }

                UiThread.Post(() => ConnectionChanged?.Invoke(
                    this, new SerialConnectionChangedEventArgs(false, previous)));
            }, cancellationToken);
        }

        // ---- 掉线检测 --------------------------------------------------------

        private void StartHeartbeat()
        {
            StopHeartbeat();
            _heartbeat = new Timer(_ => HeartbeatTick(), null, HeartbeatPeriodMs, HeartbeatPeriodMs);
        }

        private void StopHeartbeat()
        {
            _heartbeat?.Dispose();
            _heartbeat = null;
            _missedBeats = 0;
        }

        private void HeartbeatTick()
        {
            // tick 间隔自监控：正常 3 s 一跳。间隔异常拉长说明心跳线程被卡过
            // （串口驱动挂死 / ThreadPool 饥饿）—— 这正是"拔线后不断开"的嫌疑场景，
            // 必须留下证据，不能让心跳静悄悄地半死不活。
            var now = Environment.TickCount64;
            if (_lastTickMs >= 0)
            {
                var gap = now - _lastTickMs;
                if (gap > HeartbeatPeriodMs * 3)
                {
                    Capsa.Diagnostics.ErrorLog.Add(
                        "Heartbeat",
                        $"tick gap {gap} ms (expected ~{HeartbeatPeriodMs} ms) — heartbeat thread was stalled");
                }
            }

            _lastTickMs = now;

            CapsaProtocol? protocol;
            string portName;

            lock (_gate)
            {
                protocol = _protocol;
                if (protocol is null || !protocol.IsOpen)
                {
                    return;
                }
                portName = protocol.PortName;
            }

            // 端口从系统里消失（USB 拔掉 / 驱动卸载）：立刻断开，不用等握手超时
            var stillListed = false;
            try
            {
                stillListed = SerialPort.GetPortNames()
                    .Any(p => string.Equals(p, portName, StringComparison.OrdinalIgnoreCase));
            }
            catch (Exception)
            {
                // 枚举失败：按"可能还在"处理，交给下面的握手判断
                stillListed = true;
            }

            if (!stillListed)
            {
                DropConnection(portName, "port disappeared");
                return;
            }

            // 最近有命令活动（写入 / 读取 / 刷新）就跳过，别抢总线；
            // ★ 传输会话（WriteMemory / ReadMemory）进行中强制让路：
            //   读会话里上位机十几秒只收不发，光看 LastActivityMs 会误判空闲，
            //   probe 会插进正在流式回数据的总线，两个接收者抢同一串口。
            if (protocol.IsSessionBusy ||
                Environment.TickCount64 - protocol.LastActivityMs < BusyWindowMs)
            {
                return;
            }

            // 空闲心跳（★ 只在无任务时才走到这里：会话中 IsSessionBusy 直接 return）。
            // probe 的异常分两类处理 —— "被占用/被移除"是确定性故障，不用给两次机会。
            protocol.ClearPortError();
            bool alive;
            Exception? failure = null;
            try
            {
                alive = protocol.Ping(500);
            }
            catch (Exception ex)
            {
                alive = false;
                failure = ex;
            }

            if (alive)
            {
                _missedBeats = 0;
                return;
            }

            // ★ 确定性故障（被占用 / 权限 / 设备移除）重试没有意义 —— 立即断开并说清原因。
            //   判定走 PortFailure 分类，两个平台的语义才一致：Windows 的
            //   UnauthorizedAccessException 是"被占用"，Linux 的同一个异常是"不在串口组"。
            if (failure is not null)
            {
                var classified = PortFailure.Classify(failure, portName);
                if (classified.IsFatal)
                {
                    _missedBeats = 0;
                    DropConnection(portName, $"{classified.Kind} · {classified.Message}");
                    return;
                }
            }

            if (!string.IsNullOrEmpty(protocol.LastPortError))
            {
                _missedBeats = 0;
                DropConnection(portName, $"port lost · {protocol.LastPortError}");
                return;
            }

            if (++_missedBeats < 2)
            {
                return;      // 单次失败可能只是瞬时干扰，再给一个周期
            }

            DropConnection(portName, "no response");
        }

        private void DropConnection(string? portName, string reason)
        {
            StopHeartbeat();

            // 掉线历史进日志：之后任何时候都能从 Logs 里查到每次断开的原因
            Capsa.Diagnostics.ErrorLog.Add("Heartbeat", $"connection dropped · {reason}");

            lock (_gate)
            {
                _protocol?.Dispose();
                _protocol = null;
            }

            UiThread.Post(() => ConnectionChanged?.Invoke(
                this,
                new SerialConnectionChangedEventArgs(false, portName) { Reason = reason }));
        }

    }
}
