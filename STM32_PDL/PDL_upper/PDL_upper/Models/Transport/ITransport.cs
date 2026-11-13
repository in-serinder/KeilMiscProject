using System;

namespace PDL_upper.Models.Transport
{
    /// <summary>
    /// 传输抽象：换模拟器/真机/未来 WinUSB 时只换实现，业务零改动。
    /// Read 返回一次到达的任意字节数（可能是流的一部分），由上层解码器拼帧。
    /// </summary>
    public interface ITransport : IDisposable
    {
        bool IsOpen { get; }
        string Describe { get; }
        void Open();
        void Close();
        int Read(byte[] buffer, int timeoutMs);
        void Write(byte[] data);
    }
}
