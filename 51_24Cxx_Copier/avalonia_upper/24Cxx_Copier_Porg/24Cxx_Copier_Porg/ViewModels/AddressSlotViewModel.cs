using System;
using System.Threading.Tasks;
using _24Cxx_Copier_Porg.Models;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace _24Cxx_Copier_Porg.ViewModels;

/// <summary>
/// View-model backing a single selectable address row (checkbox + progress +
/// address + download). The underlying address data is provided by the
/// back-end (fixed list 0x50 - 0x57); this class only exposes the selectable
/// state, read/write progress and a download trigger so the UI binds purely to
/// data.
/// </summary>
public partial class AddressSlotViewModel : ViewModelBase
{
    public AddressSlotViewModel(byte address, string description)
    {
        Address = address;
        Description = description;
        DownloadCommand = new AsyncRelayCommand(DownloadAsync, CanExecuteDownload);
    }

    public AddressSlotViewModel(AddressSlot slot)
        : this(slot.Address, slot.Description)
    {
    }

    /// <summary>Numeric address (0x50..0x57).</summary>
    public byte Address { get; }

    /// <summary>Human readable address, e.g. "0x50".</summary>
    public string AddressText => $"0x{Address:X2}";

    /// <summary>
    /// Callback invoked when the row's download button is pressed. Wired up by
    /// the parent view-model (which owns the transport).
    /// </summary>
    public Func<AddressSlotViewModel, Task>? DownloadRequested { get; set; }

    /// <summary>Whether this address is selected as a read/write target.</summary>
    [ObservableProperty]
    private bool _isSelected;

    /// <summary>Description shown in the middle of the progress bar.</summary>
    [ObservableProperty]
    private string _description = string.Empty;

    /// <summary>Current progress value (0..Maximum).</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ProgressText))]
    private double _progress;

    /// <summary>Maximum progress value.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ProgressText))]
    private double _maximum = 100;

    /// <summary>Whether the progress bar shows a busy/indeterminate animation.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ProgressText))]
    private bool _isIndeterminate;

    /// <summary>True while a download (read) is running for this address.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(DownloadCommand))]
    private bool _isDownloading;

    /// <summary>Whether the download button is enabled.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(DownloadCommand))]
    private bool _canDownload = true;

    /// <summary>Progress label shown on the right side, e.g. "42%".</summary>
    public string ProgressText =>
        IsIndeterminate || Maximum <= 0
            ? "\u2026"
            : $"{Progress / Maximum * 100:0}%";

    // ---- Commands ----

    /// <summary>Triggers a read (download) of this address from the device.</summary>
    public IAsyncRelayCommand DownloadCommand { get; }

    private bool CanExecuteDownload() => CanDownload && !IsDownloading;

    private Task DownloadAsync()
        => DownloadRequested is null ? Task.CompletedTask : DownloadRequested(this);

    /// <summary>Updates the progress with an explicit maximum (bytes).</summary>
    public void SetByteProgress(int done, int total)
    {
        Maximum = Math.Max(1, total);
        Progress = done;
        IsIndeterminate = false;
    }

    /// <summary>Resets the row to its idle state.</summary>
    public void Reset()
    {
        Progress = 0;
        IsIndeterminate = false;
    }
}

