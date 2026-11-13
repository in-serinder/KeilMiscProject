using System;
using System.Collections.Generic;
using System.Globalization;

namespace PWMCtrol.Models;

/// <summary>
/// 纯逻辑自检（--selftest）：不打开串口、不起 GUI，验证帧解析、ECHO 解析与命令拼接。
/// 输出走 stdout，方便在命令行里跑。
/// </summary>
public static class SelfTest
{
    private static int _failed;

    public static void Run()
    {
        Console.WriteLine("==============================================================");
        Console.WriteLine(" PWMCTROL 上位机 协议层自检");
        Console.WriteLine("==============================================================");
        Console.WriteLine("串口: " + (SerialTransport.AvailablePorts() is { Length: > 0 } ports
            ? string.Join(", ", ports)
            : "（无）"));
        Console.WriteLine("--------------------------------------------------------------");

        TestFrameCodec();
        TestEchoParse();
        TestCommandFormat();

        Console.WriteLine("--------------------------------------------------------------");
        Console.WriteLine(_failed == 0 ? "全部通过" : $"{_failed} 项失败");
        Console.WriteLine("==============================================================");
    }

    private static void TestFrameCodec()
    {
        var frames = new List<string>();
        var codec = new FrameCodec();

        // 噪声 + 粘包 + 半帧续传，模拟真实串口
        var payload = "garbage[YES]noise[ECHO_1_1000.02_50.0_1234_R][OK_STOP]";
        var bytes = System.Text.Encoding.ASCII.GetBytes(payload);
        codec.Feed(bytes, bytes.Length, frames.Add);

        Check("帧解析数量 = 3", frames.Count == 3, string.Join(" | ", frames));
        Check("帧1 = YES", frames.Count > 0 && frames[0] == "YES");
        Check("帧2 是 ECHO", frames.Count > 1 && frames[1].StartsWith("ECHO_"));

        // 超长帧必须整帧丢弃
        var longFrame = System.Text.Encoding.ASCII.GetBytes("[" + new string('A', 200) + "][PONG]");
        var longFrames = new List<string>();
        codec.Feed(longFrame, longFrame.Length, longFrames.Add);
        Check("超长帧被丢弃且后续帧正常", longFrames.Count == 1 && longFrames[0] == "PONG",
            string.Join(" | ", longFrames));
    }

    private static void TestEchoParse()
    {
        var run = ChannelEcho.Parse("ECHO_1_1000.02_50.0_1234_R");
        Check("ECHO 通道号", run?.Index == 1);
        Check("ECHO 频率", run is not null && Math.Abs(run.Frequency - 1000.02) < 0.001);
        Check("ECHO 电压", run?.Millivolts == 1234);
        Check("ECHO 状态", run?.State == 'R');

        var noAdc = ChannelEcho.Parse("ECHO_0_480.00_88.0_null_E");
        Check("null 电压解析", noAdc is { Millivolts: null, State: 'E' });

        var bad = ChannelEcho.Parse("ECHO_9_1_1_0_R");
        Check("非法通道号返回 null", bad is null);

        Check("非 ECHO 帧返回 null", ChannelEcho.Parse("OK_STOP") is null);
    }

    private static void TestCommandFormat()
    {
        var inv = CultureInfo.InvariantCulture;
        var body = string.Format(inv, "CONFIG_{0}_{1}_{2}_{3}_{4}",
            0, (1500.0).ToString("0.###", inv), (50.0).ToString("0.0", inv), "F", 1);
        Check("CONFIG 拼装", body == "CONFIG_0_1500_50.0_F_1", body);

        var freq = (999000.0).ToString("0.###", inv);
        Check("999kHz 不被科学计数法改写", freq == "999000", freq);

        var duty = (62.5).ToString("0.0", inv);
        Check("占空比一位小数", duty == "62.5", duty);
    }

    private static void Check(string name, bool ok, string? detail = null)
    {
        if (ok)
        {
            Console.WriteLine($"  PASS  {name}");
            return;
        }

        _failed++;
        Console.WriteLine($"  FAIL  {name}" + (detail is null ? string.Empty : $"  → {detail}"));
    }
}
