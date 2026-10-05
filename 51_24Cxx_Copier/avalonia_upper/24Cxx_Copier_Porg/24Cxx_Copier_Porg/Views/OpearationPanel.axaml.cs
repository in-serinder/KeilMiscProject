using _24Cxx_Copier_Porg.ViewModels;
using Avalonia.Controls;
namespace _24Cxx_Copier_Porg.Views;

public partial class OpearationPanel : Window
{
    private OpearationPanelViewModel? _viewModel;

    public OpearationPanel()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    public OpearationPanel(OpearationPanelViewModel viewModel)
        : this()
    {
        DataContext = viewModel;
    }

    private void OnDataContextChanged(object? sender, System.EventArgs e)
    {
        if (_viewModel is not null)
        {
            _viewModel.HexDumpRequested -= OnHexDumpRequested;
        }

        _viewModel = DataContext as OpearationPanelViewModel;

        if (_viewModel is not null)
        {
            _viewModel.HexDumpRequested += OnHexDumpRequested;
        }
    }

    private void OnHexDumpRequested(object? sender, HexDumpRequest request)
    {
        var hexVm = new HexViewViewModel(request.Data, request.Title)
        {
            Encoding = System.Text.Encoding.ASCII,
            BytesPerLine = 16,
            IsEditable = false,
        };

        new HexViewWindow(hexVm)
        {
            Width = 780,
            Height = 520,
        }.Show(this);
    }
}
