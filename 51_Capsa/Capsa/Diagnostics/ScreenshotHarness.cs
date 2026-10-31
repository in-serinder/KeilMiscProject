using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.VisualTree;
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

            // ===== 读取模式：确认 ReadCompleted → 编辑器装载不炸线程 =====
            At(27300, () =>
            {
                var tabs = window.FindControl<TabControl>("MainTabs");
                if (tabs is not null)
                {
                    tabs.SelectedIndex = 1;
                }

                vm.Read.ReadVerifyCommand.Execute(null);
            });

            At(28600, () => Capture(window, Path.Combine(dir, "14-read-loaded.png")));

            // 读取模式的纯文本视图：编码下拉框应可切换（只影响显示解码）
            At(28700, () => vm.Read.Session.Mode = Models.EditorDisplayMode.Text);
            At(28900, () => Capture(window, Path.Combine(dir, "15-read-text.png")));

            At(29000, () =>
            {
                var combos = window.GetVisualDescendants()
                    .OfType<ComboBox>()
                    .Where(c => ReferenceEquals(c.DataContext, vm.Read.Session))
                    .Select(c => $"enabled={c.IsEnabled}")
                    .ToList();

                // 读取模式切编码：只应影响解码显示，缓冲区字节数必须不变
                var beforeLength = vm.Read.Session.ByteLength;
                var gb = vm.Read.Session.EncodingOptions.FirstOrDefault(o => o.Encoding.CodePage == 54936)
                         ?? vm.Read.Session.EncodingOptions[0];
                vm.Read.Session.SelectedEncoding = gb;
                var afterLength = vm.Read.Session.ByteLength;

                File.WriteAllText(Path.Combine(dir, "read-encoding-check.txt"),
                    $"combos=[{string.Join(",", combos)}] " +
                    $"bytes={beforeLength}->{afterLength} " +
                    $"{(beforeLength == afterLength ? "PASS" : "FAIL")}");
            });

            // ===== 写入模式：输入文本后切换编码应保持可读（真转码，不是重新解释字节）=====
            At(29100, () =>
            {
                var tabs = window.FindControl<TabControl>("MainTabs");
                if (tabs is not null)
                {
                    tabs.SelectedIndex = 0;
                }

                var session = vm.Write.Session;
                session.LoadBytes(Encoding.UTF8.GetBytes("中文测试 abc"), EditorChangeSource.Import, "note.txt");
                session.Mode = EditorDisplayMode.Text;
                session.SelectedEncoding = session.EncodingOptions.First(o => o.DisplayName == "UTF-8");
            });

            At(29300, () =>
            {
                var session = vm.Write.Session;
                var gbk = session.EncodingOptions.First(o => o.DisplayName == "GBK");
                var expected = Convert.ToHexString(gbk.Encoding.GetBytes("中文测试 abc"));

                session.SelectedEncoding = gbk;

                File.WriteAllText(Path.Combine(dir, "encoding-transcode.txt"),
                    $"text='{session.Text}'{Environment.NewLine}" +
                    $"bytes   ={Convert.ToHexString(session.Snapshot())}{Environment.NewLine}" +
                    $"expected={expected}{Environment.NewLine}" +
                    $"{(Convert.ToHexString(session.Snapshot()) == expected ? "PASS" : "FAIL")}");
            });

            At(29500, () => Capture(window, Path.Combine(dir, "16-encoding-transcoded.png")));
            At(29900, () => Environment.Exit(0));
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
                // ===== 文件往返：导入 photo.bmp → 写入（末尾附文件名）→ 读取页按已用空间读回 =====
                At(73500, () =>
                {
                    vm.Write.ApplyImportedFile("photo.bmp", BuildFilePayload(19_968));
                    vm.Write.WriteVerifyCommand.Execute(null);
                });

                // 第二次写入刚起步：所有卡片应已清空（不再残留上一轮 156 kbit 的填充）
                At(74300, () => Capture(window, Path.Combine(dir, "w4b-write-cleared.png")));
                At(82000, () => Capture(window, Path.Combine(dir, "w5-import-name.png")));
                At(97000, () => Capture(window, Path.Combine(dir, "w6-write-ok.png")));

                At(100000, () =>
                {
                    var tabs = window.FindControl<TabControl>("MainTabs");
                    if (tabs is not null)
                    {
                        tabs.SelectedIndex = 1;
                    }

                    vm.Read.ReadVerifyCommand.Execute(null);
                });

                At(102500, () => Capture(window, Path.Combine(dir, "r7-read-progress.png")));
                At(108000, () => Capture(window, Path.Combine(dir, "r8-read-restored.png")));

                // 切回写入模式：绿色覆盖层应消失，只剩蓝色占用
                At(108500, () =>
                {
                    var tabs = window.FindControl<TabControl>("MainTabs");
                    if (tabs is not null)
                    {
                        tabs.SelectedIndex = 0;
                    }
                });

                At(109500, () => Capture(window, Path.Combine(dir, "r9-overlay-cleared.png")));
                At(111000, () => Environment.Exit(0));
            }
        }

        /// <summary>生成一段任意格式的文件内容（用于验证扩展名 trailer 往返）。</summary>
        private static byte[] BuildFilePayload(int size)
        {
            var data = new byte[size];
            for (var i = 0; i < size; i++)
            {
                data[i] = (byte)((i * 31 + 7) & 0xFF);
            }

            return data;
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
