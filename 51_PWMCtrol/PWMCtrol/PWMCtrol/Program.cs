using Avalonia;
using System;
using System.Runtime.InteropServices;

namespace PWMCtrol
{
    internal sealed class Program
    {
        // PER_MONITOR_AWARE_V2；清单里已声明，这里再兜一层，
        // 因为没声明时 Windows 会把窗口按 125%/150% 位图拉伸，布局宽度算少一截。
        private const int DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4;

        /// <summary>启动参数指定的串口（--port COM2），用于联调时自动选中。</summary>
        public static string? StartupPort { get; private set; }

        /// <summary>启动后自动连接（--autoconnect）。</summary>
        public static bool AutoConnect { get; private set; }

        /// <summary>联调用：连接后切 Mode3 并下发三组示例参数（--demo）。</summary>
        public static bool DemoMode { get; private set; }

        /// <summary>界面预览：只填本地示例值，不开串口、不发命令（--ui-demo）。</summary>
        public static bool UiDemo { get; private set; }

        [DllImport("user32.dll")]
        private static extern bool SetProcessDpiAwarenessContext(IntPtr value);

        // Initialization code. Don't use any Avalonia, third-party APIs or any
        // SynchronizationContext-reliant code before AppMain is called: things aren't initialized
        // yet and stuff might break.
        [STAThread]
        public static void Main(string[] args)
        {
            try
            {
                SetProcessDpiAwarenessContext(new IntPtr(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
            }
            catch
            {
                // 旧系统没有这个 API，忽略即可
            }

            for (var i = 0; i < args.Length; i++)
            {
                switch (args[i])
                {
                    case "--autoconnect":
                        AutoConnect = true;
                        break;
                    case "--demo":
                        DemoMode = true;
                        break;
                    case "--ui-demo":
                        UiDemo = true;
                        break;
                    case "--selftest":
                        PWMCtrol.Models.SelfTest.Run();
                        return;
                    case "--port" when i + 1 < args.Length:
                        StartupPort = args[++i];
                        break;
                }
            }

            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        }

        // Avalonia configuration, don't remove; also used by visual designer.
        public static AppBuilder BuildAvaloniaApp()
            => AppBuilder.Configure<App>()
                .UsePlatformDetect()
                .WithInterFont()
                .LogToTrace();
    }
}
