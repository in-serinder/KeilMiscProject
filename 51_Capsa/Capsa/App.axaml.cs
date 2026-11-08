using Avalonia;
using System;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Data.Core;
using Avalonia.Data.Core.Plugins;
using Avalonia.Markup.Xaml;
using Capsa.Services;
using Capsa.ViewModels;
using Capsa.Views;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Capsa
{
    public partial class App : Application
    {
        public override void Initialize()
        {
            // 注册代码页编码（GBK / GB2312 / Shift_JIS ...），供编辑器编码下拉框使用。
            Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

            // 组合根：前端阶段默认注入 Mock 实现，后端就绪后只需在此替换实现。
            AppServices.ConfigureFromEnvironment();

            InstallGlobalErrorTraps();

            AvaloniaXamlLoader.Load(this);
        }

        /// <summary>
        /// 全局兜底：UI 线程未处理异常 / 后台任务未观察异常 / 域未处理异常。
        /// 全部进 ErrorLog —— "报错但不知道报错内容"的问题从根上堵住，
        /// 任何一次崩溃都能在错误详情窗口里复制出完整堆栈。
        /// </summary>
        private void InstallGlobalErrorTraps()
        {
            Avalonia.Threading.Dispatcher.UIThread.UnhandledException += (_, e) =>
            {
                Capsa.Diagnostics.ErrorLog.Add("UI thread", e.Exception.Message, e.Exception);
                // 不标记 handled：让原有崩溃行为保留（避免掩盖问题），
                // 但日志里已经留了完整现场。
            };

            TaskScheduler.UnobservedTaskException += (_, e) =>
            {
                Capsa.Diagnostics.ErrorLog.Add("Unobserved task", e.Exception.Message, e.Exception);
                e.SetObserved();   // 已记录，不再让进程被最终化器线程杀掉
            };

            AppDomain.CurrentDomain.UnhandledException += (_, e) =>
            {
                if (e.ExceptionObject is Exception ex)
                {
                    Capsa.Diagnostics.ErrorLog.Add("Unhandled", ex.Message, ex);
                }
                else
                {
                    Capsa.Diagnostics.ErrorLog.Add("Unhandled", e.ExceptionObject?.ToString() ?? "unknown failure");
                }
            };
        }

        public override void OnFrameworkInitializationCompleted()
        {
            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                // Avoid duplicate validations from both Avalonia and the CommunityToolkit.
                DisableAvaloniaDataAnnotationValidation();
                var viewModel = new MainWindowViewModel();
                var window = new MainWindow { DataContext = viewModel };
                desktop.MainWindow = window;

                if (Capsa.Diagnostics.ScreenshotHarness.IsEnabled)
                {
                    Capsa.Diagnostics.ScreenshotHarness.Run(window, viewModel);
                }
            }

            base.OnFrameworkInitializationCompleted();
        }

        private void DisableAvaloniaDataAnnotationValidation()
        {
            var dataValidationPluginsToRemove =
                BindingPlugins.DataValidators.OfType<DataAnnotationsValidationPlugin>().ToArray();

            foreach (var plugin in dataValidationPluginsToRemove)
            {
                BindingPlugins.DataValidators.Remove(plugin);
            }
        }
    }
}
