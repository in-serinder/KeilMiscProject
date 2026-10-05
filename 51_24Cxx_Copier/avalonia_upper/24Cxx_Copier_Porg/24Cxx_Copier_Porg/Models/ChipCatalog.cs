using System.Collections.Generic;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// Static, back-end owned catalogue of supported 24Cxx devices. The UI reads
/// this list (through the view-model) and never hard-codes chip parameters.
/// </summary>
public static class ChipCatalog
{
    /// <summary>All supported chips, ordered by ascending capacity.</summary>
    public static IReadOnlyList<ChipModel> All { get; } = new[]
    {
        new ChipModel
        {
            Name = "24C01",
            Bits = 1 * 1024,
            Bytes = 128,
            AddressStart = 0x00,
            AddressEnd = 0x7F,
            PageSize = 8,
            AddressBytes = 1,
            Note = "A0/A1/A2 available, I2C address 0x50~0x57",
        },
        new ChipModel
        {
            Name = "24C02",
            Bits = 2 * 1024,
            Bytes = 256,
            AddressStart = 0x00,
            AddressEnd = 0xFF,
            PageSize = 8,
            AddressBytes = 1,
            Note = "Most common small-capacity device",
        },
        new ChipModel
        {
            Name = "24C04",
            Bits = 4 * 1024,
            Bytes = 512,
            AddressStart = 0x00,
            AddressEnd = 0x1FF,
            PageSize = 16,
            AddressBytes = 1,
            Note = "Only A2/A1 available; A0 is an internal address bit",
        },
        new ChipModel
        {
            Name = "24C08",
            Bits = 8 * 1024,
            Bytes = 1024,
            AddressStart = 0x00,
            AddressEnd = 0x3FF,
            PageSize = 16,
            AddressBytes = 1,
            Note = "Only A2 available; A1/A0 are internal address bits",
        },
        new ChipModel
        {
            Name = "24C16",
            Bits = 16 * 1024,
            Bytes = 2048,
            AddressStart = 0x00,
            AddressEnd = 0x7FF,
            PageSize = 16,
            AddressBytes = 1,
            Note = "A2/A1/A0 all internal address bits; I2C fixed at 0x50",
        },
        new ChipModel
        {
            Name = "24C32",
            Bits = 32 * 1024,
            Bytes = 4096,
            AddressStart = 0x000,
            AddressEnd = 0xFFF,
            PageSize = 32,
            AddressBytes = 2,
            Note = "2-byte storage address; A0/A1/A2 configurable I2C address",
        },
        new ChipModel
        {
            Name = "24C64",
            Bits = 64 * 1024,
            Bytes = 8192,
            AddressStart = 0x0000,
            AddressEnd = 0x1FFF,
            PageSize = 32,
            AddressBytes = 2,
            Note = "2-byte storage address",
        },
        new ChipModel
        {
            Name = "24C128",
            Bits = 128 * 1024,
            Bytes = 16384,
            AddressStart = 0x0000,
            AddressEnd = 0x3FFF,
            PageSize = 64,
            AddressBytes = 2,
            Note = "2-byte storage address",
        },
        new ChipModel
        {
            Name = "24C256",
            Bits = 256 * 1024,
            Bytes = 32768,
            AddressStart = 0x0000,
            AddressEnd = 0x7FFF,
            PageSize = 64,
            AddressBytes = 2,
            Note = "2-byte storage address",
        },
        new ChipModel
        {
            Name = "24C512",
            Bits = 512 * 1024,
            Bytes = 65536,
            AddressStart = 0x0000,
            AddressEnd = 0xFFFF,
            PageSize = 128,
            AddressBytes = 2,
            Note = "2-byte storage address",
        },
        new ChipModel
        {
            Name = "24C1024",
            Bits = 1024 * 1024,
            Bytes = 131072,
            AddressStart = 0x00000,
            AddressEnd = 0x1FFFF,
            PageSize = 256,
            AddressBytes = 3,
            Note = "3-byte storage address; only A1/A2 available for I2C address",
        },
    };
}
