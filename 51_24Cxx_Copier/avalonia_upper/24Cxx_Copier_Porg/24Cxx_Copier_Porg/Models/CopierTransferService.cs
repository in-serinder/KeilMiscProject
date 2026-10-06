using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// Runs the read/write transfer flows against a single slaved address, per the
/// command tables in <c>CMD.md</c>.
///
/// This class is transport-agnostic: it only talks to a
/// <see cref="CopierProtocolSession"/>, so it can be unit-tested with a fake
/// session and reused by the UI with a real serial one.
/// </summary>
public sealed class CopierTransferService
{
    /// <summary>
    /// Number of packet handshakes collapsed into a single <c>[DATA]</c> log
    /// line. One line is emitted every <c>LogBatchSize</c> packets (and always
    /// for the final packet), instead of one line per packet.
    /// </summary>
    public const int LogBatchSize = 64;

    /// <summary>Progress callback: (bytesDone, bytesTotal).</summary>
    public delegate void ProgressCallback(int done, int total);

    /// <summary>Emits a fine-grained progress line in the "[UART]"/"[OPT]" style.</summary>
    public delegate void LogCallback(string message);

    private readonly CopierProtocolSession _session;
    private readonly Action<string> _log;

    public CopierTransferService(CopierProtocolSession session, Action<string>? log = null)
    {
        _session = session ?? throw new ArgumentNullException(nameof(session));
        _log = log ?? (_ => { });
    }

    // ---------------------------------------------------------------------
    // Write flow
    // ---------------------------------------------------------------------

    /// <summary>
    /// Writes <paramref name="payload"/> to <paramref name="chipAddress"/>.
    ///
    /// Flow: <c>ROUTER_</c> → <c>EDGE_</c> → <c>INT_W</c> →
    /// (<c>W_</c> packet → await <c>RX-OK</c>)* → <c>TX-OVER</c>.
    /// </summary>
    /// <param name="chipAddress">Target I2C chip address (0x50..0x57).</param>
    /// <param name="payload">Bytes to write (already built).</param>
    /// <param name="progress">Optional per-address progress sink.</param>
    /// <param name="ct">Cancellation token.</param>
    public async Task WriteAsync(byte chipAddress, byte[] payload, ProgressCallback? progress = null,
        CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(payload);

        var total = payload.Length;
        var endAddress = Math.Max(0, total - 1);
        var stopwatch = Stopwatch.StartNew();

        // 1) Route to the chip.
        _log($"[OPT] Writing {total} byte(s) to 0x{chipAddress:X2} (edge 0x{endAddress:X})");
        var routed = await _session.RouteAsync(chipAddress, ct: ct).ConfigureAwait(false);
        EnsureAck(routed, "ROUTER");

        // 2) Set the end address.
        var edge = await _session.EdgeAsync(endAddress, ct: ct).ConfigureAwait(false);
        EnsureAck(edge, "EDGE");

        // 3) Enter write mode.
        var started = await _session.StartWriteAsync(ct: ct).ConfigureAwait(false);
        EnsureAck(started, "INT_W");

        // 4) Stream 16-byte packets, awaiting RX-OK after each. The raw
        //    W_/RX-OK/R-S traffic is compressed: one [DATA] line per 64 packets.
        //    Hide the per-packet RX-OK/R-S handshake from the log while streaming.
        progress?.Invoke(0, total);
        var done = 0;
        var packetNo = 0;
        var packetCount = (total + ParserTXModel.PacketSize - 1) / ParserTXModel.PacketSize;

        _session.QuietHandshake = true;

        for (var offset = 0; offset < total; offset += ParserTXModel.PacketSize)
        {
            ct.ThrowIfCancellationRequested();

            var remaining = Math.Min(ParserTXModel.PacketSize, total - offset);
            var packet = new byte[remaining];
            Array.Copy(payload, offset, packet, 0, remaining);

            var ack = await _session.WritePacketAsync(packet, ct: ct).ConfigureAwait(false);
            if (ack.Kind != SerialResponseKind.Success)
            {
                throw new CopierProtocolException(
                    $"Packet @0x{offset:X} not acknowledged (got {ack.Kind}).");
            }

            done += remaining;
            packetNo++;

            // Collapse 64 handshake cycles into one [DATA] line (always log the
            // final packet so the completion percentage shows).
            if (packetNo % LogBatchSize == 0 || packetNo == packetCount)
            {
                _log($"[DATA] [0x{chipAddress:X2}] Sending packet {packetNo}/{packetCount}, Progress {Percent(done, total)}");
            }

            progress?.Invoke(done, total);
        }

        // 5) Signal the end of the write flow. Restore normal logging so the
        //    final R-S (and any R-ERR) is shown.
        _session.QuietHandshake = false;
        var over = await _session.SendTxOverAsync(ct: ct).ConfigureAwait(false);
        EnsureAck(over, "TX-OVER");

        stopwatch.Stop();
        _log($"[DATA] ChipAddr:0x{chipAddress:X2} | Write Complete | {total} bytes (100%) | Elapsed {FormatElapsed(stopwatch.Elapsed)}");
        _log($"[OPT] Write to 0x{chipAddress:X2} complete ({total} bytes in {FormatElapsed(stopwatch.Elapsed)}).");
    }

    // ---------------------------------------------------------------------
    // Read flow
    // ---------------------------------------------------------------------

    /// <summary>
    /// Reads <paramref name="length"/> bytes from <paramref name="chipAddress"/>.
    ///
    /// Flow: <c>ROUTER_</c> → <c>EDGE_</c> → <c>INT_R</c> →
    /// (await <c>R_[..]</c> → send <c>RX-OK</c>)* → until <c>TX-OVER</c>.
    /// </summary>
    /// <returns>The assembled byte buffer (clamped to <paramref name="length"/>).</returns>
    public async Task<byte[]> ReadAsync(byte chipAddress, int length, ProgressCallback? progress = null,
        CancellationToken ct = default)
    {
        if (length <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(length), "Read length must be positive.");
        }

        var endAddress = length - 1;
        var buffer = new List<byte>(length);
        var stopwatch = Stopwatch.StartNew();

        // 1) Route to the chip.
        _log($"[OPT] Reading {length} byte(s) from 0x{chipAddress:X2} (edge 0x{endAddress:X})");
        var routed = await _session.RouteAsync(chipAddress, ct: ct).ConfigureAwait(false);
        EnsureAck(routed, "ROUTER");

        // 2) Set the end address.
        var edge = await _session.EdgeAsync(endAddress, ct: ct).ConfigureAwait(false);
        EnsureAck(edge, "EDGE");

        // 3) Enter read mode; the device starts pushing packets.
        var started = await _session.StartReadAsync(ct: ct).ConfigureAwait(false);
        EnsureAck(started, "INT_R");

        // 4) Loop: receive a data packet, acknowledge it with RX-OK, until the
        //    device signals TX-OVER. After the last packet's RX-OK the device
        //    replies with TX-OVER instead of another packet. The raw traffic is
        //    compressed: one [DATA] line per 64 packets.
        progress?.Invoke(0, length);
        var packetCount = (length + ParserTXModel.PacketSize - 1) / ParserTXModel.PacketSize;
        var packetNo = 0;

        while (buffer.Count < length)
        {
            ct.ThrowIfCancellationRequested();

            var frame = await _session.AwaitAsync(SerialResponseKind.ReadData, ct: ct)
                .ConfigureAwait(false);

            // The device may stop early with TX-OVER.
            if (frame.Kind == SerialResponseKind.TxOver)
            {
                _log("[OPT] Device signalled TX-OVER; stopping read.");
                break;
            }

            if (frame.Kind == SerialResponseKind.Error)
            {
                throw new CopierProtocolException("Device reported R-ERR during read.");
            }

            if (frame.Kind != SerialResponseKind.ReadData || frame.Data is null)
            {
                throw new CopierProtocolException("Timed out waiting for a read packet.");
            }

            foreach (var b in frame.Data)
            {
                if (buffer.Count >= length)
                {
                    break;
                }

                buffer.Add(b);
            }

            packetNo++;

            // Acknowledge the packet so the device sends the next one.
            await _session.SendRxOkAsync(ct).ConfigureAwait(false);

            // Collapse 64 handshake cycles into one [DATA] line (always log the
            // final packet so the completion percentage shows).
            if (packetNo % LogBatchSize == 0 || packetNo == packetCount)
            {
                _log($"[DATA] [0x{chipAddress:X2}] Received packet {packetNo}/{packetCount}, Progress {Percent(buffer.Count, length)}");
            }

            progress?.Invoke(buffer.Count, length);
        }

        stopwatch.Stop();
        _log($"[DATA] [0x{chipAddress:X2}] Read completed, {buffer.Count} bytes (100%) | Elapsed {FormatElapsed(stopwatch.Elapsed)}");
        _log($"[OPT] Read from 0x{chipAddress:X2} complete ({buffer.Count} bytes in {FormatElapsed(stopwatch.Elapsed)}).");
        return buffer.ToArray();
    }

    private static void EnsureAck(SerialResponse response, string step)
    {
        if (response.Kind != SerialResponseKind.Success)
        {
            throw new CopierProtocolException(
                $"{step} failed: expected R-S, got {response.Kind}.");
        }
    }

    /// <summary>
    /// Formats a transfer duration in a compact, human-readable form:
    /// "1.234 s", "2 m 05.4 s" or "45 ms".
    /// </summary>
    private static string FormatElapsed(TimeSpan elapsed)
    {
        if (elapsed.TotalMinutes >= 1)
        {
            return $"{(int)elapsed.TotalMinutes} m {elapsed.Seconds:00}.{elapsed.Milliseconds / 100} s";
        }

        if (elapsed.TotalSeconds >= 1)
        {
            return $"{elapsed.TotalSeconds:0.000} s";
        }

        return $"{elapsed.TotalMilliseconds:0} ms";
    }

    /// <summary>Formats a clamped whole-number progress percentage, e.g. "20%".</summary>
    private static string Percent(int done, int total)
    {
        if (total <= 0)
        {
            return "100%";
        }

        var pct = (int)Math.Round(done * 100.0 / total);
        return $"{Math.Clamp(pct, 0, 100)}%";
    }
}

/// <summary>Raised when a transfer step fails or times out.</summary>
public sealed class CopierProtocolException : Exception
{
    public CopierProtocolException(string message) : base(message)
    {
    }
}
