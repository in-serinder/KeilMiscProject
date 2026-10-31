using System.Threading.Tasks;

namespace Capsa.Services
{
    /// <summary>导入 / 导出文件（由 View 层注入具体的 Avalonia StorageProvider 实现）。</summary>
    public interface IFilePickerService
    {
        /// <summary>
        /// 选择任意格式的文件，返回文件名与内容；用户取消时返回 null。
        /// 文件名里的扩展名会被写入存储卡数据末尾，供导出时还原。
        /// </summary>
        Task<(string Name, byte[] Data)?> PickBinaryFileAsync();

        /// <summary>
        /// 保存文件。<paramref name="extension"/> 为还原出的原扩展名（不含点），
        /// 为空时按 <c>.bin</c> 保存。
        /// </summary>
        Task SaveBinaryFileAsync(string suggestedName, byte[] data, string? extension = null);
    }
}
