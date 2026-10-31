using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Capsa.Models;
using Capsa.ViewModels;
using Capsa.ViewModels.Dialogs;
using Capsa.Views.Dialogs;

namespace Capsa.Diagnostics
{
    /// <summary>
    /// 临时 UI 走查工具：设置环境变量 CAPSA_SHOT 后启动，会自动连接设备、导入示例内容，
    /// 依次截取多张界面截图后退出。
    /// </summary>
    internal static class ScreenshotHarness
    {
        public static bool IsEnabled => !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("CAPSA_SHOT"));

        /// <summary>CAPSA_SHOT_REAL=1 时只跑“扫描 → 连接 → 截图”，用于真机联调。</summary>
        private static bool RealPlan => Environment.GetEnvironmentVariable("CAPSA_SHOT_REAL") == "1";

        public static void Run(Window window, MainWindowViewModel vm)
        {
            var dir = Environment.GetEnvironmentVariable("CAPSA_SHOT")!;
            Directory.CreateDirectory(dir);

            void At(double ms, Action action) =>
                DispatcherTimer.RunOnce(action, TimeSpan.FromMilliseconds(ms));

            if (RealPlan)
            {
                RunRealPlan(window, vm, dir);
                return;
            }

            At(1400, () => Capture(window, Path.Combine(dir, "01-empty.png")));


            At(1600, async () =>
            {
                vm.TopBar.SelectedPort = vm.TopBar.Ports.Count > 0 ? vm.TopBar.Ports[0] : null;
                await vm.TopBar.ToggleConnectionCommand.ExecuteAsync(null);
            });

            At(3600, () => vm.Write.Session.LoadBytes(BuildDemoPayload(), EditorChangeSource.Import, "demo.bin"));
            At(4200, () => Capture(window, Path.Combine(dir, "02-hex.png")));

            At(4400, () => vm.Write.Session.Mode = EditorDisplayMode.Dual);
            At(5200, () => Capture(window, Path.Combine(dir, "03-dual.png")));

            At(5400, () => vm.Write.Session.Mode = EditorDisplayMode.Text);
            At(6000, () => Capture(window, Path.Combine(dir, "04-text.png")));

            At(6200, () =>
            {
                var tabs = window.FindControl<TabControl>("MainTabs");
                if (tabs is not null)
                {
                    tabs.SelectedIndex = 1;
                }
            });
            At(6800, () => Capture(window, Path.Combine(dir, "05-read.png")));

            // ===== 初始化存储卡窗口（走真实命令 → 对话框服务链路）=====
            At(7000, () => vm.Write.ReinitializeCommand.Execute(null));

            At(7900, () =>
            {
                var dialog = window.OwnedWindows.FirstOrDefault();
                Capture(dialog, Path.Combine(dir, "06-init-idle.png"));
            });

            At(8100, () =>
            {
                var dialog = window.OwnedWindows.FirstOrDefault();
                if (dialog?.DataContext is InitializeCardViewModel initVm)
                {
                    initVm.InitializeCommand.Execute(null);
                }
            });

            At(9400, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "07-init-running.png")));
            At(12600, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "08-init-done.png")));

            At(13100, () => window.OwnedWindows.FirstOrDefault()?.Close());
            At(15200, () => Capture(window, Path.Combine(dir, "09-main-after-init.png")));

            // ===== 芯片健康检查窗口 =====
            At(15400, () =>
            {
                var tabs = window.FindControl<TabControl>("MainTabs");
                if (tabs is not null)
                {
                    tabs.SelectedIndex = 0;
                }

                vm.Write.CheckHealthCommand.Execute(null);
            });

            At(16400, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "10-health-idle.png")));

            At(16600, () =>
            {
                if (window.OwnedWindows.FirstOrDefault()?.DataContext is ChipHealthViewModel healthVm)
                {
                    healthVm.StartTestCommand.Execute(null);
                }
            });

            At(17400, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "11-health-write.png")));
            At(18800, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "12-health-read.png")));
            At(26500, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "13-health-done.png")));
            At(27100, () => window.OwnedWindows.FirstOrDefault()?.Close());
            At(27700, () => Environment.Exit(0));
        }

        /// <summary>真机联调：扫描串口 → 自动选择并连接 → 抓图。</summary>
        private static void RunRealPlan(Window window, MainWindowViewModel vm, string dir)
        {
            void At(double ms, Action action) =>
                DispatcherTimer.RunOnce(action, TimeSpan.FromMilliseconds(ms));

            At(1200, () => vm.TopBar.RefreshPortsCommand.Execute(null));
            At(4200, () => Capture(window, Path.Combine(dir, "r1-scanned.png")));

            At(4600, () => vm.TopBar.ToggleConnectionCommand.Execute(null));
            At(7600, () => Capture(window, Path.Combine(dir, "r2-connected.png")));

            // 未初始化时主界面会自动拉起初始化窗口
            At(8000, () => Capture(window.OwnedWindows.FirstOrDefault() ?? window,
                                   Path.Combine(dir, "r3-init-dialog.png")));

            At(8400, () =>
            {
                if (window.OwnedWindows.FirstOrDefault()?.DataContext is ViewModels.Dialogs.InitializeCardViewModel initVm)
                {
                    initVm.InitializeCommand.Execute(null);
                }
            });

            At(14000, () => Capture(window.OwnedWindows.FirstOrDefault() ?? window,
                                    Path.Combine(dir, "r4-init-running.png")));

            At(30000, () => Capture(window.OwnedWindows.FirstOrDefault() ?? window,
                                    Path.Combine(dir, "r5-init-done.png")));

            At(32000, () => window.OwnedWindows.FirstOrDefault()?.Close());
            At(34000, () => Capture(window, Path.Combine(dir, "r6-main-after-init.png")));

            // ===== 写入 156 kbit（19968 B）：8 片按地址顺序切片，卡片跟着进度涨 =====
            At(36000, () =>
            {
                vm.Write.Session.LoadBytes(BuildArrayPayload(19_968), EditorChangeSource.Import, "payload-156kbit.bin");
                vm.Write.WriteVerifyCommand.Execute(null);
            });
            At(43000, () => Capture(window, Path.Combine(dir, "w1-writing-0x50.png")));
            At(51000, () => Capture(window, Path.Combine(dir, "w2-writing-0x51.png")));
            At(64000, () => Capture(window, Path.Combine(dir, "w3-write-done.png")));
            At(70000, () => Capture(window, Path.Combine(dir, "w4-after-ugs.png")));
            // 整片覆写测试很慢（8 片 × 2 图案 ≈ 2 分钟），默认不跑，
            // 需要走查健康窗口时设置 CAPSA_SHOT_HEALTH=1。
            if (Environment.GetEnvironmentVariable("CAPSA_SHOT_HEALTH") == "1")
            {
                // ===== 健康检查窗口（整片覆写）=====
                At(73000, () => vm.Write.CheckHealthCommand.Execute(null));
                At(76000, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "h1-idle.png")));
                At(76500, () =>
                {
                    if (window.OwnedWindows.FirstOrDefault()?.DataContext is ChipHealthViewModel health)
                    {
                        health.StartTestCommand.Execute(null);
                    }
                });

                At(86000, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "h2-writing.png")));
                At(92000, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "h3-verifying.png")));
                At(102000, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "h4-pattern2.png")));
                At(103000, () =>
                {
                    if (window.OwnedWindows.FirstOrDefault()?.DataContext is ChipHealthViewModel health)
                    {
                        health.CancelCommand.Execute(null);
                    }
                });

                At(105000, () => Capture(window.OwnedWindows.FirstOrDefault(), Path.Combine(dir, "h5-canceled.png")));
                At(107000, () => Environment.Exit(0));
            }
            else
            {
                At(73000, () => Environment.Exit(0));
            }
        }

        /// <summary>生成指定字节数的测试载荷（156 kbit = 19968 B）。</summary>
        private static byte[] BuildArrayPayload(int size)
        {
            var data = new byte[size];
            for (var i = 0; i < size; i++)
            {
                data[i] = (byte)(i & 0xFF);
            }

            return data;
        }

        private static byte[] BuildDemoPayload()
        {
            var builder = new StringBuilder();
            builder.AppendLine("Capsa demo payload");
            builder.AppendLine("I2C EEPROM 24C256 - 32 KB");
            builder.AppendLine("--------------------------------");
            for (var i = 0; i < 24; i++)
            {
                builder.AppendLine($"row {i:D3} : the quick brown fox jumps over the lazy dog");
            }

            return Encoding.UTF8.GetBytes(builder.ToString());
        }

        private static void Capture(Window? window, string path)
        {
            try
            {
                if (window is null)
                {
                    return;
                }

                var size = new PixelSize((int)window.Bounds.Width, (int)window.Bounds.Height);
                if (size.Width <= 0 || size.Height <= 0)
                {
                    return;
                }

                using var bitmap = new RenderTargetBitmap(size, new Vector(96, 96));
                bitmap.Render(window);
                bitmap.Save(path);
            }
            catch (Exception ex)
            {
                File.WriteAllText(path + ".error.txt", ex.ToString());
            }
        }
    }
}
