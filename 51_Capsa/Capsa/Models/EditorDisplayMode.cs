namespace Capsa.Models
{
    /// <summary>编辑器呈现模式。</summary>
    public enum EditorDisplayMode
    {
        /// <summary>十六进制编辑器。</summary>
        Hex = 0,

        /// <summary>纯文本编辑器。</summary>
        Text = 1,

        /// <summary>双显（左 HEX / 右文本）同步编辑。</summary>
        Dual = 2,
    }

    /// <summary>缓冲区变更来源，用于避免编辑器之间的同步回环。</summary>
    public enum EditorChangeSource
    {
        Initial = 0,
        Import = 1,
        Device = 2,
        Hex = 3,
        Text = 4,
        Encoding = 5,
    }
}
