using System;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media;
using PDL_upper.Models;
using PDL_upper.ViewModels;

namespace PDL_upper.Views
{
    public partial class MainWindow : Window
    {
        private static async Task RunAutoDemoAsync(MainWindowViewModel vm)
        {
            var trace = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "pdl_demo_trace.txt");
            void Log(string m) => System.IO.File.AppendAllText(trace, m + "\n");
            try
            {
                System.IO.File.Delete(trace);
                Log($"start auto={AppArgs.AutoConnect} dialogs={AppArgs.DemoDialogs}");
                await Task.Delay(600);
                await vm.ConnectCommand.ExecuteAsync(null);
                Log("connected");
                await Task.Delay(800);
                await vm.AcquireCommand.ExecuteAsync(null);
                Log("acquired");
                if (AppArgs.DemoPdl)
                {
                    await Task.Delay(6000);
                    Log("demo pdl: abort+save+playback");
                    await vm.DemoPdlAsync();
                    return;
                }
                if (AppArgs.DemoDialogs)
                {
                    await Task.Delay(600);
                    // 演示：弹两路曲线窗口 + 温度窗口，再开触发配置弹窗
                    vm.PopChannelCommand.Execute(vm.ChannelCharts[0]);
                    Log("popped ch0");
                    await Task.Delay(300);
                    vm.PopChannelCommand.Execute(vm.ChannelCharts[2]);
                    Log("popped ch2");
                    await Task.Delay(300);
                    vm.OpenTempWindowCommand.Execute(null);
                    Log("temp window opened");
                    await Task.Delay(12000);
                    vm.OpenTriggerDialogCommand.Execute(null);
                    Log("trigger dialog opened");
                }
            }
            catch (Exception ex)
            {
                Log("ERR " + ex);
            }
        }
        public MainWindow()
        {
            InitializeComponent();
            // 窗口/任务栏图标（exe 图标由 csproj ApplicationIcon 提供）
            try
            {
                Icon = new WindowIcon(
                    Avalonia.Platform.AssetLoader.Open(
                        new System.Uri("avares://PDL_upper/Assets/app-256.png")));
            }
            catch { /* 图标缺失不影响启动 */ }
            Closing += (_, _) =>
            {
                if (DataContext is MainWindowViewModel vm) vm.Shutdown();
            };
        }

        protected override void OnOpened(EventArgs e)
        {
            base.OnOpened(e);
            if (DataContext is MainWindowViewModel vm)
            {
                vm.Owner = this;
                vm.StartUiTimer();
                vm.RequestConfigDialog += kind => OpenConfigDialog(vm, kind);
                if (AppArgs.AutoConnect)
                    _ = RunAutoDemoAsync(vm);
            }
        }

        // 配置弹窗：模态，同一个 AcquireViewModel，改完立即生效
        private void OpenConfigDialog(MainWindowViewModel vm, string kind)
        {
            Window dlg;
            switch (kind)
            {
                case "trigger":
                    dlg = new Dialogs.TriggerDialog { DataContext = vm };
                    break;
                case "timing":
                    dlg = new Dialogs.TimingDialog { DataContext = vm };
                    break;
                default:
                    return;
            }
            _ = dlg.ShowDialog(this);
        }
    }
}
