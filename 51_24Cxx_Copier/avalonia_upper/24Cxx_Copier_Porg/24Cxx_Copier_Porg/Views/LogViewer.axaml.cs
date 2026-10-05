using System.Collections.Specialized;
using _24Cxx_Copier_Porg.ViewModels;
using Avalonia.Controls;
using Avalonia.Threading;

namespace _24Cxx_Copier_Porg.Views;

public partial class LogViewer : UserControl
{
    private ScrollViewer? _scroll;
    private INotifyCollectionChanged? _logs;

    public LogViewer()
    {
        InitializeComponent();

        _scroll = this.FindControl<ScrollViewer>("LogScroll");
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, System.EventArgs e)
    {
        // Detach from the previous log collection.
        if (_logs is not null)
        {
            _logs.CollectionChanged -= OnLogsChanged;
        }

        _logs = (DataContext as OpearationPanelViewModel)?.Logs;

        if (_logs is not null)
        {
            _logs.CollectionChanged += OnLogsChanged;
        }
    }

    private void OnLogsChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.Action != NotifyCollectionChangedAction.Add)
        {
            return;
        }

        // Scroll after the ItemsControl has realised the new item.
        Dispatcher.UIThread.Post(
            () => _scroll?.ScrollToEnd(),
            DispatcherPriority.Background);
    }
}
