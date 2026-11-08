using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Text;

namespace Capsa.Diagnostics
{
    /// <summary>一条错误记录：摘要给界面，详情给"可复制的错误窗口"。</summary>
    public sealed record ErrorEntry(
        DateTimeOffset At,
        string Source,
        string Summary,
        string Detail)
    {
        public string TimeText => At.ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss.fff");
    }

    /// <summary>
    /// 全局错误日志（进程内环形缓冲，最近 200 条）。
    ///
    /// 设计动机：初始化 / 写入 / 校验失败时界面上只有一行截断的提示，
    /// 真正的原因（异常类型、堆栈、最后几条协议交互）全都看不见。
    /// 任何 catch / 全局兜底都应该调用 <see cref="Add"/>；
    /// 错误详情窗口从这里取文本，用户可以整段复制出去。
    /// </summary>
    public static class ErrorLog
    {
        private const int Capacity = 200;

        /// <summary>按时间顺序存放的全部记录（绑定用，勿手动 Clear）。</summary>
        public static ObservableCollection<ErrorEntry> Entries { get; } = new();

        /// <summary>新记录到达（供状态栏 / 角标提示）。</summary>
        public static event Action<ErrorEntry>? Added;

        /// <summary>
        /// 记录一条错误。<paramref name="context"/> 用于补充现场（端口名、卡片型号、
        /// 正在执行的步骤等），会原样进入详情文本。
        /// </summary>
        public static void Add(string source, string summary, Exception? ex = null, string? context = null)
        {
            var entry = new ErrorEntry(
                DateTimeOffset.Now,
                source,
                summary,
                BuildDetail(source, summary, ex, context));

            Entries.Add(entry);
            while (Entries.Count > Capacity)
            {
                Entries.RemoveAt(0);
            }

            Added?.Invoke(entry);
        }

        /// <summary>最近一条记录（没有则 null），弹窗默认展示它。</summary>
        public static ErrorEntry? Latest => Entries.Count > 0 ? Entries[^1] : null;

        /// <summary>把整个日志导出为可直接复制 / 存档的文本。</summary>
        public static string BuildReport()
        {
            var sb = new StringBuilder();
            sb.AppendLine($"Capsa error log · {Entries.Count} entr{(Entries.Count == 1 ? "y" : "ies")} · exported {DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss}");
            sb.AppendLine(new string('=', 72));

            foreach (var e in Entries)
            {
                sb.AppendLine($"[{e.TimeText}] {e.Source}: {e.Summary}");
            }

            sb.AppendLine();
            sb.AppendLine(new string('=', 72));
            sb.AppendLine("Details (latest last):");

            foreach (var e in Entries)
            {
                sb.AppendLine(new string('-', 72));
                sb.AppendLine($"[{e.TimeText}] {e.Source}");
                sb.AppendLine(e.Detail);
            }

            return sb.ToString();
        }

        private static string BuildDetail(string source, string summary, Exception? ex, string? context)
        {
            var sb = new StringBuilder();
            sb.AppendLine($"Source : {source}");
            sb.AppendLine($"Time   : {DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss.fff} (local)");
            sb.AppendLine($"Summary: {summary}");

            if (!string.IsNullOrWhiteSpace(context))
            {
                sb.AppendLine($"Context: {context}");
            }

            if (ex is not null)
            {
                sb.AppendLine();
                sb.AppendLine("--- Exception ---");
                sb.AppendLine(ex.ToString());   // 类型 + 消息 + 堆栈 + InnerException 全链
            }

            // 协议层最近交互：SET/UG/DI 被拒、超时这类问题，一眼能看出卡回了什么
            try
            {
                var trace = Services.Protocol.CapsaProtocol.RecentTrace();
                if (trace.Count > 0)
                {
                    sb.AppendLine();
                    sb.AppendLine("--- Last protocol exchanges (latest last) ---");
                    foreach (var t in trace)
                    {
                        sb.AppendLine($"[{t.At:HH:mm:ss.fff}] {t.ElapsedMs,5} ms  {t.Command}  ->  {t.Response}");
                    }
                }
            }
            catch
            {
                /* 日志收集绝不能再抛 */
            }

            return sb.ToString();
        }
    }
}
