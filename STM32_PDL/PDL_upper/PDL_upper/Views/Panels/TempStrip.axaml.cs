using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Controls;
using PDL_upper.ViewModels;

namespace PDL_upper.Views.Panels
{
    public partial class TempStrip : UserControl
    {
        public TempStrip()
        {
            InitializeComponent();
        }

        // 点迷你折线 = 弹出温度窗口
        private void OnChartTapped(object? sender, TappedEventArgs e)
        {
            if (DataContext is MainWindowViewModel vm)
                vm.OpenTempWindowCommand.Execute(null);
        }
    }
}
