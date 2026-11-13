using Avalonia.Controls;
using Avalonia.Interactivity;
using PDL_upper.ViewModels;

namespace PDL_upper.Views.Dialogs
{
    public partial class TriggerDialog : Window
    {
        public TriggerDialog()
        {
            InitializeComponent();
        }

        private void OnClose(object? sender, RoutedEventArgs e) => Close();
    }
}
