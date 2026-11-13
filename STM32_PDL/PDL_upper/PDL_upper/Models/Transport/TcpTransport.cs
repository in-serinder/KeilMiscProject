using System;
using System.IO;
using System.Net.Sockets;
using System.Threading;

namespace PDL_upper.Models.Transport
{
    /// <summary>连接 Python 模拟器的 TCP 传输。</summary>
    public sealed class TcpTransport : ITransport
    {
        private readonly string _host;
        private readonly int _port;
        private TcpClient? _client;
        private NetworkStream? _stream;

        public TcpTransport(string host, int port) { _host = host; _port = port; }

        public bool IsOpen => _client?.Connected ?? false;
        public string Describe => $"tcp://{_host}:{_port}";

        public void Open()
        {
            Close();
            _client = new TcpClient();
            if (!_client.ConnectAsync(_host, _port).Wait(3000))
            {
                _client.Close();
                _client = null;
                throw new TimeoutException($"连接 {Describe} 超时（模拟器没开？）");
            }
            _client.NoDelay = true;
            _client.ReceiveTimeout = 200;
            _stream = _client.GetStream();
        }

        public void Close()
        {
            try { _stream?.Close(); } catch { }
            try { _client?.Close(); } catch { }
            _stream = null;
            _client = null;
        }

        public int Read(byte[] buffer, int timeoutMs)
        {
            var s = _stream;
            if (s == null || !IsOpen) return 0;
            try
            {
                int want = Math.Min(buffer.Length, 8192);
                if (s.DataAvailable)
                    return s.Read(buffer, 0, want);
                // 无数据时小睡一拍，避免把 IO 线程变成死循环
                Thread.Sleep(Math.Min(timeoutMs, 5));
                return s.DataAvailable ? s.Read(buffer, 0, want) : 0;
            }
            catch (IOException) { return 0; }
            catch (ObjectDisposedException) { return 0; }
            catch (SocketException) { return 0; }
        }

        public void Write(byte[] data)
        {
            var s = _stream;
            if (s == null || !IsOpen) throw new InvalidOperationException("未连接");
            s.Write(data, 0, data.Length);
        }

        public void Dispose() => Close();
    }
}
