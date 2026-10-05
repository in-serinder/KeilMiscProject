using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// A stateful, single-port protocol session used to talk to the simulated
/// lower device (下位机) as described in <c>CMD.md</c>.
///
/// The session owns one <see cref="SerialPort"/> (115200 8N1), builds commands
/// with <see cref="ParserTXModel"/> and parses replies with
/// <see cref="ParserRXModel"/>. Every sent frame, received frame and error is
/// reported through the <see cref="Log"/> callback so callers can surface it in
/// the UI log.
///
/// It is <b>not</b> thread-safe: a single session is meant to run one transfer
/// task at a time.
/// </summary>
public sealed class CopierProtocolSession : IDisposable
{
    /// <summary>Protocol baud rate (fixed by CMD.md).</summary>
    public const int BaudRate = 115200;

    /// <summary>Default timeout for a single expected response.</summary>
    public const int DefaultTimeoutMs = 2000;

    private readonly SerialPort _port;
    private readonly ParserRXModel _parser = new();
    private readonly Action<string>? _log;
    private bool _disposed;

    /// <summary>
    /// Frames parsed but not yet consumed by <see cref="AwaitAsync"/>.
    /// Important: a single read may deliver several frames at once (e.g. the
    /// <c>R-S</c> of <c>INT_R</c> immediately followed by the first
    /// <c>R_[..]</c> packet); the ones that don't match the awaited kind must be
    /// kept for the next await instead of being dropped.
    /// </summary>
    private readonly Queue<SerialResponse> _pending = new();

    public CopierProtocolSession(string portName, Action<string>? log = null)
    {
        PortName = portName;
        _log = log;
        _port = new SerialPort(portName, BaudRate, Parity.None, 8, StopBits.One)
        {
            Handshake = Handshake.None,
            DtrEnable = true,
            RtsEnable = true,
            ReadTimeout = 250,
            WriteTimeout = DefaultTimeoutMs,
        };
    }

    /// <summary>Port this session is bound to.</summary>
    public string PortName { get; }

    /// <summary>True once the underlying port has been opened.</summary>
    public bool IsOpen => _port.IsOpen;

    /// <summary>
    /// When <c>false</c> (the default) the bulk data frames
    /// (<c>W_[d1,…,d16]</c> / <c>R_[d1,…,d16]</c>) are <b>not</b> logged — the
    /// callers emit a compressed <c>[DATA]</c> progress line instead. All other
    /// frames (<c>ROUTER</c>, <c>EDGE</c>, <c>INT_R/W</c>, <c>RX-OK</c>,
    /// <c>R-S</c>, <c>TX-OVER</c>, <c>R-ERR</c> …) are always logged.
    /// Set to <c>true</c> to also log every data frame verbatim.
    /// </summary>
    public bool Verbose { get; set; }

    /// <summary>
    /// When set, the per-packet handshake frames received during a write
    /// (<c>RX-OK</c> followed by <c>R-S</c>) are <b>not</b> logged — the write
    /// loop emits a compressed <c>[DATA]</c> line every 64 packets instead.
    /// Turned on by the transfer service around the packet-streaming loop and
    /// off again before the final <c>TX-OVER</c> (whose <c>R-S</c> is shown).
    /// </summary>
    public bool QuietHandshake { get; set; }

    /// <summary>
    /// True when <paramref name="content"/> (a frame body without the
    /// <c>&gt;</c>/<c>&lt;</c> markers) is a bulk data frame: <c>W_[…]</c> or
    /// <c>R_[…]</c>. These are the only frames suppressed by default.
    /// </summary>
    private static bool IsBulkDataFrame(string? content)
    {
        if (string.IsNullOrEmpty(content))
        {
            return false;
        }

        var s = content.TrimStart();
        return s.StartsWith("W_[", StringComparison.Ordinal)
            || s.StartsWith("R_[", StringComparison.Ordinal);
    }

    /// <summary>
    /// True when a received frame should not be logged: bulk data frames
    /// always, and (while <see cref="QuietHandshake"/> is set) the per-packet
    /// write handshake frames <c>RX-OK</c> / <c>R-S</c>.
    /// </summary>
    private bool IsSuppressedFrame(SerialResponse frame)
    {
        if (IsBulkDataFrame(frame.Content))
        {
            return true;
        }

        if (!QuietHandshake)
        {
            return false;
        }

        var content = frame.Content.Trim();
        return string.Equals(content, "RX-OK", StringComparison.Ordinal)
            || string.Equals(content, "R-S", StringComparison.Ordinal);
    }

    // ---------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------

    /// <summary>Opens the port.</summary>
    public void Open()
    {
        if (!_port.IsOpen)
        {
            _port.Open();
            // Drop any bytes that arrived during/just after open.
            try { _port.DiscardInBuffer(); } catch { /* ignore */ }
            try { _port.DiscardOutBuffer(); } catch { /* ignore */ }
            _pending.Clear();
            _parser.Reset();
        }
    }

    /// <summary>Closes the port if open (never throws).</summary>
    public void Close()
    {
        try { if (_port.IsOpen) _port.Close(); } catch { /* ignore */ }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        Close();
        try { _port.Dispose(); } catch { /* ignore */ }
    }

    // ---------------------------------------------------------------------
    // High-level commands
    // ---------------------------------------------------------------------

    /// <summary>
    /// Sends <c>&gt;ROUTER_0xXX&lt;</c> and waits for <c>R-S</c>.
    /// </summary>
    public Task<SerialResponse> RouteAsync(byte i2cAddress, int timeoutMs = DefaultTimeoutMs,
        CancellationToken ct = default)
        => SendAndAwaitAckAsync(ParserTXModel.Router(i2cAddress), timeoutMs, ct);

    /// <summary>
    /// Sends <c>&gt;EDGE_0xXXXX&lt;</c> and waits for <c>R-S</c>.
    /// </summary>
    public Task<SerialResponse> EdgeAsync(int endAddress, int timeoutMs = DefaultTimeoutMs,
        CancellationToken ct = default)
        => SendAndAwaitAckAsync(ParserTXModel.Edge(endAddress), timeoutMs, ct);

    /// <summary>Sends <c>&gt;INT_W&lt;</c> and waits for <c>R-S</c>.</summary>
    public Task<SerialResponse> StartWriteAsync(int timeoutMs = DefaultTimeoutMs,
        CancellationToken ct = default)
        => SendAndAwaitAckAsync(ParserTXModel.IntWrite(), timeoutMs, ct);

    /// <summary>Sends <c>&gt;INT_R&lt;</c> and waits for the first <c>R-S</c>.</summary>
    public Task<SerialResponse> StartReadAsync(int timeoutMs = DefaultTimeoutMs,
        CancellationToken ct = default)
        => SendAndAwaitAckAsync(ParserTXModel.IntRead(), timeoutMs, ct);

    /// <summary>
    /// Sends a <c>&gt;W_[..]&lt;</c> packet and waits for the device's
    /// <c>RX-OK</c> acknowledgement, then for the following <c>R-S</c> /
    /// <c>R-ERR</c> (the device sends both: "先 RX-OK，写完 R-S/R-ERR").
    /// </summary>
    public async Task<SerialResponse> WritePacketAsync(byte[] packet,
        int timeoutMs = DefaultTimeoutMs, CancellationToken ct = default)
    {
        var rxOk = await SendAndAwaitAsync(ParserTXModel.WritePacket(packet),
            SerialResponseKind.RxOk, timeoutMs, ct).ConfigureAwait(false);

        if (rxOk.Kind != SerialResponseKind.RxOk)
        {
            return rxOk;
        }

        // Consume the trailing R-S / R-ERR so it does not leak into the next
        // await (e.g. the final TX-OVER).
        var ack = await AwaitAsync(SerialResponseKind.Success, timeoutMs, ct)
            .ConfigureAwait(false);

        return ack;
    }

    /// <summary>
    /// Sends <c>&gt;RX-OK&lt;</c> (read-flow acknowledgement) without waiting
    /// for a reply. The device's next frame (another <c>R_[..]</c> packet or a
    /// final <c>TX-OVER</c>) is collected by the following read await.
    /// </summary>
    public Task SendRxOkAsync(CancellationToken ct = default)
        => WriteFrameAsync(ParserTXModel.RxOk(), ct);

    /// <summary>Sends <c>&gt;TX-OVER&lt;</c> and waits for <c>R-S</c>.</summary>
    public Task<SerialResponse> SendTxOverAsync(int timeoutMs = DefaultTimeoutMs,
        CancellationToken ct = default)
        => SendAndAwaitAckAsync(ParserTXModel.TxOver(), timeoutMs, ct);

    /// <summary>Sends <c>&gt;ABORT&lt;</c> (fire and forget, best-effort).</summary>
    public void Abort()
    {
        try
        {
            var frame = ParserTXModel.Abort();
            _port.Write(frame);
            Log("[UART] >> >ABORT<");
        }
        catch (Exception ex)
        {
            Log($"[ERR] Failed to send ABORT: {ex.Message}");
        }
    }

    // ---------------------------------------------------------------------
    // Low-level frame IO
    // ---------------------------------------------------------------------

    /// <summary>
    /// Sends a frame and waits until a response of the expected kind arrives
    /// (any <c>R-ERR</c> is treated as an immediate failure).
    /// </summary>
    public async Task<SerialResponse> SendAndAwaitAsync(string frame, SerialResponseKind expected,
        int timeoutMs = DefaultTimeoutMs, CancellationToken ct = default)
    {
        await WriteFrameAsync(frame, ct).ConfigureAwait(false);
        return await AwaitAsync(expected, timeoutMs, ct).ConfigureAwait(false);
    }

    /// <summary>
    /// Sends a frame and waits for <c>R-S</c> / <c>R-ERR</c> (the standard
    /// command acknowledgement).
    /// </summary>
    public Task<SerialResponse> SendAndAwaitAckAsync(string frame, int timeoutMs = DefaultTimeoutMs,
        CancellationToken ct = default)
        => SendAndAwaitAsync(frame, SerialResponseKind.Success, timeoutMs, ct);

    /// <summary>
    /// Writes a raw pre-built frame and logs it — except bulk data frames
    /// (<c>W_[…]</c>) and the read-flow <c>RX-OK</c> acknowledgements, which are
    /// only logged when <see cref="Verbose"/> is set. <c>RX-OK</c> is sent once
    /// per received packet, so logging it would flood the log during reads.
    /// </summary>
    public async Task WriteFrameAsync(string frame, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        _port.Write(frame);

        var content = ParserRXModel.ExtractContent(frame);
        var quiet = IsBulkDataFrame(content)
            || string.Equals(content.Trim(), "RX-OK", StringComparison.Ordinal);

        if (Verbose || !quiet)
        {
            Log($"[UART] >> {Sanitize(frame)}");
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    /// <summary>
    /// Waits (non-blocking) until a frame of <paramref name="expected"/> kind
    /// is received or the timeout elapses. <c>R-ERR</c> returns immediately.
    /// </summary>
    public async Task<SerialResponse> AwaitAsync(SerialResponseKind expected,
        int timeoutMs = DefaultTimeoutMs, CancellationToken ct = default)
    {
        var deadline = Environment.TickCount64 + timeoutMs;

        while (Environment.TickCount64 < deadline)
        {
            ct.ThrowIfCancellationRequested();

            // Drain any already-parsed frames first.
            while (_pending.Count > 0)
            {
                var response = _pending.Dequeue();

                if (response.Kind == SerialResponseKind.Error)
                {
                    return response;
                }

                // When awaiting read data, a TX-OVER means the flow is finished
                // (no further packets will come).
                if (expected == SerialResponseKind.ReadData
                    && response.Kind == SerialResponseKind.TxOver)
                {
                    return response;
                }

                if (response.Kind == expected)
                {
                    return response;
                }

                // Not what we're waiting for (e.g. a stray R-S): drop it and
                // keep looking.
            }

            // Refill from the port (parsed frames are enqueued for next time).
            Poll();

            if (_pending.Count == 0)
            {
                await Task.Delay(10, ct).ConfigureAwait(false);
            }
        }

        return new SerialResponse { Kind = SerialResponseKind.Unknown, Content = string.Empty };
    }

    /// <summary>
    /// Reads whatever bytes are currently buffered (non-blocking), parses the
    /// complete frames, enqueues them for <see cref="AwaitAsync"/> and returns
    /// them. Every frame is logged here except bulk data frames
    /// (<c>R_[…]</c>), which require <see cref="Verbose"/>.
    /// </summary>
    public IReadOnlyList<SerialResponse> Poll()
    {
        int available;
        try { available = _port.BytesToRead; }
        catch { return Array.Empty<SerialResponse>(); }

        if (available <= 0)
        {
            return Array.Empty<SerialResponse>();
        }

        var buffer = new byte[available];
        int read;
        try { read = _port.Read(buffer, 0, available); }
        catch { return Array.Empty<SerialResponse>(); }

        var text = Encoding.ASCII.GetString(buffer, 0, read);
        var frames = _parser.Feed(text);

        foreach (var f in frames)
        {
            // Bulk data frames (R_[…]) are suppressed by default; everything
            // else (R-S, R-ERR, RX-OK, TX-OVER, YES …) is always logged, except
            // the per-packet write handshake (RX-OK + R-S) while QuietHandshake
            // is on.
            if (Verbose || !IsSuppressedFrame(f))
            {
                Log(f.HasData
                    ? $"[UART] << >R_[{ParserTXModel.ToHexString(f.Data!)}]<"
                    : $"[UART] << {Sanitize(("<" + f.Content + ">"))}");
            }

            _pending.Enqueue(f);
        }

        return frames;
    }

    private void Log(string message) => _log?.Invoke(message);

    /// <summary>Turns the literal CR/LF of a frame into visible escapes.</summary>
    private static string Sanitize(string frame)
        => frame.Replace("\r", "\\r").Replace("\n", "\\n");
}
