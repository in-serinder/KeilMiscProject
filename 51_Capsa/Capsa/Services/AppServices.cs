using System;
using Capsa.Services.Mock;
using Capsa.Services.Protocol;

namespace Capsa.Services
{
    /// <summary>
    /// 极简组合根（Service Locator）。
    /// 默认注入真实串口实现；设置环境变量 <c>CAPSA_MOCK=1</c> 时切换为内存 Mock（无硬件演示 / 截图用）。
    /// </summary>
    public static class AppServices
    {
        /// <summary>当前是否运行在 Mock 模式。</summary>
        public static bool UseMock { get; private set; }

        public static ISerialPortService Serial { get; set; } = new MockSerialPortService();

        public static IDeviceService Device { get; set; } = new MockDeviceService();

        public static ITransferService Transfer { get; set; } = new MockTransferService();

        /// <summary>存储卡初始化流程（写入初始化信息 → 校准 → 芯片测试）。</summary>
        public static ICardInitService CardInit { get; set; } = new MockCardInitService(() => Device);

        /// <summary>芯片健康检查（写入 0xFF / 0x00 并读回校验）。</summary>
        public static IChipTestService ChipTest { get; set; } = new MockChipTestService(() => Device);

        /// <summary>文件选择服务由 MainWindow 在创建后注入（需要 TopLevel）。</summary>
        public static IFilePickerService Files { get; set; } = new NullFilePickerService();

        /// <summary>初始化窗口服务由 MainWindow 在创建后注入（需要 Owner 窗口）。</summary>
        public static IInitializeCardDialogService InitializeDialog { get; set; } = new NullInitializeCardDialogService();

        /// <summary>芯片健康检查窗口服务，同样由 MainWindow 注入。</summary>
        public static IChipHealthDialogService HealthCheckDialog { get; set; } = new NullChipHealthDialogService();

        /// <summary>错误提示弹窗。</summary>
        public static IMessageDialogService Messages { get; set; } = new NullMessageDialogService();

        /// <summary>当前串口链路（真实服务注入，Mock 模式返回 null）。</summary>
        public static Func<CapsaProtocol?> Link { get; set; } = () => null;

        // ------------------------------------------------------------------ //

        /// <summary>根据环境变量选择真实 / Mock 实现。</summary>
        public static void ConfigureFromEnvironment()
        {
            var flag = Environment.GetEnvironmentVariable("CAPSA_MOCK");
            var mock = !string.IsNullOrEmpty(flag) && flag != "0";

            if (mock)
            {
                ConfigureMock();
            }
            else
            {
                ConfigureReal();
            }
        }

        public static void ConfigureMock()
        {
            UseMock = true;

            Serial = new MockSerialPortService();
            Device = new MockDeviceService();
            Transfer = new MockTransferService();
            CardInit = new MockCardInitService(() => Device);
            ChipTest = new MockChipTestService(() => Device);
            Link = () => null;

            Files = new NullFilePickerService();
            InitializeDialog = new NullInitializeCardDialogService();
            HealthCheckDialog = new NullChipHealthDialogService();
            Messages = new NullMessageDialogService();
        }

        public static void ConfigureReal()
        {
            UseMock = false;

            var serial = new CapsaSerialPortService();
            Serial = serial;
            Link = () => serial.Protocol;

            ChipTest = new CapsaChipTestService(Link);
            Device = new CapsaDeviceService(Link);
            Transfer = new CapsaTransferService(Link);
            CardInit = new CapsaCardInitService(Link, ChipTest);

            Files = new NullFilePickerService();
            InitializeDialog = new NullInitializeCardDialogService();
            HealthCheckDialog = new NullChipHealthDialogService();
            Messages = new NullMessageDialogService();
        }
    }

    /// <summary>空实现，避免设计时 / 单元测试时的空引用。</summary>
    public sealed class NullFilePickerService : IFilePickerService
    {
        public System.Threading.Tasks.Task<(string Name, byte[] Data)?> PickBinaryFileAsync()
            => System.Threading.Tasks.Task.FromResult<(string Name, byte[] Data)?>(null);

        public System.Threading.Tasks.Task SaveBinaryFileAsync(string suggestedName, byte[] data, string? extension = null)
            => System.Threading.Tasks.Task.CompletedTask;
    }
}
