using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;
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
                    foreach (var name in SerialPort.GetPortNames().OrderBy(n => n, StringComparer.OrdinalIgnoreCase))
                    {
                        list.Add(new SerialPortDescriptor(name, "Serial port"));
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
                if (string.IsNullOrWhiteSpace(portName))
                {
                    return false;
                }

                try
                {
                    using var probe = new CapsaProtocol(portName) { ProbeTimeoutMs = 400 };
                    probe.Open();
                    return probe.Ping(probe.ProbeTimeoutMs);
                }
                catch (Exception)
                {
                    // 端口被占用 / 不存在
                    return false;
                }
            }, cancellationToken);
        }

        // ------------------------------------------------------------------ //
        // 连接管理
        // ------------------------------------------------------------------ //

        public Task ConnectAsync(string portName, int baudRate = 115200, CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                lock (_gate)
                {
                    if (_protocol is not null)
                    {
                        _protocol.Dispose();
                        _protocol = null;
                    }

                    var protocol = new CapsaProtocol(portName, baudRate);
                    protocol.Open();

                    if (!protocol.Ping(800))
                    {
                        protocol.Dispose();
                        throw new InvalidOperationException($"{portName} did not answer TYPE-CAPASA");
                    }

                    _protocol = protocol;
                }

                ConnectionChanged?.Invoke(this, new SerialConnectionChangedEventArgs(true, portName));
            }, cancellationToken);
        }

        public Task DisconnectAsync(CancellationToken cancellationToken = default)
        {
            return Task.Run(() =>
            {
                string? previous;

                lock (_gate)
                {
                    previous = _protocol?.PortName;
                    _protocol?.Dispose();
                    _protocol = null;
                }

                ConnectionChanged?.Invoke(this, new SerialConnectionChangedEventArgs(false, previous));
            }, cancellationToken);
        }
    }
}
