using System;
using Avalonia.Media;

namespace PWMCtrol.Models;

public enum LogKind
{
    Info,
    Tx,
    Rx,
    Warn,
    Error,
}

/// <summary>一条串口/事件日志。</summary>
public sealed class LogEntry
{
    public required string Time { get; init; }
    public required string Text { get; init; }
    public LogKind Kind { get; init; } = LogKind.Info;

    public IBrush Brush => Kind switch
    {
        LogKind.Tx => new SolidColorBrush(Color.Parse("#4E9BF0")),
        LogKind.Rx => new SolidColorBrush(Color.Parse("#57C785")),
        LogKind.Warn => new SolidColorBrush(Color.Parse("#E0B341")),
        LogKind.Error => new SolidColorBrush(Color.Parse("#E06A6A")),
        _ => new SolidColorBrush(Color.Parse("#8A95A5")),
    };

    public static LogEntry Create(LogKind kind, string text) => new()
    {
        Time = DateTime.Now.ToString("HH:mm:ss.fff"),
        Text = text,
        Kind = kind,
    };
}
