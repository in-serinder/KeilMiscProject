namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// Immutable description of a single slaved EEPROM address that can be
/// selected as a write target. The list of these is defined once in the
/// back-end (0x50 - 0x57) and consumed by the UI through bindings only.
/// </summary>
public sealed class AddressSlot
{
    /// <summary>Numeric device address, e.g. 0x50 .. 0x57.</summary>
    public byte Address { get; init; }

    /// <summary>Human readable address, e.g. "0x50".</summary>
    public string AddressText => $"0x{Address:X2}";

    /// <summary>Short label shown centered inside the progress bar.</summary>
    public string Description { get; init; } = string.Empty;
}
