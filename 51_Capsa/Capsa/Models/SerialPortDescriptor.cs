using CommunityToolkit.Mvvm.ComponentModel;

namespace Capsa.Models
{
    /// <summary>
    /// 可用的串口条目（由 <see cref="Services.ISerialPortService"/> 枚举得到）。
    /// <see cref="IsCapsa"/> 为 true 表示探测到 Capsa 下位机，列表里会打上星号。
    /// </summary>
    public sealed partial class SerialPortDescriptor : ObservableObject
    {
        public SerialPortDescriptor(string portName, string description)
        {
            PortName = portName;
            Description = description;
        }

        /// <summary>端口名，例如 "COM3"。</summary>
        public string PortName { get; }

        /// <summary>端口描述，例如 "USB-SERIAL CH340"。</summary>
        public string Description { get; }

        /// <summary>是否探测到 Capsa 设备（TYPE-CAPASA → [YES]）。</summary>
        [ObservableProperty]
        private bool _isCapsa;

        /// <summary>下拉框显示文本，已探测到时前置星号。</summary>
        public string Display => IsCapsa
            ? $"★ {PortName}  ·  {Description}"
            : $"{PortName}  ·  {Description}";

        partial void OnIsCapsaChanged(bool value) => OnPropertyChanged(nameof(Display));

        public override string ToString() => Display;
    }
}
