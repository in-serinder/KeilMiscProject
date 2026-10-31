using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services.Mock
{
    /// <summary>
    /// 前端阶段的串口服务：真实枚举本机串口，连接动作以延时模拟。
    /// 真实实现替换点为 <see cref="AppServices.Serial"/>。
    /// </summary>
    public sealed class MockSerialPortService : ISerialPortService
    {
        public bool IsConnected { get; private set; }

        public string? ConnectedPortName { get; private set; }

        public event EventHandler<SerialConnectionChangedEventArgs>? ConnectionChanged;

        public Task<IReadOnlyList<SerialPortDescriptor>> EnumeratePortsAsync(CancellationToken cancellationToken = default)
        {
            var ports = new List<SerialPortDescriptor>();

            try
            {
                foreach (var name in SerialPort.GetPortNames().OrderBy(n => n, StringComparer.OrdinalIgnoreCase))
                {
                    ports.Add(new SerialPortDescriptor(name, ResolveDescription(name)));
                }
            }
            catch
            {
                // 枚举失败时静默降级为空列表。
            }

            // 没有任何物理串口时提供两个虚拟端口，方便界面走查。
            if (ports.Count == 0)
            {
                ports.Add(new SerialPortDescriptor("COM3", "Virtual port (mock)"));
                ports.Add(new SerialPortDescriptor("COM4", "Virtual port (mock)"));
            }

            return Task.FromResult<IReadOnlyList<SerialPortDescriptor>>(ports);
        }

        public Task<bool> ProbeAsync(string portName, CancellationToken cancellationToken = default)
            => Task.FromResult(!string.IsNullOrWhiteSpace(portName));

        public async Task ConnectAsync(string portName, int baudRate = 115200, CancellationToken cancellationToken = default)
        {
            await Task.Delay(320, cancellationToken).ConfigureAwait(true);

            IsConnected = true;
            ConnectedPortName = portName;
            ConnectionChanged?.Invoke(this, new SerialConnectionChangedEventArgs(true, portName));
        }

        public async Task DisconnectAsync(CancellationToken cancellationToken = default)
        {
            await Task.Delay(80, cancellationToken).ConfigureAwait(true);

            var previous = ConnectedPortName;
            IsConnected = false;
            ConnectedPortName = null;
            ConnectionChanged?.Invoke(this, new SerialConnectionChangedEventArgs(false, previous));
        }

        private static string ResolveDescription(string portName)
        {
            // 简化处理：真实实现可读取注册表 / WMI 获取驱动描述。
            return portName.StartsWith("COM", StringComparison.OrdinalIgnoreCase)
                ? "Serial device"
                : "Unknown device";
        }
    }
}
