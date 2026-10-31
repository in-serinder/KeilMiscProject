using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Capsa.Models;

namespace Capsa.Services
{
    /// <summary>
    /// 串口枚举 / 连接管理。界面只依赖该接口，真实实现（串口 + 握手协议）
    /// 在 <see cref="AppServices"/> 中替换即可。
    /// </summary>
    public interface ISerialPortService
    {
        bool IsConnected { get; }

        string? ConnectedPortName { get; }

        /// <summary>连接状态变化。</summary>
        event EventHandler<SerialConnectionChangedEventArgs>? ConnectionChanged;

        /// <summary>枚举当前可用串口。</summary>
        Task<IReadOnlyList<SerialPortDescriptor>> EnumeratePortsAsync(CancellationToken cancellationToken = default);

        /// <summary>打开端口发送 TYPE-CAPASA 探测是否为 Capsa 设备（探测后立即关闭）。</summary>
        Task<bool> ProbeAsync(string portName, CancellationToken cancellationToken = default);

        /// <summary>打开串口并完成设备握手。</summary>
        Task ConnectAsync(string portName, int baudRate = 115200, CancellationToken cancellationToken = default);

        /// <summary>关闭串口。</summary>
        Task DisconnectAsync(CancellationToken cancellationToken = default);
    }

    public sealed class SerialConnectionChangedEventArgs : EventArgs
    {
        public SerialConnectionChangedEventArgs(bool isConnected, string? portName)
        {
            IsConnected = isConnected;
            PortName = portName;
        }

        public bool IsConnected { get; }

        public string? PortName { get; }
    }
}
