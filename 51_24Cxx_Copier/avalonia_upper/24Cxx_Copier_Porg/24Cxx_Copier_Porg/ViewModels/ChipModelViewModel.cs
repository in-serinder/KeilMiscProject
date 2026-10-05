using _24Cxx_Copier_Porg.Models;

namespace _24Cxx_Copier_Porg.ViewModels;

/// <summary>
/// View-model for a single entry in the chip selection drop-down. Exposes the
/// model name prominently plus the capacity and address range in a muted style.
/// </summary>
public sealed class ChipModelViewModel
{
    public ChipModelViewModel(ChipModel model)
    {
        Model = model;
    }

    /// <summary>The underlying chip definition.</summary>
    public ChipModel Model { get; }

    /// <summary>Model name, e.g. "24C02".</summary>
    public string Name => Model.Name;

    /// <summary>Capacity rendered as a muted sub-line, e.g. "2 Kbit / 256 Byte".</summary>
    public string CapacityText => $"{Model.BitsText} / {Model.BytesText}";

    /// <summary>Address range rendered as a muted sub-line.</summary>
    public string AddressRangeText => Model.AddressRangeText;

    /// <summary>Page size rendered as a muted sub-line.</summary>
    public string PageText => $"Page {Model.PageSize}B";

    public override string ToString() => Name;
}
