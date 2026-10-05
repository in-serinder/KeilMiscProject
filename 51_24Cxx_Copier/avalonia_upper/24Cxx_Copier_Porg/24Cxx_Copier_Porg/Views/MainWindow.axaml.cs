using System;
using _24Cxx_Copier_Porg.ViewModels;
using Avalonia.Controls;

namespace _24Cxx_Copier_Porg.Views;

/// <summary>
/// Port-selection / validation window. It is shown first; the view-model
/// scans every serial port, probes them for the <c>TYPE_ECHO</c> device and,
/// once the user confirms a port, this window opens the operation panel and
/// closes itself.
/// </summary>
public partial class MainWindow : Window
{
    private MainWindowViewModel? _viewModel;

    public MainWindow()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (_viewModel is not null)
        {
            _viewModel.PortConfirmed -= OnPortConfirmed;
            _viewModel.CloseRequested -= OnCloseRequested;
        }

        _viewModel = DataContext as MainWindowViewModel;

        if (_viewModel is not null)
        {
            _viewModel.PortConfirmed += OnPortConfirmed;
            _viewModel.CloseRequested += OnCloseRequested;
        }
    }

    private void OnPortConfirmed(object? sender, ComPortInfo port)
    {
        // Build the operation panel for the chosen port and show it.
        var operationVm = new OpearationPanelViewModel
        {
            PortName = port.PortName,
        };

        var panel = new OpearationPanel(operationVm);
        panel.Show();

        Close();
    }

    private void OnCloseRequested(object? sender, EventArgs e) => Close();
}