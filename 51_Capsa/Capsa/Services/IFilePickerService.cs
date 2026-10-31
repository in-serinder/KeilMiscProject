using System.Threading.Tasks;

namespace Capsa.Services
{
    /// <summary>导入 / 导出文件（由 View 层注入具体的 Avalonia StorageProvider 实现）。</summary>
    public interface IFilePickerService
    {
        /// <summary>选择一个二进制文件，返回文件名与内容；用户取消时返回 null。</summary>
        Task<(string Name, byte[] Data)?> PickBinaryFileAsync();

        /// <summary>保存二进制文件。</summary>
        Task SaveBinaryFileAsync(string suggestedName, byte[] data);
    }
}
