using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Threading;
using Capsa.Models;

namespace Capsa.Services.Protocol
{
    /// <summary>帧种类：>数据&lt; 为 Data，[应答] 为 Response。</summary>
    public enum FrameKind
    {
        Data = 0,
        Response = 1,
    }

    /// <summary>一帧：下位机发 >...&lt;，或回 [....]。</summary>
    public readonly record struct ProtocolFrame(FrameKind Kind, string Payload)
    {
        public bool Matches(string token)
            => Kind == FrameKind.Response && string.Equals(Payload, token, StringComparison.OrdinalIgnoreCase);

        public bool StartsWith(string prefix)
            => Payload.StartsWith(prefix, StringComparison.OrdinalIgnoreCase);

        public override string ToString() => Kind == FrameKind.Data ? $">{Payload}<" : $"[{Payload}]";
    }

    /// <summary>SET / INFO 的 9 字段模型。</summary>
    public sealed class CapsaInfo
    {
        public string Name { get; set; } = string.Empty;

        /// <summary>WP 掩码，bit0~bit7 ↔ 0x50~0x57。</summary>
        public int WriteProtectMask { get; set; }

        /// <summary>健康掩码，bit0~bit7 ↔ 0x50~0x57。</summary>
        public int HealthMask { get; set; } = 0xFF;

        /// <summary>阵列容量（KB）。</summary>
        public int CapacityKb { get; set; }

        /// <summary>型号（Kbit）。</summary>
        public int ModelKbit { get; set; } = 64;

        public long ModifiedAt { get; set; }

        public long ReadAt { get; set; }

        /// <summary>0 EEPROM / 1 FRAM / 2 EERAM / 3 SRAM。</summary>
        public int Medium { get; set; }

        public string Uid { get; set; } = "000000000000";

        /// <summary>单片容量（字节）。</summary>
        public int ChipBytes => ChipCatalog.Find($"24C{ModelKbit}")?.Bytes
                                ?? ChipCatalog.Find("24C64")!.Bytes;

        public string MediumName => StorageMediumCatalog.All
            .FirstOrDefault(m => (int)m.Value == Medium)?.DisplayName ?? Medium.ToString();

        /// <summary>正常芯片数量（健康掩码 bit 数）。</summary>
        public int HealthyChips => CountBits(HealthMask);

        /// <summary>组合 SET 命令的 9 字段部分。</summary>
        public string ToSetFields()
            => string.Join("_",
                Sanitize(Name),
                Hex(WriteProtectMask),
                Hex(HealthMask),
                CapacityKb.ToString(CultureInfo.InvariantCulture),
                ModelKbit.ToString(CultureInfo.InvariantCulture),
                ModifiedAt.ToString(CultureInfo.InvariantCulture),
                ReadAt.ToString(CultureInfo.InvariantCulture),
                Medium.ToString(CultureInfo.InvariantCulture),
                Uid.ToUpperInvariant());

        /// <summary>组合 INFO 应答（用于模拟 / 回显）。</summary>
        public string ToInfoFrame() => "INFO_" + ToSetFields();

        public static CapsaInfo? Parse(string payload)
        {
            const string prefix = "INFO_";
            if (!payload.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            {
                return null;
            }

            var parts = payload[prefix.Length..].Split('_');
            if (parts.Length != 9)
            {
                return null;
            }

            try
            {
                return new CapsaInfo
                {
                    Name = parts[0],
                    WriteProtectMask = ParseMask(parts[1]),
                    HealthMask = ParseMask(parts[2]),
                    CapacityKb = int.Parse(parts[3], CultureInfo.InvariantCulture),
                    ModelKbit = int.Parse(parts[4], CultureInfo.InvariantCulture),
                    ModifiedAt = long.Parse(parts[5], CultureInfo.InvariantCulture),
                    ReadAt = long.Parse(parts[6], CultureInfo.InvariantCulture),
                    Medium = int.Parse(parts[7], CultureInfo.InvariantCulture),
                    Uid = parts[8].ToUpperInvariant(),
                };
            }
            catch (Exception)
            {
                return null;
            }
        }

        /// <summary>1 字节掩码 → 2 位大写十六进制（"FF" / "00"）。</summary>
        public static string Hex(int mask) => (mask & 0xFF).ToString("X2", CultureInfo.InvariantCulture);

        /// <summary>兼容旧版的 8 位二进制写法。</summary>
        public static string Bits(int mask) => Convert.ToString(mask & 0xFF, 2).PadLeft(8, '0');

        /// <summary>解析掩码：标准为 2 位十六进制，同时兼容旧的 8 位二进制。</summary>
        public static int ParseMask(string text)
        {
            if (text.Length == 2 &&
                byte.TryParse(text, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var hex))
            {
                return hex;
            }

            if (text.Length == 8 && text.All(c => c is '0' or '1'))
            {
                return Convert.ToInt32(text, 2);
            }

            throw new FormatException($"Invalid mask '{text}' (expected 2 hex chars)");
        }

        public static int CountBits(int mask)
        {
            var count = 0;
            for (var i = 0; i < 8; i++)
            {
                if ((mask & (1 << i)) != 0)
                {
                    count++;
                }
            }

            return count;
        }

        /// <summary>名称不能含 '_' / '!'（协议分隔符）。</summary>
        public static string Sanitize(string name)
        {
            var cleaned = (name ?? string.Empty)
                .Replace("_", "-")
                .Replace("!", ".")
                .Trim();

            if (Encoding.UTF8.GetByteCount(cleaned) > 20)
            {
                while (cleaned.Length > 0 && Encoding.UTF8.GetByteCount(cleaned) > 20)
                {
                    cleaned = cleaned[..^1];
                }
            }

            return cleaned;
        }
    }

    /// <summary>
    /// 下位机串口协议 Helper：负责帧收发、命令组合、读写流程与应答匹配。
    /// 帧格式见 CapsaSim/docs/COMMAND_REFERENCE.md。
    /// </summary>
    public sealed class CapsaProtocol : IDisposable
    {
        private readonly object _gate = new();
        private readonly List<byte> _buffer = new();

        public CapsaProtocol(string portName, int baudRate = 115200)
        {
            Port = new SerialPort(portName, baudRate, Parity.None, 8, StopBits.One)
            {
                ReadTimeout = 50,
                WriteTimeout = 1000,
                Encoding = Encoding.UTF8,
                NewLine = "\r\n",
            };
        }

        public SerialPort Port { get; }

        public string PortName => Port.PortName;

        public bool IsOpen
        {
            get
            {
                try
                {
                    return Port.IsOpen;
                }
                catch (Exception)
                {
                    return false;
                }
            }
        }

        /// <summary>常规命令超时（毫秒）。</summary>
        public int CommandTimeoutMs { get; set; } = 1500;

        /// <summary>探测超时（毫秒），用于刷新串口列表。</summary>
        public int ProbeTimeoutMs { get; set; } = 400;

        /// <summary>每包数据超时（毫秒）。</summary>
        public int PacketTimeoutMs { get; set; } = 2000;

        // ------------------------------------------------------------------ //
        // 生命周期
        // ------------------------------------------------------------------ //

        public void Open()
        {
            lock (_gate)
            {
                var justOpened = false;

                if (!Port.IsOpen)
                {
                    Port.Open();
                    justOpened = true;
                }

                _buffer.Clear();

                if (justOpened)
                {
                    // CH340 / CP2102 这类 USB 桥在端口刚打开时会清一次内部 FIFO，
                    // 紧接着写的第一帧经常整帧丢失。给一小段稳定时间并丢弃残留字节，
                    // 否则"探测"总在开门的瞬间失败，看起来像设备不存在。
                    Thread.Sleep(60);

                    try
                    {
                        Port.DiscardInBuffer();
                        Port.DiscardOutBuffer();
                    }
                    catch (Exception)
                    {
                        // 部分驱动不支持 Discard，忽略
                    }
                }
            }
        }

        /// <summary>丢弃尚未解析的接收数据（重试握手前清场）。</summary>
        public void ClearReceiveBuffer()
        {
            lock (_gate)
            {
                _buffer.Clear();

                try
                {
                    if (Port.IsOpen)
                    {
                        Port.DiscardInBuffer();
                    }
                }
                catch (Exception)
                {
                    // 驱动不支持时忽略
                }
            }
        }

        public void Dispose()
        {
            try
            {
                if (Port.IsOpen)
                {
                    Port.Close();
                }
            }
            catch (Exception)
            {
                // 端口已断开
            }

            Port.Dispose();
        }

        // ------------------------------------------------------------------ //
        // 帧收发
        // ------------------------------------------------------------------ //

        /// <summary>最近一次发出命令的时间（TickCount64 毫秒），心跳检测据此避让忙碌期。</summary>
        public long LastActivityMs { get; private set; }

        // ------------------------------------------------------------------ //
        // 会话占用标记
        //
        // ★ 心跳的判忙只看 LastActivityMs（最后一次**写**命令的时间），
        //   但读会话中上位机可能十几秒只收不发 —— 心跳会误判"空闲"并往
        //   正在流式回数据的总线上插 TYPE-CAPASA，两个接收者抢同一串口，
        //   [YES] 与 R_ 帧互相污染，读帧 2 s 超时（现象：Verify read failed）。
        //   传输会话期间必须显式抑制心跳。
        // ------------------------------------------------------------------ //
        private int _sessionDepth;

        /// <summary>传输会话（WriteMemory / ReadMemory）进行中。心跳看到它必须让路。</summary>
        public bool IsSessionBusy => Volatile.Read(ref _sessionDepth) > 0;

        /// <summary>
        /// 最近一次"端口级"故障（被占用 / 被移除 / 权限）的消息。
        /// 读阶段的这类异常不会冒出协议层（<see cref="ReadFrame"/> 吞掉返回 null），
        /// 心跳据此区分"没响应"和"端口没了"—— 后者立即断开，不给重试机会。
        /// </summary>
        public string? LastPortError { get; private set; }

        /// <summary>每次心跳探测前清零，保证 LastPortError 反映的是本次探测的故障。</summary>
        public void ClearPortError() => LastPortError = null;

        private void EnterSession() => Interlocked.Increment(ref _sessionDepth);

        private void ExitSession() => Interlocked.Decrement(ref _sessionDepth);

        public void WriteFrame(string payload)
        {
            LastActivityMs = Environment.TickCount64;
            Port.Write($">{payload}<");
        }

        /// <summary>
        /// 读流程里对每一个数据包回的应答。
        /// **必须用 >...&lt; 帧**：下位机的帧解析器（Proto_Poll）只认 > 起始、
        /// &lt; 结束的命令帧；用 [RX-OK] 会被当作噪声丢掉，下位机每包白等 2 秒
        /// 超时后回 [RX-ERR] 退出读会话 —— 现象就是"只读到第一包"，
        /// 芯片测试因此全部不合格（0/8）。
        /// 注意与 <see cref="WriteFrame"/> 的区别：那个是发命令，这里发的是应答。
        /// </summary>
        public void WriteAck(string token = "RX-OK")
            => Port.Write($">{token}<");

        /// <summary>读取一帧；超时返回 null。</summary>
        public ProtocolFrame? ReadFrame(int timeoutMs)
        {
            var deadline = Environment.TickCount64 + timeoutMs;

            while (true)
            {
                var frame = TryExtract();
                if (frame.HasValue)
                {
                    return frame;
                }

                var remaining = deadline - Environment.TickCount64;
                if (remaining <= 0)
                {
                    return null;
                }

                try
                {
                    var value = Port.ReadByte();
                    if (value >= 0)
                    {
                        _buffer.Add((byte)value);
                        // 收到的每个字节都算总线活动：读会话中上位机可能长时间
                        // 只收不发，不刷新这里的话心跳会误判空闲并抢总线
                        LastActivityMs = Environment.TickCount64;
                    }
                }
                catch (TimeoutException)
                {
                    // 继续等到 deadline
                }
                catch (Exception ex) when (ex is UnauthorizedAccessException || ex is IOException)
                {
                    // ★ 端口被别的程序占用 / 设备被移除：读阶段抛不出异常（这里吞掉
                    //   返回 null），但不记录的话心跳只会看到"没响应"而不知道原因
                    LastPortError = $"{ex.GetType().Name}: {ex.Message}";
                    return null;
                }
                catch (Exception)
                {
                    return null;
                }
            }
        }

        private ProtocolFrame? TryExtract()
        {
            while (true)
            {
                var startAngle = _buffer.IndexOf((byte)'>');
                var startBracket = _buffer.IndexOf((byte)'[');

                var candidates = new List<int>(2);
                if (startAngle >= 0)
                {
                    candidates.Add(startAngle);
                }

                if (startBracket >= 0)
                {
                    candidates.Add(startBracket);
                }

                if (candidates.Count == 0)
                {
                    if (_buffer.Count > 8192)
                    {
                        _buffer.RemoveRange(0, _buffer.Count - 1024);
                    }

                    return null;
                }

                var start = candidates.Min();
                var isData = _buffer[start] == (byte)'>';
                var closer = isData ? (byte)'<' : (byte)']';
                var end = _buffer.IndexOf(closer, start + 1);

                if (end < 0)
                {
                    if (start > 0)
                    {
                        _buffer.RemoveRange(0, start);
                    }

                    return null;
                }

                var payload = Encoding.UTF8.GetString(_buffer.GetRange(start + 1, end - start - 1).ToArray());
                _buffer.RemoveRange(0, end + 1);

                return new ProtocolFrame(isData ? FrameKind.Data : FrameKind.Response, payload);
            }
        }

        /// <summary>发命令并取第一帧应答。</summary>
        public ProtocolFrame? Exchange(string command, int timeoutMs = -1)
        {
            var start = Environment.TickCount64;
            WriteFrame(command);
            var frame = ReadFrame(timeoutMs < 0 ? CommandTimeoutMs : timeoutMs);
            TraceExchange(command, frame, (int)(Environment.TickCount64 - start));
            return frame;
        }

        /// <summary>发命令并要求应答命中给定集合。</summary>
        public bool TryCommand(string command, int timeoutMs = -1, params string[] accepted)
        {
            var frame = Exchange(command, timeoutMs);
            return frame is { } f && accepted.Any(f.Matches);
        }

        // ------------------------------------------------------------------ //
        // 命令追踪（错误详情窗口用）
        // ------------------------------------------------------------------ //

        /// <summary>一条命令交互记录：发了什么、卡回了什么、花了多久。</summary>
        public sealed record ProtocolTraceEntry(DateTimeOffset At, string Command, string Response, int ElapsedMs);

        private static readonly object TraceGate = new();
        private static readonly List<ProtocolTraceEntry> TraceLog = new();

        /// <summary>最近 40 条命令交互（全局，最新在末尾）。错误详情窗口据此还原现场。</summary>
        public static IReadOnlyList<ProtocolTraceEntry> RecentTrace()
        {
            lock (TraceGate)
            {
                return TraceLog.ToArray();
            }
        }

        private static void TraceExchange(string command, ProtocolFrame? frame, int elapsedMs)
        {
            try
            {
                lock (TraceGate)
                {
                    TraceLog.Add(new ProtocolTraceEntry(
                        DateTimeOffset.Now,
                        FoldTraceText(command),
                        DescribeFrame(frame),
                        elapsedMs));

                    if (TraceLog.Count > 40)
                    {
                        TraceLog.RemoveRange(0, TraceLog.Count - 40);
                    }
                }
            }
            catch
            {
                /* 追踪绝不能影响正常收发 */
            }
        }

        private static string DescribeFrame(ProtocolFrame? frame)
            => frame is { } f
                ? FoldTraceText(f.Kind == FrameKind.Data ? ">" + f.Payload + "<" : "[" + f.Payload + "]")
                : "<no reply / timeout>";

        /// <summary>W_ / R_ 数据包折叠成 "W_A1B2C3D4… (32 B)"，长应答截断 —— 追踪要能看清命令序列。</summary>
        private static string FoldTraceText(string text)
        {
            if ((text.StartsWith("W_", StringComparison.Ordinal) ||
                 text.StartsWith("R_", StringComparison.Ordinal)) &&
                text.Length > 12)
            {
                return $"{text[..10]}… ({(text.Length - 2) / 2} B)";
            }

            return text.Length > 64 ? text[..64] + "…" : text;
        }

        // ------------------------------------------------------------------ //
        // 高层命令
        // ------------------------------------------------------------------ //

        /// <summary>TYPE-CAPASA → [YES]。</summary>
        public bool Ping(int timeoutMs = -1)
            => TryCommand("TYPE-CAPASA", timeoutMs, "YES");

        /// <summary>GET_! → INFO 记录；未初始化返回 null。</summary>
        public CapsaInfo? ReadInfo(int timeoutMs = -1)
        {
            var frame = Exchange("GET_!", timeoutMs);
            if (frame is not { Kind: FrameKind.Data } f)
            {
                return null;
            }

            return CapsaInfo.Parse(f.Payload);
        }

        /// <summary>SET → [SET-OK]。</summary>
        public bool WriteInfo(CapsaInfo info, int timeoutMs = -1)
            => TryCommand("SET_" + info.ToSetFields(), timeoutMs, "SET-OK");

        /// <summary>
        /// 翻转某片的写保护位：读当前元数据 → 改 WP 掩码 → SET 回写。
        /// 其余 8 个字段全部用 '!' 占位（固件只更新 WP），PCF8574 立即生效。
        /// </summary>
        public bool SetWriteProtectBit(int chipAddress, bool protect, int timeoutMs = -1)
        {
            var bit = chipAddress - 0x50;
            if (bit < 0 || bit > 7)
            {
                return false;
            }

            var info = ReadInfo();
            if (info is null)
            {
                return false;
            }

            var mask = protect
                ? info.WriteProtectMask | (1 << bit)
                : info.WriteProtectMask & ~(1 << bit);

            return TryCommand($"SET_!_{mask:X2}_!_!_!_!_!_!_!", timeoutMs, "SET-OK");
        }

        /// <summary>UG_&lt;字节数&gt; → [UG-OK]。</summary>
        public bool SetUsedBytes(long usedBytes, int timeoutMs = -1)
            => TryCommand($"UG_{Math.Max(0, usedBytes)}", timeoutMs, "UG-OK");

        /// <summary>UGS → [UG-&lt;字节数&gt;]；未初始化 / 超时返回 null。</summary>
        public long? ReadUsedBytes(int timeoutMs = -1)
        {
            var frame = Exchange("UGS", timeoutMs);
            if (frame is not { Kind: FrameKind.Response } f)
            {
                return null;
            }

            const string prefix = "UG-";
            if (!f.Payload.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            {
                return null;
            }

            return long.TryParse(f.Payload[prefix.Length..], NumberStyles.Integer,
                CultureInfo.InvariantCulture, out var value)
                ? value
                : null;
        }

        /// <summary>
        /// DI_&lt;哈希&gt;_&lt;次数&gt; → [DI-OK]。两个字段都可以传 null 表示不修改（协议里的 !）。
        /// 哈希为 16 位十六进制（8 字节），由上位机计算，下位机只存储。
        /// </summary>
        public bool SetDataInfo(string? hashHex, long? writeCount, int timeoutMs = -1)
        {
            var hash = string.IsNullOrWhiteSpace(hashHex) ? "!" : hashHex!.Trim().ToUpperInvariant();
            var count = writeCount is { } value ? Math.Clamp(value, 0, uint.MaxValue).ToString(CultureInfo.InvariantCulture) : "!";

            return TryCommand($"DI_{hash}_{count}", timeoutMs, "DI-OK");
        }

        /// <summary>DIG → (哈希 16 位十六进制, 累计写入次数)；未初始化 / 超时返回 null。</summary>
        public (string Hash, long WriteCount)? ReadDataInfo(int timeoutMs = -1)
        {
            var frame = Exchange("DIG", timeoutMs);
            if (frame is not { Kind: FrameKind.Response } f)
            {
                return null;
            }

            const string prefix = "DIG-";
            if (!f.Payload.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            {
                return null;
            }

            var parts = f.Payload[prefix.Length..].Split('-');
            if (parts.Length != 2)
            {
                return null;
            }

            if (!long.TryParse(parts[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out var count))
            {
                return null;
            }

            return (parts[0].ToUpperInvariant(), count);
        }

        /// <summary>ROUTER → [RX-OK]。</summary>
        public bool SelectChip(int chipAddress, int timeoutMs = -1)
            => TryCommand($"ROUTER_0x{chipAddress:X2}", timeoutMs, "RX-OK");

        /// <summary>ADDR → [RX-OK] / [ERR_EXT]。</summary>
        public bool SetEndAddress(int address, int timeoutMs = -1)
            => TryCommand($"ADDR_0x{address:X4}", timeoutMs, "RX-OK");

        public bool BeginWrite(int timeoutMs = -1)
            => TryCommand("INT_W", timeoutMs, "RX-OK");

        public bool EndWrite(int timeoutMs = -1)
            => TryCommand("TX-OVER", timeoutMs, "RX-OK");

        public bool Abort(int timeoutMs = 500)
            => TryCommand("ABORT", timeoutMs, "ABORT-OK");

        /// <summary>写一包（≤16 字节，不足补 0x00）。返回是否收到 [RX-OK]。</summary>
        public bool WritePacket(ReadOnlySpan<byte> data, int timeoutMs = -1)
        {
            var packet = new byte[16];
            data[..Math.Min(16, data.Length)].CopyTo(packet);

            var builder = new StringBuilder("W_");
            for (var i = 0; i < packet.Length; i++)
            {
                if (i > 0)
                {
                    builder.Append(',');
                }

                builder.Append(packet[i].ToString("X2", CultureInfo.InvariantCulture));
            }

            return TryCommand(builder.ToString(), timeoutMs < 0 ? PacketTimeoutMs : timeoutMs, "RX-OK");
        }

        /// <summary>
        /// 写入一段数据到指定芯片（自动 ROUTER / ADDR / INT_W / W_* / TX-OVER）。
        /// <paramref name="progress"/> 参数为（本片百分比 0-100, 本片总字节数）。
        /// </summary>
        public bool WriteMemory(
            int chipAddress,
            byte[] payload,
            Action<int, int>? progress = null,
            CancellationToken cancellationToken = default)
        {
            if (payload.Length == 0)
            {
                return true;
            }

            EnterSession();
            try
            {
                if (!SelectChip(chipAddress) || !SetEndAddress(payload.Length - 1))
                {
                    return false;
                }

                var start = Exchange("INT_W");
                if (start is null)
                {
                    return false;
                }

                if (start.Value.Matches("ERR_WP") || start.Value.Matches("RX-ERR"))
                {
                    return false;
                }

                for (var offset = 0; offset < payload.Length; offset += 16)
                {
                    cancellationToken.ThrowIfCancellationRequested();

                    var size = Math.Min(16, payload.Length - offset);
                    if (!WritePacket(payload.AsSpan(offset, size)))
                    {
                        return false;
                    }

                    progress?.Invoke((offset + size) * 100 / payload.Length, payload.Length);
                }

                return EndWrite();
            }
            finally
            {
                ExitSession();
            }
        }

        /// <summary>
        /// 从指定芯片读出 <paramref name="length"/> 字节（自动 INT_R / 逐包应答 / TX-OVER）。
        /// <paramref name="progress"/> 参数为（本片百分比 0-100, 本片总字节数），与写入一致。
        /// </summary>
        public byte[]? ReadMemory(
            int chipAddress,
            int length,
            Action<int, int>? progress = null,
            CancellationToken cancellationToken = default)
        {
            if (length <= 0)
            {
                return Array.Empty<byte>();
            }

            EnterSession();
            try
            {
                return ReadMemoryCore(chipAddress, length, progress, cancellationToken);
            }
            finally
            {
                ExitSession();
            }
        }

        private byte[]? ReadMemoryCore(
            int chipAddress,
            int length,
            Action<int, int>? progress,
            CancellationToken cancellationToken)
        {
            if (!SelectChip(chipAddress) || !SetEndAddress(length - 1))
            {
                return null;
            }

            var start = Exchange("INT_R");
            if (start is not { } s || !s.Matches("RX-OK"))
            {
                return null;
            }

            var data = new byte[length];
            var offset = 0;

            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();

                var frame = ReadFrame(PacketTimeoutMs);
                if (frame is null)
                {
                    return null;
                }

                if (frame.Value.Kind == FrameKind.Data &&
                    string.Equals(frame.Value.Payload, "TX-OVER", StringComparison.OrdinalIgnoreCase))
                {
                    break;
                }

                if (frame.Value.Kind == FrameKind.Data && frame.Value.StartsWith("R_"))
                {
                    var bytes = frame.Value.Payload[2..]
                        .Split(',', StringSplitOptions.RemoveEmptyEntries)
                        .Select(t => byte.TryParse(t, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var v) ? v : (byte)0)
                        .ToArray();

                    var copy = Math.Min(bytes.Length, length - offset);
                    Array.Copy(bytes, 0, data, offset, copy);
                    offset += copy;

                    WriteAck("RX-OK");
                    // 与 WriteMemory 保持同一约定：百分比（0-100）+ 总字节数
                    progress?.Invoke(
                        Math.Min(offset, length) * 100 / Math.Max(1, length),
                        length);
                    continue;
                }

                // [RX-ERR] 或意外帧
                return null;
            }

            return data;
        }
    }
}
