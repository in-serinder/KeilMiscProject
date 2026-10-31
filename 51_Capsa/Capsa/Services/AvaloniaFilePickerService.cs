using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using Capsa.Models;

namespace Capsa.Services
{
    /// <summary>基于 Avalonia StorageProvider 的文件选择实现。</summary>
    public sealed class AvaloniaFilePickerService : IFilePickerService
    {
        private readonly TopLevel _topLevel;

        public AvaloniaFilePickerService(TopLevel topLevel)
        {
            _topLevel = topLevel;
        }

        public async Task<(string Name, byte[] Data)?> PickBinaryFileAsync()
        {
            var files = await _topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Import a file (any format)",
                AllowMultiple = false,
                FileTypeFilter = new[]
                {
                    // 任意格式都可以导入，扩展名会随数据写进存储卡
                    new FilePickerFileType("All files") { Patterns = new[] { "*.*" } },
                    new FilePickerFileType("EEPROM images")
                    {
                        Patterns = new[] { "*.bin", "*.hex", "*.dat", "*.eep" },
                    },
                },
            });

            var file = files.FirstOrDefault();
            if (file is null)
            {
                return null;
            }

            await using var stream = await file.OpenReadAsync();
            using var memory = new MemoryStream();
            await stream.CopyToAsync(memory);

            return (file.Name, memory.ToArray());
        }

        public async Task SaveBinaryFileAsync(string suggestedName, byte[] data, string? extension = null)
        {
            var ext = FilePayloadCodec.NormalizeExtension(extension);
            if (ext.Length == 0)
            {
                ext = "bin";
            }

            var file = await _topLevel.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
            {
                Title = $"Export as .{ext}",
                SuggestedFileName = suggestedName,
                DefaultExtension = ext,
                ShowOverwritePrompt = true,
                FileTypeChoices = new[]
                {
                    new FilePickerFileType($"{ext.ToUpperInvariant()} file")
                    {
                        Patterns = new[] { $"*.{ext}" },
                    },
                    new FilePickerFileType("All files") { Patterns = new[] { "*.*" } },
                },
            });

            if (file is null)
            {
                return;
            }

            await using var stream = await file.OpenWriteAsync();
            stream.SetLength(0);
            await stream.WriteAsync(data.AsMemory());
            await stream.FlushAsync();
        }
    }
}
