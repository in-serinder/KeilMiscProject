using System;
using System.IO;

namespace Capsa.Models
{
    /// <summary>
    /// 串口"端口级"故障的语义分类。
    ///
    /// 为什么要分类：**同一个异常类型在两个平台上含义不同**。
    /// Windows 的串口是独占的 —— 第二个进程打开会抛 <see cref="UnauthorizedAccessException"/>（拒绝访问），
    /// 含义就是"被占用"；而 Linux 的 tty 默认不排他，同样的异常几乎总是"权限不足"
    /// （当前用户不在串口组）。如果照搬 Windows 的文案，Linux 用户会被引导去
    /// 关一个根本没开着的串口助手，排查方向从一开始就是错的。
    ///
    /// 另外串口组的**名字也随发行版变**（Debian 系 dialout，Arch 系 uucp），
    /// 所以文案由 <see cref="UnixSerialGroup"/> 在运行时按 <c>/etc/group</c> 生成。
    /// </summary>
    public enum PortFailureKind
    {
        /// <summary>无故障。</summary>
        None = 0,

        /// <summary>权限不足（Linux 上基本等于"不在串口组"：dialout 或 uucp）。</summary>
        PermissionDenied,

        /// <summary>端口被其它进程占用（Windows 独占语义）。</summary>
        PortInUse,

        /// <summary>设备已从系统移除（USB 拔掉 / 驱动卸载）。</summary>
        DeviceRemoved,

        /// <summary>端口名不合法（Linux 少了 /dev/ 前缀、Windows 不是 COMx）。</summary>
        BadConfiguration,

        /// <summary>其它 I/O 故障。</summary>
        Io,

        /// <summary>归类不了。</summary>
        Unknown,
    }

    /// <summary>
    /// 一次端口故障：(分类, 面向用户的一句话)。<see cref="Message"/> 是**单行**的，
    /// 可以放进状态栏；更长的排查清单由界面自己按平台拼。
    /// </summary>
    public sealed record PortFailure(PortFailureKind Kind, string Message)
    {
        /// <summary>确定性故障：重试没有意义，心跳应立即断开（对应旧的 UnauthorizedAccessException / IOException 分支）。</summary>
        public bool IsFatal => Kind is PortFailureKind.PermissionDenied
            or PortFailureKind.PortInUse
            or PortFailureKind.DeviceRemoved
            or PortFailureKind.BadConfiguration;

        /// <summary>
        /// 把异常翻译成平台正确的语义 + 文案。端口名可为 null（枚举阶段没有具体端口）。
        /// </summary>
        public static PortFailure Classify(Exception? ex, string? portName)
        {
            var port = string.IsNullOrWhiteSpace(portName) ? "串口" : portName!.Trim();

            switch (ex)
            {
                case null:
                    return new PortFailure(PortFailureKind.None, string.Empty);

                case UnauthorizedAccessException:
                    if (OperatingSystem.IsWindows())
                    {
                        return new PortFailure(
                            PortFailureKind.PortInUse,
                            $"{port} 被其它程序占用（串口助手 / PuTTY / 另一个 Capsa 实例？）");
                    }

                    // ★ .NET 在 Unix 上把 open() 的**所有**失败都包成 UnauthorizedAccessException，
                    //   设备根本不存在（ENOENT）也是。所以先看设备在不在，
                    //   别把"没插"说成"没权限"——那会让用户白折腾 dialout 组。
                    if (!File.Exists(port))
                    {
                        return new PortFailure(
                            PortFailureKind.DeviceRemoved,
                            $"{port} 不存在（设备未插入，或端口名拼错）");
                    }

                    // 组名不能写死：Debian 系是 dialout，Arch / openSUSE 是 uucp。
                    // 照着错组名敲 usermod 会得到 "group does not exist"，等于把人带进沟里。
                    return new PortFailure(
                        PortFailureKind.PermissionDenied,
                        UnixSerialGroup.BuildHint(port));

                case FileNotFoundException:
                case DirectoryNotFoundException:
                    return new PortFailure(
                        PortFailureKind.DeviceRemoved,
                        $"{port} 不存在（设备未插入，或端口名拼错）");

                case ArgumentException:
                    return new PortFailure(
                        PortFailureKind.BadConfiguration,
                        OperatingSystem.IsWindows()
                            ? $"{port} 不是合法的串口名（应为 COM1 / COM3 …）"
                            : $"{port} 不是合法的串口名（应为 /dev/ttyUSB0、/dev/ttyACM0 …）");

                case PlatformNotSupportedException:
                    return new PortFailure(
                        PortFailureKind.BadConfiguration,
                        $"{port}：当前平台的串口支持不可用（System.IO.Ports 原生库缺失？）");

                case InvalidOperationException:
                    // SerialPort 在同一进程里被重复打开 / 状态非法，按"占用"处理
                    return new PortFailure(PortFailureKind.PortInUse, $"{port} 已在别处打开（{ex.Message}）");

                case IOException:
                    // Linux 上 USB 拔出表现为 ENXIO/ENODEV 的 IOException；Windows 上是设备移除
                    return new PortFailure(
                        PortFailureKind.DeviceRemoved,
                        $"{port} 已从系统移除（USB 拔出 / 驱动卸载）");

                default:
                    return new PortFailure(PortFailureKind.Unknown, $"{port}：{ex.Message}");
            }
        }

        /// <summary>
        /// 端口名归一化。Unix 上允许用户只敲 <c>ttyUSB0</c>（或者 <c>serial/by-id/…</c>），
        /// 统一补成 <c>/dev/…</c> 全路径；Windows 原样返回。
        /// </summary>
        public static string NormalizePortName(string? portName)
        {
            var name = (portName ?? string.Empty).Trim();
            if (name.Length == 0 || OperatingSystem.IsWindows())
            {
                return name;
            }

            // 已经是绝对路径（/dev/ttyUSB0、/dev/serial/by-id/usb-…）就不要再加前缀
            return name.StartsWith('/') ? name : "/dev/" + name;
        }
    }
}
