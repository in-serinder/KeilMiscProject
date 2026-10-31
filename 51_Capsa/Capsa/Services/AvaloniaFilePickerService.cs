using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;

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
                Title = "Import payload",
                AllowMultiple = false,
                FileTypeFilter = new[]
                {
                    new FilePickerFileType("Binary / text files")
                    {
                        Patterns = new[] { "*.bin", "*.hex", "*.dat", "*.txt", "*.eep" },
                    },
                    new FilePickerFileType("All files") { Patterns = new[] { "*.*" } },
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

        public async Task SaveBinaryFileAsync(string suggestedName, byte[] data)
        {
            var file = await _topLevel.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
            {
                Title = "Export as .bin",
                SuggestedFileName = suggestedName,
                DefaultExtension = "bin",
                ShowOverwritePrompt = true,
                FileTypeChoices = new[]
                {
                    new FilePickerFileType("Binary file") { Patterns = new[] { "*.bin" } },
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
