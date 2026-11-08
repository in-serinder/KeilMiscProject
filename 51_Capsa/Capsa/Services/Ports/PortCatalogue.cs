using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Capsa.Services.Ports
{
    /// <summary>
    /// Linux 下的端口信息补全（描述 + 占用诊断）。
    ///
    /// 两个平台在这里的差距是"信息"，不是"能力"：
    /// Windows 用 SetupAPI/注册表能拿到 "USB-SERIAL CH340 (COM3)" 这种人类可读描述；
    /// Linux 什么都没有，`GetPortNames()` 只给一串 <c>/dev/ttyUSB0</c>，
    /// 插两个 USB-TTL 就分不清哪个是哪个。这里直接读 sysfs，**不引入任何 P/Invoke**。
    ///
    /// 全部是 best-effort：任何一步失败都退回 "Serial port"，绝不让枚举抛异常。
    /// </summary>
    internal static class PortCatalogue
    {
        private const string Fallback = "Serial port";

        /// <summary>
        /// 取端口的人类可读描述。Linux 上形如 <c>USB Serial (1a86:7523)</c>；
        /// 其它平台保持原样（Windows 侧仍由系统给出 COM 号，够用了）。
        /// </summary>
        public static string Describe(string portName)
        {
            if (!OperatingSystem.IsLinux() || string.IsNullOrWhiteSpace(portName))
            {
                return Fallback;
            }

            try
            {
                var node = Path.GetFileName(portName.TrimEnd('/'));
                if (node.Length == 0)
                {
                    return Fallback;
                }

                // /sys/class/tty/ttyUSB0/device 指向 USB 接口目录（1-1:1.0）
                var deviceDir = $"/sys/class/tty/{node}/device";
                if (!Directory.Exists(deviceDir))
                {
                    return Fallback;
                }

                // 接口级描述（"USB Serial"），以及 usb 设备级的 product / VID / PID
                var interfaceName = ReadFirstLine(Path.Combine(deviceDir, "interface"));
                var product = ReadFirstLine(Path.Combine(deviceDir, "..", "product"));
                var vendorId = ReadFirstLine(Path.Combine(deviceDir, "..", "idVendor"));
                var productId = ReadFirstLine(Path.Combine(deviceDir, "..", "idProduct"));

                var name = !string.IsNullOrWhiteSpace(product) ? product
                    : !string.IsNullOrWhiteSpace(interfaceName) ? interfaceName
                    : string.Empty;

                var ids = !string.IsNullOrWhiteSpace(vendorId) && !string.IsNullOrWhiteSpace(productId)
                    ? $"{vendorId}:{productId}"
                    : string.Empty;

                if (name.Length == 0 && ids.Length == 0)
                {
                    return Fallback;
                }

                if (name.Length == 0)
                {
                    return ids;
                }

                return ids.Length == 0 ? name : $"{name} ({ids})";
            }
            catch (Exception)
            {
                return Fallback;
            }
        }

        private static string ReadFirstLine(string path)
        {
            try
            {
                if (!File.Exists(path))
                {
                    return string.Empty;
                }

                return File.ReadLines(path).FirstOrDefault()?.Trim() ?? string.Empty;
            }
            catch (Exception)
            {
                return string.Empty;
            }
        }
    }

    /// <summary>
    /// 端口占用诊断（仅 Linux 有意义）。
    ///
    /// Windows 的串口独占让"被占用"自己会报错；Linux 的 tty 不排他 ——
    /// 两个进程能同时打开、**都不报错**，症状变成"帧被撕裂 / 没有应答"，
    /// 用户看不出是谁在抢。这里在**失败之后**扫一遍 /proc/[pid]/fd，
    /// 把真正持有该 tty 的进程名列出来。
    ///
    /// 只用于诊断文案，不参与连接决策：ModemManager 这类探测进程持有时间极短，
    /// 拿它当"占用"直接拒绝连接会引入偶发失败。
    /// </summary>
    internal static class PortInspector
    {
        private const int MaxHolders = 3;

        /// <summary>扫描预算：诊断信息不值得让探测多等 —— 超时就返回已找到的。</summary>
        private const int ScanBudgetMs = 150;

        /// <summary>形如 <c>；持有该端口的进程：modem-manager (1234)</c>，没有则返回空串。</summary>
        public static string Suffix(string? portName)
        {
            var holders = FindHolders(portName);
            return holders.Count == 0
                ? string.Empty
                : "；持有该端口的进程：" + string.Join("、", holders);
        }

        /// <summary>找出打开着该 tty 的其它进程（不含本进程）。</summary>
        public static IReadOnlyList<string> FindHolders(string? portName)
        {
            if (!OperatingSystem.IsLinux() || string.IsNullOrWhiteSpace(portName))
            {
                return Array.Empty<string>();
            }

            try
            {
                var port = portName!.Trim();

                // by-id 这类软链要先把真实路径也放进比对集合
                var targets = new HashSet<string>(StringComparer.Ordinal) { port };
                try
                {
                    var resolved = File.ResolveLinkTarget(port, true);
                    if (resolved is not null)
                    {
                        targets.Add(resolved.FullName);
                    }
                }
                catch (Exception)
                {
                    // 软链解析失败不影响主比对
                }

                var self = Environment.ProcessId;
                var results = new List<string>();
                var clock = System.Diagnostics.Stopwatch.StartNew();

                foreach (var procDir in Directory.EnumerateDirectories("/proc"))
                {
                    if (results.Count >= MaxHolders || clock.ElapsedMilliseconds > ScanBudgetMs)
                    {
                        break;
                    }

                    if (!int.TryParse(Path.GetFileName(procDir), out var pid) || pid == self)
                    {
                        continue;
                    }

                    IEnumerable<string> descriptors;
                    try
                    {
                        descriptors = Directory.EnumerateFileSystemEntries(Path.Combine(procDir, "fd"));
                    }
                    catch (Exception)
                    {
                        continue;   // 别人的进程目录读不了，跳过
                    }

                    foreach (var descriptor in descriptors)
                    {
                        string? link = null;
                        try
                        {
                            link = new FileInfo(descriptor).LinkTarget;
                        }
                        catch (Exception)
                        {
                            // fd 恰好被关闭 / 权限不足
                        }

                        if (link is null || !targets.Contains(link))
                        {
                            continue;
                        }

                        results.Add($"{ReadCommandName(procDir)} ({pid})");
                        break;
                    }
                }

                return results;
            }
            catch (Exception)
            {
                return Array.Empty<string>();
            }
        }

        private static string ReadCommandName(string procDir)
        {
            try
            {
                var comm = Path.Combine(procDir, "comm");
                if (File.Exists(comm))
                {
                    var name = File.ReadAllText(comm).Trim();
                    if (name.Length > 0)
                    {
                        return name;
                    }
                }
            }
            catch (Exception)
            {
                // 进程已退出
            }

            return "pid";
        }
    }
}
