using System;
using System.Diagnostics;
using System.Threading.Tasks;
using Capsa.Services;
using Capsa.ViewModels.Panels;
using Capsa.ViewModels.Shell;
using CommunityToolkit.Mvvm.Input;

namespace Capsa.ViewModels
{
    /// <summary>主窗口：负责组装各面板并串联“连接 → 初始化 → 刷新信息”的主流程。</summary>
    public sealed partial class MainWindowViewModel : ViewModelBase
    {
        private readonly IDeviceService _device;

        public MainWindowViewModel()
        {
            _device = AppServices.Device;

            TopBar = new TopBarViewModel(AppServices.Serial);
            DeviceInfo = new DeviceInfoViewModel();
            Board = new ChipBoardViewModel();

            Write = new WritePanelViewModel(
                DeviceInfo,
                Board,
                _device,
                AppServices.Transfer,
                () => AppServices.Files,
                RefreshDeviceAsync);

            Read = new ReadPanelViewModel(
                DeviceInfo,
                AppServices.Transfer,
                () => AppServices.Files);

            TopBar.ConnectedChanged += OnConnectionChanged;
            _device.DeviceChanged += (_, info) => DeviceInfo.Apply(info);

            _ = TopBar.RefreshPortsCommand.ExecuteAsync(null);
        }

        public TopBarViewModel TopBar { get; }

        public DeviceInfoViewModel DeviceInfo { get; }

        public ChipBoardViewModel Board { get; }

        public WritePanelViewModel Write { get; }

        public ReadPanelViewModel Read { get; }

        public string ProjectUrl => "https://github.com/in-serinder/KeilMiscProject/blob/master/51_Capsa";

        /// <summary>与 Capsa.csproj 中的 &lt;Version&gt; 保持一致。</summary>
        private const string ApplicationVersion = "0.1.0";

        public string VersionText => $"v{ApplicationVersion}";

        [RelayCommand]
        private void OpenProject()
        {
            try
            {
                Process.Start(new ProcessStartInfo(ProjectUrl) { UseShellExecute = true });
            }
            catch
            {
                // 忽略浏览器打开失败。
            }
        }

        private async void OnConnectionChanged(object? sender, bool connected)
        {
            if (!connected)
            {
                DeviceInfo.Reset();
                Board.Reset();
                return;
            }

            await RefreshDeviceAsync().ConfigureAwait(true);

            // GET 没有返回信息 = 卡片未初始化：自动拉起初始化窗口
            if (DeviceInfo.IsInitialized)
            {
                return;
            }

            var port = AppServices.Serial.ConnectedPortName;
            if (string.IsNullOrWhiteSpace(port))
            {
                return;
            }

            var result = await AppServices.InitializeDialog.ShowAsync(port).ConfigureAwait(true);
            if (result is not null)
            {
                await RefreshDeviceAsync().ConfigureAwait(true);
            }
        }

        /// <summary>重新拉取设备信息与 8 个单元状态（供各面板复用）。</summary>
        private async Task RefreshDeviceAsync()
        {
            var info = await _device.ReadDeviceInfoAsync().ConfigureAwait(true);
            DeviceInfo.Apply(info);

            if (info is null)
            {
                Board.Reset();
                return;
            }

            var slots = await _device.ReadSlotsAsync().ConfigureAwait(true);
            Board.Load(slots);
        }
    }
}
