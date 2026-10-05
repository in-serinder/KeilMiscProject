using _24Cxx_Copier_Porg.ViewModels;
using Avalonia.Controls;

namespace _24Cxx_Copier_Porg.Views;

/// <summary>
/// Standalone window that displays a hexadecimal view of a byte buffer and a
/// footer line showing its size, length and encoding information.
/// </summary>
public partial class HexViewWindow : Window
{
    public HexViewWindow()
    {
        InitializeComponent();
    }

    public HexViewWindow(HexViewViewModel viewModel)
        : this()
    {
        DataContext = viewModel;
    }
}
