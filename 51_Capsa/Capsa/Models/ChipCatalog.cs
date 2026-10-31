using System.Collections.Generic;

namespace Capsa.Models
{
    /// <summary>
    /// 存储芯片型号定义（对齐 51_24Cxx_Copier 的 ChipCatalog）。
    /// </summary>
    public sealed class ChipDefinition
    {
        public required string Name { get; init; }

        /// <summary>容量（bit）。</summary>
        public int Bits { get; init; }

        /// <summary>容量（byte）。</summary>
        public int Bytes { get; init; }

        public int PageSize { get; init; }

        public int AddressBytes { get; init; }

        public string Note { get; init; } = string.Empty;

        /// <summary>下拉框展示文本，例如 “AT24C256 · 32 KB”。</summary>
        public string Display => $"{Name}  ·  {ByteSize.Format(Bytes)}";

        /// <summary>下拉框右侧的容量文本。</summary>
        public string CapacityText => ByteSize.Format(Bytes);

        public override string ToString() => Display;
    }

    /// <summary>预置芯片型号目录（与参考项目 24Cxx 系列保持一致）。</summary>
    public static class ChipCatalog
    {
        public static IReadOnlyList<ChipDefinition> All { get; } = new[]
        {
            new ChipDefinition { Name = "24C01", Bits = 1 * 1024, Bytes = 128, PageSize = 8, AddressBytes = 1, Note = "A0/A1/A2 available" },
            new ChipDefinition { Name = "24C02", Bits = 2 * 1024, Bytes = 256, PageSize = 8, AddressBytes = 1, Note = "Most common small-capacity device" },
            new ChipDefinition { Name = "24C04", Bits = 4 * 1024, Bytes = 512, PageSize = 16, AddressBytes = 1, Note = "Only A2/A1 available" },
            new ChipDefinition { Name = "24C08", Bits = 8 * 1024, Bytes = 1024, PageSize = 16, AddressBytes = 1, Note = "Only A2 available" },
            new ChipDefinition { Name = "24C16", Bits = 16 * 1024, Bytes = 2048, PageSize = 16, AddressBytes = 1, Note = "I2C address fixed at 0x50" },
            new ChipDefinition { Name = "24C32", Bits = 32 * 1024, Bytes = 4096, PageSize = 32, AddressBytes = 2, Note = "2-byte storage address" },
            new ChipDefinition { Name = "24C64", Bits = 64 * 1024, Bytes = 8192, PageSize = 32, AddressBytes = 2, Note = "2-byte storage address" },
            new ChipDefinition { Name = "24C128", Bits = 128 * 1024, Bytes = 16384, PageSize = 64, AddressBytes = 2, Note = "2-byte storage address" },
            new ChipDefinition { Name = "24C256", Bits = 256 * 1024, Bytes = 32768, PageSize = 64, AddressBytes = 2, Note = "2-byte storage address" },
            new ChipDefinition { Name = "24C512", Bits = 512 * 1024, Bytes = 65536, PageSize = 128, AddressBytes = 2, Note = "2-byte storage address" },
            new ChipDefinition { Name = "24C1024", Bits = 1024 * 1024, Bytes = 131072, PageSize = 256, AddressBytes = 3, Note = "3-byte storage address" },
        };

        public static ChipDefinition? Find(string? name)
        {
            if (string.IsNullOrWhiteSpace(name))
            {
                return null;
            }

            foreach (var chip in All)
            {
                if (string.Equals(chip.Name, name, System.StringComparison.OrdinalIgnoreCase))
                {
                    return chip;
                }
            }

            return null;
        }
    }
}
