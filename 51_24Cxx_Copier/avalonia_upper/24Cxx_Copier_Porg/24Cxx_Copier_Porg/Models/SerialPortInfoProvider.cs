using System;
using System.Collections.Generic;
using System.IO.Ports;
using Microsoft.Win32;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// Enumerates the available serial ports together with a human friendly
/// description (e.g. "USB-SERIAL CH340 (COM3)").
///
/// <see cref="SerialPort.GetPortNames"/> only yields the COM name, so the
/// description is resolved from the Windows registry without requiring any
/// extra package (no WMI / System.Management dependency).
/// </summary>
public static class SerialPortInfoProvider
{
    /// <summary>A single enumerated serial port.</summary>
    public readonly record struct PortEntry(string PortName, string Description);

    /// <summary>
    /// Returns the available serial ports with their descriptions, ordered by
    /// port name. Falls back to the bare names when a description cannot be
    /// resolved or when not running on Windows.
    /// </summary>
    public static IReadOnlyList<PortEntry> GetPorts()
    {
        var names = SerialPort.GetPortNames();
        var descriptions = TryGetDescriptions();

        var result = new List<PortEntry>(names.Length);
        foreach (var name in names)
        {
            var description = descriptions.TryGetValue(name, out var d) && !string.IsNullOrWhiteSpace(d)
                ? d
                : name;
            result.Add(new PortEntry(name, description));
        }

        // Sort by trailing numeric part when possible (COM2 < COM10).
        result.Sort((a, b) => ComparePortNames(a.PortName, b.PortName));
        return result;
    }

    /// <summary>
    /// Reads friendly names for all COM ports from the registry
    /// (<c>HKLM\SYSTEM\CurrentControlSet\Enum\...</c>).
    /// </summary>
    private static Dictionary<string, string> TryGetDescriptions()
    {
        var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        if (!OperatingSystem.IsWindows())
        {
            return map;
        }

        try
        {
            using var enumKey = Registry.LocalMachine.OpenSubKey(
                @"SYSTEM\CurrentControlSet\Enum");
            if (enumKey is null)
            {
                return map;
            }

            foreach (var busName in enumKey.GetSubKeyNames())
            {
                using var busKey = enumKey.OpenSubKey(busName);
                if (busKey is null)
                {
                    continue;
                }

                foreach (var deviceName in busKey.GetSubKeyNames())
                {
                    using var deviceKey = busKey.OpenSubKey(deviceName);
                    if (deviceKey is null)
                    {
                        continue;
                    }

                    foreach (var instanceName in deviceKey.GetSubKeyNames())
                    {
                        using var instanceKey = deviceKey.OpenSubKey(instanceName);
                        if (instanceKey is null)
                        {
                            continue;
                        }

                        var friendly = instanceKey.GetValue("FriendlyName") as string;
                        if (string.IsNullOrWhiteSpace(friendly))
                        {
                            continue;
                        }

                        // FriendlyName often contains "(COM3)".
                        var comName = ExtractComName(friendly);
                        if (comName is not null)
                        {
                            map[comName] = friendly;
                        }
                    }
                }
            }
        }
        catch
        {
            // Registry access can fail under restricted environments; ignore.
        }

        return map;
    }

    /// <summary>Extracts "COMx" from a string like "... (COM3)".</summary>
    private static string? ExtractComName(string text)
    {
        var open = text.LastIndexOf('(');
        var close = text.LastIndexOf(')');
        if (open >= 0 && close > open)
        {
            var inner = text.Substring(open + 1, close - open - 1).Trim();
            if (inner.StartsWith("COM", StringComparison.OrdinalIgnoreCase))
            {
                return inner;
            }
        }

        return null;
    }

    /// <summary>Compares COM names by their numeric suffix, falling back to text.</summary>
    private static int ComparePortNames(string a, string b)
    {
        var na = ExtractNumber(a);
        var nb = ExtractNumber(b);
        if (na.HasValue && nb.HasValue)
        {
            return na.Value.CompareTo(nb.Value);
        }

        return string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
    }

    private static int? ExtractNumber(string portName)
    {
        if (portName.StartsWith("COM", StringComparison.OrdinalIgnoreCase)
            && int.TryParse(portName.AsSpan(3), out var value))
        {
            return value;
        }

        return null;
    }
}
