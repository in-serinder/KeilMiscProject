using _24Cxx_Copier_Porg.ViewModels;
using Avalonia.Controls;

namespace _24Cxx_Copier_Porg.Views;

/// <summary>
/// Standalone window that displays a byte buffer decoded as text, with a footer
/// line showing its size, character count and encoding plus a "download as
/// .txt" action.
/// </summary>
public partial class TextViewerWindow : Window
{
    public TextViewerWindow()
    {
        InitializeComponent();
    }

    public TextViewerWindow(TextViewViewModel viewModel)
        : this()
    {
        DataContext = viewModel;
    }
}
