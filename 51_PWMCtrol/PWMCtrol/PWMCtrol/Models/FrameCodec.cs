using System;
using System.Text;

namespace PWMCtrol.Models;

/// <summary>
/// 下行帧解析：从字节流里提取 <c>[ … ]</c>。
/// 帧外字节全部丢弃（含噪声、半帧），超长帧整帧丢弃。
/// </summary>
public sealed class FrameCodec
{
    private const int MaxLength = 128;
    private readonly StringBuilder _buffer = new(MaxLength);
    private bool _active;

    public int DiscardedFrames { get; private set; }

    public void Reset()
    {
        _buffer.Clear();
        _active = false;
    }

    public void Feed(byte[] data, int count, Action<string> onFrame)
    {
        for (var i = 0; i < count; i++)
        {
            var b = data[i];
            if (b == (byte)'[')
            {
                _active = true;
                _buffer.Clear();
            }
            else if (b == (byte)']')
            {
                if (_active)
                {
                    _active = false;
                    var body = _buffer.ToString();
                    _buffer.Clear();
                    if (body.Length > 0)
                    {
                        onFrame(body);
                    }
                }
            }
            else if (_active)
            {
                if (_buffer.Length >= MaxLength)
                {
                    _active = false;
                    _buffer.Clear();
                    DiscardedFrames++;
                }
                else
                {
                    _buffer.Append((char)b);
                }
            }
        }
    }
}
