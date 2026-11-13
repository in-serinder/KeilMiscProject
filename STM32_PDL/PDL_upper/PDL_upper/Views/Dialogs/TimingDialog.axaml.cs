using Avalonia.Controls;
using Avalonia.Interactivity;

namespace PDL_upper.Views.Dialogs
{
    public partial class TimingDialog : Window
    {
        public TimingDialog()
        {
            InitializeComponent();
        }

        private void OnClose(object? sender, RoutedEventArgs e) => Close();
    }
}
