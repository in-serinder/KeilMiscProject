using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ArchivumU.Models
{
    /// <summary>
    /// 串口对象帮助器模型 - 用于管理多个串口的交互
    /// </summary>
    public class SerialObjectHelperModel : IDisposable
    {
        /// <summary>
        /// 存储所有串口对象的字典，Key为串口名称
        /// </summary>
        private Dictionary<string, SerialPort> _serialPorts = new Dictionary<string, SerialPort>();

        /// <summary>
        /// 串口数据接收事件
        /// </summary>
        public event EventHandler<SerialDataReceivedEventArgs> DataReceived;

        /// <summary>
        /// 串口状态改变事件
        /// </summary>
        public event EventHandler<SerialStatusChangedEventArgs> StatusChanged;

        /// <summary>
        /// 获取所有已创建的串口名称
        /// </summary>
        public List<string> SerialPortNames => _serialPorts.Keys.ToList();

        /// <summary>
        /// 获取系统可用的串口列表
        /// </summary>
        /// <returns>可用串口名称数组</returns>
        public string[] GetAvailablePorts()
        {
            return SerialPort.GetPortNames();
        }

        public List<string> GetAllPortNames()
        {
            return SerialPort.GetPortNames().ToList();
        }

        public List<string> GetFreePortNames()
        {
            var allPorts = SerialPort.GetPortNames().ToList();
            var usedPorts = _serialPorts.Keys.ToList();
            return allPorts.Except(usedPorts).ToList();
        }

        /// <summary>
        /// 创建串口对象（不立即打开）
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <param name="baudRate">波特率</param>
        /// <param name="dataBits">数据位</param>
        /// <param name="parity">校验位</param>
        /// <param name="stopBits">停止位</param>
        /// <returns>是否创建成功</returns>
        public bool CreateSerialPort(string portName, int baudRate = 9600, int dataBits = 8,
                                     Parity parity = Parity.None, StopBits stopBits = StopBits.One)
        {
            try
            {
                if (_serialPorts.ContainsKey(portName))
                {
                    throw new InvalidOperationException($"串口 {portName} 已存在");
                }

                var serialPort = new SerialPort(portName, baudRate, parity, dataBits, stopBits);
                serialPort.DataReceived += SerialPort_DataReceived;
                serialPort.ErrorReceived += SerialPort_ErrorReceived;

                _serialPorts.Add(portName, serialPort);
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Created, null));
                return true;
            }
            catch (Exception ex)
            {
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Error, ex.Message));
                return false;
            }
        }

        /// <summary>
        /// 打开串口连接
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <returns>是否连接成功</returns>
        public bool OpenPort(string portName)
        {
            try
            {
                if (!_serialPorts.TryGetValue(portName, out var serialPort))
                {
                    throw new InvalidOperationException($"串口 {portName} 不存在，请先创建");
                }

                if (serialPort.IsOpen)
                {
                    throw new InvalidOperationException($"串口 {portName} 已处于打开状态");
                }

                serialPort.Open();
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Opened, null));
                return true;
            }
            catch (Exception ex)
            {
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Error, ex.Message));
                return false;
            }
        }

        /// <summary>
        /// 关闭串口连接
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <returns>是否断开成功</returns>
        public bool ClosePort(string portName)
        {
            try
            {
                if (!_serialPorts.TryGetValue(portName, out var serialPort))
                {
                    throw new InvalidOperationException($"串口 {portName} 不存在");
                }

                if (!serialPort.IsOpen)
                {
                    throw new InvalidOperationException($"串口 {portName} 已处于关闭状态");
                }

                serialPort.Close();
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Closed, null));
                return true;
            }
            catch (Exception ex)
            {
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Error, ex.Message));
                return false;
            }
        }

        /// <summary>
        /// 打开所有已创建的串口
        /// </summary>
        public void OpenAllPorts()
        {
            foreach (var portName in _serialPorts.Keys)
            {
                OpenPort(portName);
            }
        }

        /// <summary>
        /// 关闭所有已打开的串口
        /// </summary>
        public void CloseAllPorts()
        {
            foreach (var portName in _serialPorts.Keys)
            {
                ClosePort(portName);
            }
        }

        /// <summary>
        /// 删除串口对象
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <returns>是否删除成功</returns>
        public bool RemoveSerialPort(string portName)
        {
            try
            {
                if (!_serialPorts.TryGetValue(portName, out var serialPort))
                {
                    throw new InvalidOperationException($"串口 {portName} 不存在");
                }

                // 如果串口处于打开状态，先关闭
                if (serialPort.IsOpen)
                {
                    serialPort.Close();
                }

                // 移除事件订阅
                serialPort.DataReceived -= SerialPort_DataReceived;
                serialPort.ErrorReceived -= SerialPort_ErrorReceived;

                // 释放资源
                serialPort.Dispose();
                _serialPorts.Remove(portName);

                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Removed, null));
                return true;
            }
            catch (Exception ex)
            {
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Error, ex.Message));
                return false;
            }
        }

        /// <summary>
        /// 发送数据（字节数组）
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <param name="data">要发送的字节数组</param>
        /// <returns>是否发送成功</returns>
        public bool SendData(string portName, byte[] data)
        {
            try
            {
                if (!_serialPorts.TryGetValue(portName, out var serialPort))
                {
                    throw new InvalidOperationException($"串口 {portName} 不存在");
                }

                if (!serialPort.IsOpen)
                {
                    throw new InvalidOperationException($"串口 {portName} 未打开");
                }

                serialPort.Write(data, 0, data.Length);
                return true;
            }
            catch (Exception ex)
            {
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Error, ex.Message));
                return false;
            }
        }

        /// <summary>
        /// 发送数据（字符串）
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <param name="data">要发送的字符串</param>
        /// <param name="encoding">编码方式</param>
        /// <returns>是否发送成功</returns>
        public bool SendData(string portName, string data, Encoding encoding = null)
        {
            try
            {
                if (encoding == null)
                {
                    encoding = Encoding.Default;
                }

                byte[] byteData = encoding.GetBytes(data);
                return SendData(portName, byteData);
            }
            catch (Exception ex)
            {
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Error, ex.Message));
                return false;
            }
        }

        /// <summary>
        /// 发送数据（十六进制字符串）
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <param name="hexString">十六进制字符串（如 "A1 B2 C3"）</param>
        /// <returns>是否发送成功</returns>
        public bool SendHexData(string portName, string hexString)
        {
            try
            {
                // 移除所有空格和非十六进制字符
                string cleanedHex = new string(hexString.Where(c => !char.IsWhiteSpace(c)).ToArray());

                if (cleanedHex.Length % 2 != 0)
                {
                    throw new ArgumentException("十六进制字符串长度必须为偶数");
                }

                byte[] data = new byte[cleanedHex.Length / 2];
                for (int i = 0; i < data.Length; i++)
                {
                    data[i] = Convert.ToByte(cleanedHex.Substring(i * 2, 2), 16);
                }

                return SendData(portName, data);
            }
            catch (Exception ex)
            {
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Error, ex.Message));
                return false;
            }
        }

        /// <summary>
        /// 读取指定长度的数据
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <param name="length">要读取的字节数</param>
        /// <returns>读取到的字节数组</returns>
        public byte[] ReadData(string portName, int length)
        {
            try
            {
                if (!_serialPorts.TryGetValue(portName, out var serialPort))
                {
                    throw new InvalidOperationException($"串口 {portName} 不存在");
                }

                if (!serialPort.IsOpen)
                {
                    throw new InvalidOperationException($"串口 {portName} 未打开");
                }

                byte[] data = new byte[length];
                int bytesRead = serialPort.Read(data, 0, length);

                if (bytesRead < length)
                {
                    Array.Resize(ref data, bytesRead);
                }

                return data;
            }
            catch (Exception ex)
            {
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Error, ex.Message));
                return null;
            }
        }

        /// <summary>
        /// 读取所有可用数据
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <returns>读取到的字节数组</returns>
        public byte[] ReadAllData(string portName)
        {
            try
            {
                if (!_serialPorts.TryGetValue(portName, out var serialPort))
                {
                    throw new InvalidOperationException($"串口 {portName} 不存在");
                }

                if (!serialPort.IsOpen)
                {
                    throw new InvalidOperationException($"串口 {portName} 未打开");
                }

                int bytesToRead = serialPort.BytesToRead;
                if (bytesToRead == 0)
                {
                    return new byte[0];
                }

                byte[] data = new byte[bytesToRead];
                serialPort.Read(data, 0, bytesToRead);
                return data;
            }
            catch (Exception ex)
            {
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Error, ex.Message));
                return null;
            }
        }

        /// <summary>
        /// 读取数据并转换为字符串
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <param name="encoding">编码方式</param>
        /// <returns>读取到的字符串</returns>
        public string ReadDataAsString(string portName, Encoding encoding = null)
        {
            try
            {
                byte[] data = ReadAllData(portName);
                if (data == null || data.Length == 0)
                {
                    return string.Empty;
                }

                if (encoding == null)
                {
                    encoding = Encoding.Default;
                }

                return encoding.GetString(data);
            }
            catch (Exception ex)
            {
                OnStatusChanged(new SerialStatusChangedEventArgs(portName, PortStatus.Error, ex.Message));
                return string.Empty;
            }
        }

        /// <summary>
        /// 检查串口是否存在
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <returns>是否存在</returns>
        public bool PortExists(string portName)
        {
            return _serialPorts.ContainsKey(portName);
        }

        /// <summary>
        /// 检查串口是否打开
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <returns>是否打开</returns>
        public bool IsPortOpen(string portName)
        {
            if (_serialPorts.TryGetValue(portName, out var serialPort))
            {
                return serialPort.IsOpen;
            }
            return false;
        }

        /// <summary>
        /// 获取串口状态
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <returns>串口状态</returns>
        public PortStatus GetPortStatus(string portName)
        {
            if (!_serialPorts.TryGetValue(portName, out var serialPort))
            {
                return PortStatus.NotExists;
            }

            return serialPort.IsOpen ? PortStatus.Opened : PortStatus.Created;
        }

        /// <summary>
        /// 获取串口配置信息
        /// </summary>
        /// <param name="portName">串口名称</param>
        /// <returns>串口配置对象</returns>
        public SerialPortConfig GetPortConfig(string portName)
        {
            if (_serialPorts.TryGetValue(portName, out var serialPort))
            {
                return new SerialPortConfig
                {
                    PortName = serialPort.PortName,
                    BaudRate = serialPort.BaudRate,
                    DataBits = serialPort.DataBits,
                    Parity = serialPort.Parity,
                    StopBits = serialPort.StopBits,
                    IsOpen = serialPort.IsOpen
                };
            }
            return null;
        }

        /// <summary>
        /// 数据接收事件处理
        /// </summary>
        private void SerialPort_DataReceived(object sender, System.IO.Ports.SerialDataReceivedEventArgs e)
        {
            var serialPort = (SerialPort)sender;
            DataReceived?.Invoke(this, new SerialDataReceivedEventArgs(serialPort.PortName, e.EventType));
        }

        /// <summary>
        /// 错误接收事件处理
        /// </summary>
        private void SerialPort_ErrorReceived(object sender, SerialErrorReceivedEventArgs e)
        {
            var serialPort = (SerialPort)sender;
            OnStatusChanged(new SerialStatusChangedEventArgs(serialPort.PortName, PortStatus.Error,
                $"串口错误: {e.EventType}"));
        }

        /// <summary>
        /// 触发状态改变事件
        /// </summary>
        protected virtual void OnStatusChanged(SerialStatusChangedEventArgs e)
        {
            StatusChanged?.Invoke(this, e);
        }

        /// <summary>
        /// 释放资源
        /// </summary>
        public void Dispose()
        {
            CloseAllPorts();

            foreach (var serialPort in _serialPorts.Values)
            {
                serialPort.DataReceived -= SerialPort_DataReceived;
                serialPort.ErrorReceived -= SerialPort_ErrorReceived;
                serialPort.Dispose();
            }

            _serialPorts.Clear();
        }

        public async Task<(bool Success, string Result)> QuickSendCommand(string portName, string command, int timeoutMs = 5000)
        {
            // 检查串口是否已被占用
            if (_serialPorts.ContainsKey(portName))
            {
                return (false, "串口已被占用");
            }

            SerialPort tempPort = null;
            string response = string.Empty;
            bool received = false;

            try
            {
                // 创建临时串口对象
                tempPort = new SerialPort(portName, 9600, Parity.None, 8, StopBits.One);
                tempPort.ReadTimeout = timeoutMs;
                tempPort.WriteTimeout = timeoutMs;

                // 打开串口
                tempPort.Open();

                // 先清空发送前可能残留的旧数据，避免读到上一条命令的响应（如 ECHO）
                try
                {
                    tempPort.DiscardInBuffer();
                }
                catch { }

                // 发送指令
                tempPort.WriteLine(command);

                // 等待接收响应：持续累积直到出现明确的结束标记或短暂静默
                var startTime = DateTime.Now;
                int lastLength = 0;
                int idleCount = 0;
                while ((DateTime.Now - startTime).TotalMilliseconds < timeoutMs)
                {
                    string chunk = string.Empty;
                    try
                    {
                        chunk = tempPort.ReadExisting();
                    }
                    catch { }

                    if (!string.IsNullOrEmpty(chunk))
                    {
                        response += chunk;
                        idleCount = 0;
                    }
                    else if (response.Length > 0)
                    {
                        // 已收到内容后，若连续多次无新数据（约 250ms 静默）则视为接收完成
                        idleCount++;
                        if (idleCount >= 5)
                        {
                            break;
                        }
                    }

                    // 收到明确的结果标记后，再多等一小会儿把尾部数据收完
                    if (response.Length > 0 && IsTerminal(response))
                    {
                        // 继续循环，由上面的静默检测收尾
                        received = true;
                    }

                    lastLength = response.Length;
                    await Task.Delay(50);
                }

                return (received || response.Length > 0, response.Trim());
            }
            catch (Exception ex)
            {
                return (false, $"错误: {ex.Message}");
            }
            finally
            {
                // 确保关闭并释放资源
                tempPort?.Close();
                tempPort?.Dispose();
            }
        }

        /// <summary>
        /// 判断已接收内容是否包含「结果性」结束标记，
        /// 用于判定一条命令的响应是否已经收完。
        /// </summary>
        private static bool IsTerminal(string response)
        {
            if (string.IsNullOrEmpty(response))
            {
                return false;
            }

            string s = response;
            return s.Contains("RESULT+")   // RESULT+0 / RESULT+1
                || s.Contains("DATA+")     // DATA+...
                || s.Contains("ERR+")      // ERR+<code>
                || s.Contains("AUTH+")     // AUTH+...
                || s.Contains("INFO+")     // INFO+...
                || s.Contains("STATUS+")   // STATUS+...
                || s.Contains("EOF");      // \EOF 结束标记
        }
    }

    /// <summary>
    /// 串口状态枚举
    /// </summary>
    public enum PortStatus
    {
        /// <summary>
        /// 不存在
        /// </summary>
        NotExists,
        /// <summary>
        /// 已创建
        /// </summary>
        Created,
        /// <summary>
        /// 已打开
        /// </summary>
        Opened,
        /// <summary>
        /// 已关闭
        /// </summary>
        Closed,
        /// <summary>
        /// 已移除
        /// </summary>
        Removed,
        /// <summary>
        /// 错误
        /// </summary>
        Error
    }

    /// <summary>
    /// 串口配置信息类
    /// </summary>
    public class SerialPortConfig
    {
        public string PortName { get; set; }
        public int BaudRate { get; set; }
        public int DataBits { get; set; }
        public Parity Parity { get; set; }
        public StopBits StopBits { get; set; }
        public bool IsOpen { get; set; }
    }

    /// <summary>
    /// 串口数据接收事件参数
    /// </summary>
    public class SerialDataReceivedEventArgs : EventArgs
    {
        public string PortName { get; }
        public SerialData DataEventType { get; }

        public SerialDataReceivedEventArgs(string portName, SerialData dataEventType)
        {
            PortName = portName;
            DataEventType = dataEventType;
        }
    }

    /// <summary>
    /// 串口状态改变事件参数
    /// </summary>
    public class SerialStatusChangedEventArgs : EventArgs
    {
        public string PortName { get; }
        public PortStatus Status { get; }
        public string Message { get; }

        public SerialStatusChangedEventArgs(string portName, PortStatus status, string message)
        {
            PortName = portName;
            Status = status;
            Message = message;
        }
    }
}