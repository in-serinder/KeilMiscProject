namespace PWMCtrol.Models;

/// <summary>一帧下位机上报的通道状态（ECHO_…）。</summary>
public sealed class ChannelEcho
{
    public int Index { get; init; }
    public double Frequency { get; init; }
    public double Duty { get; init; }

    /// <summary>电压探头读数；null 表示该通道未使能采集。</summary>
    public int? Millivolts { get; init; }

    /// <summary>R 运行 / S 停止 / E 到期 / F 故障。</summary>
    public char State { get; init; } = 'S';

    /// <summary>解析 <c>ECHO_1_1000.02_50.0_1234_R</c>，失败返回 null。</summary>
    public static ChannelEcho? Parse(string body)
    {
        if (!body.StartsWith("ECHO_", System.StringComparison.Ordinal))
        {
            return null;
        }

        var parts = body.Split('_');
        if (parts.Length < 5)
        {
            return null;
        }

        if (!int.TryParse(parts[1], out var index) || index is < 0 or > 2)
        {
            return null;
        }

        if (!double.TryParse(parts[2], System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture, out var freq))
        {
            return null;
        }

        if (!double.TryParse(parts[3], System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture, out var duty))
        {
            return null;
        }

        int? mv = null;
        if (parts[4] != "null" && int.TryParse(parts[4], out var parsed))
        {
            mv = parsed;
        }

        var state = parts.Length > 5 && parts[5].Length > 0 ? parts[5][0] : 'R';
        return new ChannelEcho
        {
            Index = index,
            Frequency = freq,
            Duty = duty,
            Millivolts = mv,
            State = state,
        };
    }
}
