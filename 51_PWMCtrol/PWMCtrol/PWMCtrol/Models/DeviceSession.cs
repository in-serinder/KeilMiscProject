using System;
using System.Threading;
using System.Threading.Tasks;

namespace PWMCtrol.Models;

/// <summary>
/// 设备会话：串口 + 帧解析 + 一问一答的命令调度。
/// 协议是严格的一问一答，所以用信号量串行化命令；ECHO 帧不计入应答。
/// </summary>
public sealed class DeviceSession : IDisposable
{
    private readonly SerialTransport _transport = new();
    private readonly FrameCodec _codec = new();
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _pendingLock = new();
    private TaskCompletionSource<string>? _pending;

    /// <summary>所有原始帧（含 ECHO / EVT），用于日志。</summary>
    public event Action<string>? FrameReceived;

    /// <summary>解析成功的通道上报。</summary>
    public event Action<ChannelEcho>? EchoReceived;

    public event Action<bool, string>? ConnectionChanged;

    public DeviceSession()
    {
        _transport.BytesReceived += (buffer, count) => _codec.Feed(buffer, count, HandleFrame);
        _transport.StateChanged += (open, message) => ConnectionChanged?.Invoke(open, message);
    }

    public bool IsOpen => _transport.IsOpen;

    public bool Open(string portName)
    {
        _codec.Reset();
        return _transport.Open(portName);
    }

    public void Close()
    {
        lock (_pendingLock)
        {
            _pending?.TrySetResult(string.Empty);
            _pending = null;
        }

        _transport.Close();
    }

    /// <summary>发送一条命令并等待应答；超时返回 null。</summary>
    public async Task<string?> SendAsync(string body, int timeoutMs = 800)
    {
        if (!IsOpen)
        {
            return null;
        }

        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            var tcs = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
            lock (_pendingLock)
            {
                _pending = tcs;
            }

            if (!_transport.Write(">" + body + "<"))
            {
                lock (_pendingLock)
                {
                    _pending = null;
                }
                return null;
            }

            var finished = await Task.WhenAny(tcs.Task, Task.Delay(timeoutMs)).ConfigureAwait(false);
            lock (_pendingLock)
            {
                _pending = null;
            }

            return finished == tcs.Task ? tcs.Task.Result : null;
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>握手：确认对端是本项目设备。</summary>
    public async Task<bool> IdentifyAsync(CancellationToken token = default)
    {
        if (!IsOpen)
        {
            return false;
        }

        for (var attempt = 0; attempt < 3 && !token.IsCancellationRequested; attempt++)
        {
            var reply = await SendAsync("TYPE-PWMCTROL", 700).ConfigureAwait(false);
            if (reply == "YES")
            {
                return true;
            }
        }

        return false;
    }

    private void HandleFrame(string body)
    {
        FrameReceived?.Invoke(body);

        if (body.StartsWith("ECHO_", StringComparison.Ordinal))
        {
            var echo = ChannelEcho.Parse(body);
            if (echo is not null)
            {
                EchoReceived?.Invoke(echo);
            }
            return;
        }

        // 异步事件不占用命令应答
        if (body.StartsWith("EVT_", StringComparison.Ordinal))
        {
            return;
        }

        TaskCompletionSource<string>? pending;
        lock (_pendingLock)
        {
            pending = _pending;
        }

        pending?.TrySetResult(body);
    }

    public void Dispose()
    {
        _transport.Dispose();
        _gate.Dispose();
    }
}
