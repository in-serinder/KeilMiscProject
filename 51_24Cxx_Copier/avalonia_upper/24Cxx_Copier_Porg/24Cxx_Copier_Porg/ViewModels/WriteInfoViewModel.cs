using _24Cxx_Copier_Porg.Models;
using CommunityToolkit.Mvvm.ComponentModel;

namespace _24Cxx_Copier_Porg.ViewModels;

/// <summary>
/// View-model for the "Write Info" panel. This is a reserved control: it does
/// not perform any write, but it exposes the interface the back-end will use to
/// push the operation parameters and read the resulting summary.
///
/// Inputs (set by the back-end / other view-models):
///   <see cref="Chip"/>            – the currently selected device
///   <see cref="WriteSize"/>       – number of bytes about to be written
///   <see cref="WriteAddress"/>    – start address of the write
///
/// Derived (read-only, shown in the UI):
///   <see cref="ChipRangeText"/>   – the chip's internal address range
///   <see cref="CapacityText"/>    – the chip capacity
///   <see cref="WriteSizeText"/>   – the current write size
///   <see cref="WriteRangeText"/>  – the write address range
///   <see cref="IsOverflow"/>      – whether the write exceeds the chip
/// </summary>
public partial class WriteInfoViewModel : ViewModelBase
{
    /// <summary>Ascending asset names for the overflow indicator.</summary>
    public const string OverflowYesAsset = "avares://24Cxx_Copier_Porg/Assets/overflow_yes.png";
    public const string OverflowNoAsset = "avares://24Cxx_Copier_Porg/Assets/overflow_no.png";

    // ---- Reserved inputs ----

    /// <summary>Currently selected chip. Set by the parent view-model.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ChipNameText))]
    [NotifyPropertyChangedFor(nameof(ChipRangeText))]
    [NotifyPropertyChangedFor(nameof(CapacityText))]
    [NotifyPropertyChangedFor(nameof(IsOverflow))]
    [NotifyPropertyChangedFor(nameof(OverflowText))]
    [NotifyPropertyChangedFor(nameof(OverflowAsset))]
    private ChipModel? _chip;

    /// <summary>Number of bytes to be written. Reserved input.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(WriteSizeText))]
    [NotifyPropertyChangedFor(nameof(WriteEndAddress))]
    [NotifyPropertyChangedFor(nameof(WriteRangeText))]
    [NotifyPropertyChangedFor(nameof(IsOverflow))]
    [NotifyPropertyChangedFor(nameof(OverflowText))]
    [NotifyPropertyChangedFor(nameof(OverflowAsset))]
    private int _writeSize;

    /// <summary>Start address of the write. Reserved input.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(WriteEndAddress))]
    [NotifyPropertyChangedFor(nameof(WriteRangeText))]
    [NotifyPropertyChangedFor(nameof(IsOverflow))]
    [NotifyPropertyChangedFor(nameof(OverflowText))]
    [NotifyPropertyChangedFor(nameof(OverflowAsset))]
    private int _writeAddress;

    // ---- Derived display values ----

    /// <summary>Selected chip model name, or a placeholder when none.</summary>
    public string ChipNameText => Chip?.Name ?? "-";

    /// <summary>Chip internal address range (read-only display).</summary>
    public string ChipRangeText => Chip?.AddressRangeText ?? "-";

    /// <summary>Chip capacity, e.g. "2 Kbit / 256 Byte".</summary>
    public string CapacityText =>
        Chip is null ? "-" : $"{Chip.BitsText} / {Chip.BytesText}";

    /// <summary>Current write size, e.g. "128 Byte".</summary>
    public string WriteSizeText => $"{WriteSize} Byte";

    /// <summary>
    /// End address of the write (inclusive). One past the last written byte
    /// when the size is non-zero.
    /// </summary>
    public int WriteEndAddress => WriteAddress + System.Math.Max(0, WriteSize) - 1;

    /// <summary>Write address range (read-only display), e.g. "0x0000 ~ 0x007F".</summary>
    public string WriteRangeText
    {
        get
        {
            if (WriteSize <= 0)
            {
                return "-";
            }

            var end = WriteEndAddress;
            var digits = end <= 0xFF ? 2
                : end <= 0xFFFF ? 4
                : end <= 0xFFFFF ? 5
                : 6;
            return $"0x{WriteAddress.ToString($"X{digits}")} ~ 0x{end.ToString($"X{digits}")}";
        }
    }

    /// <summary>True when the write would exceed the chip's internal range.</summary>
    public bool IsOverflow
    {
        get
        {
            if (Chip is null || WriteSize <= 0)
            {
                return false;
            }

            return WriteAddress < Chip.AddressStart
                || WriteEndAddress > Chip.AddressEnd;
        }
    }

    /// <summary>"YES" / "NO" overflow indicator.</summary>
    public string OverflowText => IsOverflow ? "YES" : "NO";

    /// <summary>Asset shown next to the overflow indicator.</summary>
    public string OverflowAsset => IsOverflow ? OverflowYesAsset : OverflowNoAsset;
}

