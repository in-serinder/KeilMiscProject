using Avalonia.Controls;

namespace Capsa.Views
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            Opened += (_, _) =>
            {
                // 需要 TopLevel：进入窗口生命周期后再注入文件选择实现。
                Services.AppServices.Files = new Services.AvaloniaFilePickerService(this);
                Services.AppServices.InitializeDialog = new Services.AvaloniaInitializeCardDialogService(this);
                Services.AppServices.HealthCheckDialog = new Services.AvaloniaChipHealthDialogService(this);
                Services.AppServices.Messages = new Services.AvaloniaMessageDialogService(this);
            };
        }
    }
}
