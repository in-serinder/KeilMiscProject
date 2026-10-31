using Avalonia.Controls;
using Avalonia.Interactivity;

namespace Capsa.Views.Dialogs
{
    /// <summary>通用提示窗口（错误弹窗）。</summary>
    public partial class MessageWindow : Window
    {
        public MessageWindow()
        {
            InitializeComponent();
        }

        public MessageWindow(string title, string message)
            : this()
        {
            Title = title;
            TitleText.Text = title;
            MessageText.Text = message;
        }

        private void OnOkClick(object? sender, RoutedEventArgs e) => Close();
    }
}
