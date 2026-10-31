using System;
using Avalonia.Threading;

namespace Capsa.Services
{
    /// <summary>
    /// 把服务层的事件回调切回 UI 线程。
    /// 传输 / 设备 / 串口服务都在后台线程（<c>Task.Run</c>）里跑，
    /// 若直接从后台线程触发事件，订阅方会在错误线程上创建控件或改动绑定，
    /// 触发 <c>InvalidOperationException: Call from invalid thread</c>。
    /// </summary>
    internal static class UiThread
    {
        /// <summary>已在 UI 线程则同步执行，否则投递到 UI 线程。</summary>
        public static void Post(Action action)
        {
            if (Dispatcher.UIThread.CheckAccess())
            {
                action();
            }
            else
            {
                Dispatcher.UIThread.Post(action);
            }
        }
    }
}
