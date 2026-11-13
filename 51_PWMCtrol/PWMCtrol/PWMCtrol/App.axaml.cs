using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Data.Core;
using Avalonia.Data.Core.Plugins;
using Avalonia.Markup.Xaml;
using PWMCtrol.ViewModels;
using PWMCtrol.Views;
using System.Linq;

namespace PWMCtrol
{
    public partial class App : Application
    {
        public override void Initialize()
        {
            AvaloniaXamlLoader.Load(this);
        }

        public override void OnFrameworkInitializationCompleted()
        {
            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                // Avoid duplicate validations from both Avalonia and the CommunityToolkit. 
                // More info: https://docs.avaloniaui.net/docs/guides/development-guides/data-validation#manage-validationplugins
                DisableAvaloniaDataAnnotationValidation();

                var viewModel = new MainWindowViewModel();
                if (!string.IsNullOrWhiteSpace(Program.StartupPort))
                {
                    viewModel.SelectedPort = Program.StartupPort;
                }

                desktop.MainWindow = new MainWindow
                {
                    DataContext = viewModel,
                };

                if (Program.UiDemo)
                {
                    viewModel.LoadUiDemo();
                }

                if (Program.AutoConnect)
                {
                    Avalonia.Threading.Dispatcher.UIThread.Post(async () =>
                    {
                        await viewModel.ConnectCommand.ExecuteAsync(null);
                        if (Program.DemoMode)
                        {
                            await viewModel.RunDemoAsync();
                        }
                    });
                }
            }

            base.OnFrameworkInitializationCompleted();
        }

        private void DisableAvaloniaDataAnnotationValidation()
        {
            // Get an array of plugins to remove
            var dataValidationPluginsToRemove =
                BindingPlugins.DataValidators.OfType<DataAnnotationsValidationPlugin>().ToArray();

            // remove each entry found
            foreach (var plugin in dataValidationPluginsToRemove)
            {
                BindingPlugins.DataValidators.Remove(plugin);
            }
        }
    }
}