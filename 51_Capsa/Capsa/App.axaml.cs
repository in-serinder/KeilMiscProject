using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Data.Core;
using Avalonia.Data.Core.Plugins;
using Avalonia.Markup.Xaml;
using Capsa.Services;
using Capsa.ViewModels;
using Capsa.Views;
using System.Linq;
using System.Text;

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

            AvaloniaXamlLoader.Load(this);
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
