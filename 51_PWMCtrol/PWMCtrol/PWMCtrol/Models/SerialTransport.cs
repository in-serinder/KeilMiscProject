using System;
using System.IO.Ports;
using System.Linq;
using System.Text;

namespace PWMCtrol.Models;

/// <summary>
/// 串口传输层：只负责开关串口、收发字节。
/// 解析、超时、重试都在 <see cref="DeviceSession"/> 里，避免这层变厚。
/// </summary>
public sealed class SerialTransport : IDisposable
{
    private SerialPort? _port;

    public event Action<byte[], int>? BytesReceived;
    public event Action<bool, string>? StateChanged;

    public bool IsOpen => _port is { IsOpen: true };

    public static string[] AvailablePorts()
    {
        try
        {
            return SerialPort.GetPortNames().OrderBy(x => x, StringComparer.OrdinalIgnoreCase).ToArray();
        }
        catch
        {
            return Array.Empty<string>();
        }
    }

    public bool Open(string portName, int baudRate = 115200)
    {
        Close();
        try
        {
            _port = new SerialPort(portName, baudRate, Parity.None, 8, StopBits.One)
            {
                ReadTimeout = 50,
                WriteTimeout = 500,
                DtrEnable = false,
                RtsEnable = false,
            };
            _port.DataReceived += OnDataReceived;
            _port.Open();
            _port.DiscardInBuffer();
            StateChanged?.Invoke(true, $"{portName} @{baudRate}");
            return true;
        }
        catch (Exception ex)
        {
            StateChanged?.Invoke(false, ex.Message);
            Close();
            return false;
        }
    }

    public void Close()
    {
        if (_port is null)
        {
            return;
        }

        try
        {
            _port.DataReceived -= OnDataReceived;
            if (_port.IsOpen)
            {
                _port.Close();
            }
        }
        catch
        {
            // 关闭失败无补救手段，忽略即可
        }
        finally
        {
            _port.Dispose();
            _port = null;
            StateChanged?.Invoke(false, "已断开");
        }
    }

    public bool Write(string frame)
    {
        if (_port is not { IsOpen: true })
        {
            return false;
        }

        try
        {
            var bytes = Encoding.ASCII.GetBytes(frame);
            _port.Write(bytes, 0, bytes.Length);
            return true;
        }
        catch
        {
            return false;
        }
    }

    private void OnDataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        if (_port is not { IsOpen: true })
        {
            return;
        }

        try
        {
            var available = _port.BytesToRead;
            if (available <= 0)
            {
                return;
            }

            var buffer = new byte[available];
            var read = _port.Read(buffer, 0, available);
            if (read > 0)
            {
                BytesReceived?.Invoke(buffer, read);
            }
        }
        catch
        {
            // 拔线时读取会抛异常，交给上层通过状态判断
        }
    }

    public void Dispose()
    {
        Close();
    }
}
