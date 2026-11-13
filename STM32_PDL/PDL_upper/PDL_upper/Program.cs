using Avalonia;
using System;
using System.Threading.Tasks;
using PDL_upper.Models;

namespace PDL_upper
{
    internal sealed class Program
    {
        [STAThread]
        public static void Main(string[] args)
        {
            AppArgs.Parse(args);
            if (AppArgs.SelfTest)
            {
                Console.WriteLine("PDL 上位机协议自检（golden vector）");
                var fails = Models.Protocol.Golden.Run();
                Environment.Exit(fails.Count == 0 ? 0 : 1);
                return;
            }
            if (AppArgs.Probe)
            {
                Environment.Exit(Models.Probe.RunAsync().GetAwaiter().GetResult());
                return;
            }
            // 全局异常落盘：UI 线程 / 后台任务 / 启动失败都有据可查
            // 注意：订阅 UI 异常必须等平台初始化完成（AfterSetup）——
            // 在 BuildAvaloniaApp 之前碰 Dispatcher.UIThread 会提前创建
            // 没有平台内核的 Dispatcher，MainLoop 直接抛 PlatformNotSupportedException（踩过）
            var crashPath = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "pdl_crash.txt");
            void RegisterCrashHandlers()
            {
                Avalonia.Threading.Dispatcher.UIThread.UnhandledException += (_, e) =>
                {
                    try
                    {
                        System.IO.File.AppendAllText(crashPath,
                            $"[UI {DateTime.Now:HH:mm:ss}] {e.Exception}\n");
                    }
                    catch { /* 落盘失败别再抛 */ }
                };
            }
            AppDomain.CurrentDomain.UnhandledException += (_, e) =>
            {
                try
                {
                    System.IO.File.AppendAllText(crashPath,
                        $"[APP {DateTime.Now:HH:mm:ss}] {e.ExceptionObject}\n");
                }
                catch { }
            };
            TaskScheduler.UnobservedTaskException += (_, e) =>
            {
                try
                {
                    System.IO.File.AppendAllText(crashPath,
                        $"[TASK {DateTime.Now:HH:mm:ss}] {e.Exception}\n");
                }
                catch { }
            };
            try
            {
                BuildAvaloniaApp()
                    .AfterSetup(_ => RegisterCrashHandlers())
                    .StartWithClassicDesktopLifetime(args);
            }
            catch (Exception ex)
            {
                System.IO.File.WriteAllText(crashPath, "[STARTUP] " + ex);
                throw;
            }
        }

        public static AppBuilder BuildAvaloniaApp()
            => AppBuilder.Configure<App>()
                .UsePlatformDetect()
                .WithInterFont()
                .LogToTrace();
    }
}
