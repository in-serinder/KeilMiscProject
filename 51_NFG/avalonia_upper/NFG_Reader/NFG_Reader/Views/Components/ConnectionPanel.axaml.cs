using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using NFG_Reader.ViewModels;
using NFG_Reader.Views;

namespace NFG_Reader.Views.Components
{
    public partial class ConnectionPanel : UserControl
    {
        public ConnectionPanel()
        {
            InitializeComponent();
        }

        private async void OnConnectClick(object? sender, RoutedEventArgs e)
        {
            if (DataContext is not MainWindowViewModel vm)
            {
                return;
            }

            var owner = TopLevel.GetTopLevel(this) as Window;
            var portName = vm.SelectedComPort?.PortName;

            var dialog = AuthenticateDialog.ForPort(portName);
            var confirmed = owner is null
                ? await ShowDialogFallbackAsync(dialog)
                : await dialog.ShowDialog<bool>(owner);

            if (!confirmed)
            {
                return;
            }

            // TODO: exchange/verify the password with the device here.
            vm.Password = dialog.Password;
            vm.IsConnected = true;
        }

        private static Task<bool> ShowDialogFallbackAsync(AuthenticateDialog dialog)
        {
            var tcs = new TaskCompletionSource<bool>();
            dialog.Closed += (_, _) => tcs.TrySetResult(dialog.Password.Length > 0);
            dialog.Show();
            return tcs.Task;
        }
    }
}
