namespace Capsa.Models
{
    /// <summary>传输进度快照。</summary>
    /// <param name="Percent">整卡（8 片合计）进度 0-100。</param>
    /// <param name="Message">状态文案。</param>
    /// <param name="ChipAddress">当前正在读写的芯片地址，0 表示不确定（如 Mock）。</param>
    /// <param name="ChipBytes">该片已经写入 / 读出的字节数，用于卡片进度条。</param>
    public readonly record struct TransferProgress(
        double Percent,
        string Message,
        int ChipAddress = 0,
        int ChipBytes = 0);

    /// <summary>一次写入 / 读取 / 校验的操作结果。</summary>
    public sealed record TransferResult(bool Success, string Message)
    {
        public static TransferResult Ok(string message) => new(true, message);

        public static TransferResult Fail(string message) => new(false, message);
    }
}
