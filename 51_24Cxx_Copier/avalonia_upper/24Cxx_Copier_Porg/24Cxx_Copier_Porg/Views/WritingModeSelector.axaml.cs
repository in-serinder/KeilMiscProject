using _24Cxx_Copier_Porg.ViewModels;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;

namespace _24Cxx_Copier_Porg.Views;

/// <summary>
/// Writing-mode selector. The "Browse…" button is handled here because file
/// pickers require the top-level window, which a view-model must not own.
/// </summary>
public partial class WritingModeSelector : UserControl
{
    public WritingModeSelector()
    {
        InitializeComponent();
    }

    private async void OnBrowseClick(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not OpearationPanelViewModel vm)
        {
            return;
        }

        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel is null)
        {
            return;
        }

        var files = await topLevel.StorageProvider.OpenFilePickerAsync(
            new FilePickerOpenOptions
            {
                Title = "Select a file to write",
                AllowMultiple = false,
                FileTypeFilter = new[]
                {
                    new FilePickerFileType("All files") { Patterns = new[] { "*.*" } },
                },
            });

        if (files.Count == 0)
        {
            return;
        }

        var path = files[0].TryGetLocalPath();
        if (!string.IsNullOrWhiteSpace(path))
        {
            vm.PreviewFile(path);
        }
    }
}
