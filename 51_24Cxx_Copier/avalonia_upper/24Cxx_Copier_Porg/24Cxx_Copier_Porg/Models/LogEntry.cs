namespace _24Cxx_Copier_Porg.Models;

/// <summary>Severity / source tag of a log line, used for colouring.</summary>
public enum LogKind
{
    /// <summary>Serial transport traffic ("[UART]").</summary>
    Uart,

    /// <summary>Operation / workflow step ("[OPT]").</summary>
    Operation,

    /// <summary>Compressed data-transfer progress ("[DATA]").</summary>
    Data,

    /// <summary>Error ("[ERR]").</summary>
    Error,

    /// <summary>Plain informational line.</summary>
    Info,
}

/// <summary>A single line in the operation log, tagged for the UI.</summary>
public sealed class LogEntry
{
    public LogEntry(LogKind kind, string message)
    {
        Kind = kind;
        Message = message;
    }

    /// <summary>Severity / source tag.</summary>
    public LogKind Kind { get; }

    /// <summary>The text with its "[TAG]" prefix already stripped.</summary>
    public string Message { get; }

    /// <summary>The bracketed prefix shown at the start of the line.</summary>
    public string Tag => Kind switch
    {
        LogKind.Uart => "[UART]",
        LogKind.Operation => "[OPT]",
        LogKind.Data => "[DATA]",
        LogKind.Error => "[ERR]",
        _ => "[INFO]",
    };

    /// <summary>Colour (hex) used for the tag, chosen by kind.</summary>
    public string TagColor => Kind switch
    {
        LogKind.Uart => "#3B78C3",     // blue
        LogKind.Operation => "#2E7D32", // green
        LogKind.Data => "#00838F",      // teal
        LogKind.Error => "#C62828",     // red
        _ => "#666666",                 // grey
    };

    /// <summary>The full formatted line shown in the UI.</summary>
    public string Display => $"{Tag} {Message}";

    /// <summary>Parses a raw tagged string into a <see cref="LogEntry"/>.</summary>
    public static LogEntry FromString(string raw)
    {
        if (string.IsNullOrEmpty(raw))
        {
            return new LogEntry(LogKind.Info, string.Empty);
        }

        if (raw.StartsWith("[UART]"))
        {
            return new LogEntry(LogKind.Uart, raw.Substring(6).TrimStart());
        }

        if (raw.StartsWith("[OPT]"))
        {
            return new LogEntry(LogKind.Operation, raw.Substring(5).TrimStart());
        }

        if (raw.StartsWith("[DATA]"))
        {
            return new LogEntry(LogKind.Data, raw.Substring(6).TrimStart());
        }

        if (raw.StartsWith("[ERR]"))
        {
            return new LogEntry(LogKind.Error, raw.Substring(5).TrimStart());
        }

        return new LogEntry(LogKind.Info, raw);
    }
}
