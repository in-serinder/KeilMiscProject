using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using _24Cxx_Copier_Porg.Models;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace _24Cxx_Copier_Porg.ViewModels;

/// <summary>
/// View-model for the operation panel. Exposes the writing-mode selection,
/// the slaved address list (fixed 0x50 - 0x57, defined by the back-end) and
/// the global progress / log interface.
/// </summary>
public partial class OpearationPanelViewModel : ViewModelBase
{
    /// <summary>Project repository URL opened by the footer link.</summary>
    public const string ProjectUrl =
        "https://github.com/in-serinder/KeilMiscProject/tree/master/51_24Cxx_Copier";

    public OpearationPanelViewModel()
    {
        // The back-end owns the fixed list of slaved addresses 0x50 - 0x57.
        // The UI only reads this collection and reflects selection/progress.
        AddressSlots = new ObservableCollection<AddressSlotViewModel>(
            BuildFixedAddressList());

        AddressSlots[0].IsSelected = true;

        // Chip catalogue + selection (drives the reserved Write Info panel).
        ChipModels = new ObservableCollection<ChipModelViewModel>(
            ChipCatalog.All.Select(m => new ChipModelViewModel(m)));

        WriteInfo = new WriteInfoViewModel();

        // Default to the most common device.
        SelectedChip = ChipModels.FirstOrDefault(c => c.Name == "24C02")
                       ?? ChipModels.FirstOrDefault();

        SubmitCommand = new RelayCommand(Submit, CanSubmit);
        CancelCommand = new RelayCommand(Cancel);
        SelectAllCommand = new RelayCommand(() => SetAllSelected(true));
        DeselectAllCommand = new RelayCommand(() => SetAllSelected(false));
        OpenProjectLinkCommand = new RelayCommand(() => OpenUrl(ProjectUrl));

        // Route each address row's download button to the read flow.
        WireDownloadHandlers();
    }

    // ---- Port / connection ----

    /// <summary>Name of the serial port this panel operates on (e.g. "COM3").</summary>
    [ObservableProperty]
    private string _portName = string.Empty;

    // ---- Design-time constructor helper ----
    public static OpearationPanelViewModel DesignInstance
    {
        get
        {
            var vm = new OpearationPanelViewModel();
            // Representative sample so the designer/preview shows a populated
            // Write Info panel (including a non-overflow indicator).
            vm.FeedTestWriteInfo(writeAddress: 0x0000, writeSize: 256);
            return vm;
        }
    }

    /// <summary>
    /// Reserved interface for the back-end: feeds the Write Info panel with the
    /// parameters of the operation that is about to be performed.
    /// </summary>
    /// <param name="writeAddress">Start address of the write.</param>
    /// <param name="writeSize">Number of bytes to write.</param>
    public void FeedTestWriteInfo(int writeAddress, int writeSize)
    {
        WriteInfo.WriteAddress = writeAddress;
        WriteInfo.WriteSize = writeSize;
    }

    // ---- Chip selection ----

    /// <summary>All supported chips, ordered by ascending capacity.</summary>
    public ObservableCollection<ChipModelViewModel> ChipModels { get; }

    /// <summary>
    /// Reserved "Write Info" panel view-model. Feeding it with the selected
    /// chip / write parameters drives the read-only summary UI.
    /// </summary>
    public WriteInfoViewModel WriteInfo { get; }

    /// <summary>
    /// Currently selected chip. Selecting a chip feeds the reserved Write Info
    /// panel with the matching device parameters.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SelectedChipName))]
    [NotifyPropertyChangedFor(nameof(HasSelectedChip))]
    private ChipModelViewModel? _selectedChip;

    /// <summary>Convenience accessor for the selected chip model name.</summary>
    public string SelectedChipName => SelectedChip?.Name ?? "-";

    /// <summary>True when a chip is selected.</summary>
    public bool HasSelectedChip => SelectedChip is not null;

    partial void OnSelectedChipChanged(ChipModelViewModel? value)
    {
        if (WriteInfo is not null)
        {
            WriteInfo.Chip = value?.Model;
        }
    }

    // ---- Writing mode ----

    /// <summary>Currently selected writing mode.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsTextMode))]
    [NotifyPropertyChangedFor(nameof(IsBinaryFileMode))]
    [NotifyPropertyChangedFor(nameof(IsFill0XffMode))]
    [NotifyPropertyChangedFor(nameof(IsFill0X00Mode))]
    [NotifyPropertyChangedFor(nameof(IsFillCustomMode))]
    [NotifyPropertyChangedFor(nameof(IsConstantFillMode))]
    private WritingMode _writingMode = WritingMode.Text;

    public bool IsTextMode => WritingMode == WritingMode.Text;
    public bool IsBinaryFileMode => WritingMode == WritingMode.BinaryFile;
    public bool IsFill0XffMode => WritingMode == WritingMode.Fill0xFF;
    public bool IsFill0X00Mode => WritingMode == WritingMode.Fill0x00;
    public bool IsFillCustomMode => WritingMode == WritingMode.FillCustom;

    /// <summary>True for the read-only constant fill modes (0xFF / 0x00).</summary>
    public bool IsConstantFillMode => IsFill0XffMode || IsFill0X00Mode;

    // ---- Mode payloads ----

    /// <summary>Path of the file used in binary-file write mode.</summary>
    [ObservableProperty]
    private string _targetFilePath = string.Empty;

    /// <summary>Text payload used in text write mode.</summary>
    [ObservableProperty]
    private string _textContent = string.Empty;

    /// <summary>Custom byte value (hex string) used in custom fill mode.</summary>
    [ObservableProperty]
    private string _customFillValue = "0xAA";

    // ---- Address selection ----

    /// <summary>Fixed slaved-address rows (0x50 - 0x57).</summary>
    public ObservableCollection<AddressSlotViewModel> AddressSlots { get; }

    /// <summary>All addresses currently selected.</summary>
    public System.Collections.Generic.IEnumerable<AddressSlotViewModel> SelectedSlots =>
        AddressSlots.Where(s => s.IsSelected);

    // ---- Global progress ----

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(TotalProgressText))]
    private double _totalProgress;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(TotalProgressText))]
    private double _totalMaximum = 100;

    [ObservableProperty]
    private bool _isBusy;

    public string TotalProgressText =>
        TotalMaximum <= 0 ? "0%" : $"{TotalProgress / TotalMaximum * 100:0}%";

    // ---- Log ----

    /// <summary>Tagged log lines (see <see cref="LogKind"/> / <see cref="LogEntry"/>).</summary>
    public ObservableCollection<LogEntry> Logs { get; } = new();

    // ---- Commands ----

    public IRelayCommand SubmitCommand { get; }
    public IRelayCommand CancelCommand { get; }
    public IRelayCommand SelectAllCommand { get; }
    public IRelayCommand DeselectAllCommand { get; }
    public IRelayCommand OpenProjectLinkCommand { get; }

    private void SetAllSelected(bool value)
    {
        foreach (var slot in AddressSlots)
        {
            slot.IsSelected = value;
        }
    }

    /// <summary>
    /// Opens the given URL with the system default browser.
    /// </summary>
    private static void OpenUrl(string url)
    {
        try
        {
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            {
                Process.Start("open", url);
            }
            else
            {
                Process.Start("xdg-open", url);
            }
        }
        catch
        {
            // Ignore failures to open the browser.
        }
    }

    /// <summary>
    /// Builds the fixed slaved address list 0x50 - 0x57.
    /// </summary>
    private static AddressSlotViewModel[] BuildFixedAddressList()
    {
        var slots = new AddressSlotViewModel[8];
        for (var i = 0; i < slots.Length; i++)
        {
            var address = (byte)(0x50 + i);
            slots[i] = new AddressSlotViewModel(
                new AddressSlot { Address = address });
        }

        return slots;
    }
}

