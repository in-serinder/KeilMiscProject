namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// Immutable description of a supported 24Cxx series EEPROM device.
/// The catalogue of these is owned by the back-end and consumed by the UI
/// through bindings only.
/// </summary>
public sealed class ChipModel
{
    /// <summary>Device model name, e.g. "24C02".</summary>
    public required string Name { get; init; }

    /// <summary>Capacity in bits, e.g. 2048 for 24C02 (2 Kbit).</summary>
    public required int Bits { get; init; }

    /// <summary>Capacity in bytes, e.g. 256 for 24C02.</summary>
    public required int Bytes { get; init; }

    /// <summary>Lowest internal memory address (inclusive).</summary>
    public required int AddressStart { get; init; }

    /// <summary>Highest internal memory address (inclusive).</summary>
    public required int AddressEnd { get; init; }

    /// <summary>Page size in bytes.</summary>
    public required int PageSize { get; init; }

    /// <summary>Number of bytes used to encode the internal storage address.</summary>
    public required int AddressBytes { get; init; }

    /// <summary>Free-form remarks (I2C address wiring, etc.).</summary>
    public string Note { get; init; } = string.Empty;

    /// <summary>Human readable capacity in bits, e.g. "2 Kbit".</summary>
    public string BitsText => Bits >= 1024
        ? $"{Bits / 1024} Mbit ({Bits} Kbit)"
        : $"{Bits} Kbit";

    /// <summary>Human readable capacity in bytes, e.g. "256 Byte".</summary>
    public string BytesText => $"{Bytes} Byte";

    /// <summary>Human readable internal address range, e.g. "0x00 ~ 0xFF".</summary>
    public string AddressRangeText
    {
        get
        {
            // Pad both endpoints to the same width, based on the end address.
            var digits = AddressEnd <= 0xFF ? 2
                : AddressEnd <= 0xFFFF ? 4
                : 5;
            return $"0x{AddressStart.ToString($"X{digits}")} ~ 0x{AddressEnd.ToString($"X{digits}")}";
        }
    }
}

