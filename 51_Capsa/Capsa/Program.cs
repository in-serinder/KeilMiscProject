using Avalonia;
using System;

namespace Capsa
{
    internal sealed class Program
    {
        // Initialization code. Don't use any Avalonia, third-party APIs or any
        // SynchronizationContext-reliant code before AppMain is called: things aren't initialized
        // yet and stuff might break.
        [STAThread]
        public static void Main(string[] args)
        {
            // 纯逻辑自检（不碰串口）：Capsa.exe --self-test [log.txt]
            if (args.Length >= 1 && string.Equals(args[0], "--self-test", StringComparison.OrdinalIgnoreCase))
            {
                Environment.Exit(Diagnostics.SelfTest.Run(args.Length >= 2 ? args[1] : null));
                return;
            }

            // 串口协议自检：Capsa.exe --sim-test COM1 [log.txt]
            if (args.Length >= 2 && string.Equals(args[0], "--sim-test", StringComparison.OrdinalIgnoreCase))
            {
                var logPath = args.Length >= 3 ? args[2] : "sim-test.log";
                Environment.Exit(Diagnostics.ProtocolSmokeTest.Run(args[1], logPath));
                return;
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
