using System;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Capsa.Diagnostics;

namespace Capsa.Views.Dialogs
{
    /// <summary>
    /// 错误详情窗口：完整异常（类型 / 消息 / 堆栈）+ 最近协议交互，
    /// 全部放在可选中、可复制的只读文本框里。
    /// 动机：初始化 / 读写 / 校验失败时界面上只有一行截断提示，
    /// 用户没法把真正的原因带走 —— 这个窗口就是为"复制报错内容"存在的。
    /// </summary>
    public partial class ErrorDetailsWindow : Window
    {
        private readonly string _detail = string.Empty;
        private readonly string _fullLog = string.Empty;

        public ErrorDetailsWindow()
        {
            InitializeComponent();
        }

        public ErrorDetailsWindow(string title, string summary, string detail)
            : this()
        {
            Title = title;
            TitleText.Text = title;
            SummaryText.Text = summary;
            _detail = detail;
            _fullLog = ErrorLog.BuildReport();
            DetailBox.Text = detail;
        }

        /// <summary>打开后展示整个错误日志（而不是单条），用于日志入口。</summary>
        public static ErrorDetailsWindow ForFullLog(string title = "Error log")
            => new(
                title,
                $"{ErrorLog.Entries.Count} entr{(ErrorLog.Entries.Count == 1 ? "y" : "ies")} · details of every entry below",
                ErrorLog.BuildReport());

        private async void OnCopyClick(object? sender, RoutedEventArgs e)
            => await CopyToClipboardAsync((Button)sender!, _detail, "Copied");

        private async void OnCopyAllClick(object? sender, RoutedEventArgs e)
            => await CopyToClipboardAsync((Button)sender!, _fullLog, "Copied log");

        private async Task CopyToClipboardAsync(Button source, string text, string doneText)
        {
            var previous = source.Content;
            try
            {
                var clipboard = TopLevel.GetTopLevel(this)?.Clipboard;
                if (clipboard is null)
                {
                    source.Content = "Clipboard N/A";
                }
                else
                {
                    await clipboard.SetTextAsync(text);
                    source.Content = doneText;
                }
            }
            catch (Exception)
            {
                source.Content = "Copy failed";
            }
            finally
            {
                // 让"已复制"的反馈停留一下再还原（无 await 风险的界面微交互）
                _ = RestoreLabelAsync(source, previous);
            }
        }

        private static async Task RestoreLabelAsync(Button source, object? previous)
        {
            await Task.Delay(900);
            source.Content = previous;
        }

        private void OnCloseClick(object? sender, RoutedEventArgs e) => Close();
    }
}
