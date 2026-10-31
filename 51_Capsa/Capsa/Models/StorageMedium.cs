using System.Collections.Generic;

namespace Capsa.Models
{
    /// <summary>存储介质类型。</summary>
    public enum StorageMedium
    {
        Eeprom = 0,
        Fram = 1,
        Eeram = 2,
        Sram = 3,
    }

    /// <summary>介质下拉框条目（自带展示名，ComboBox 直接使用 ToString）。</summary>
    public sealed class StorageMediumOption
    {
        public StorageMediumOption(StorageMedium value, string displayName)
        {
            Value = value;
            DisplayName = displayName;
        }

        public StorageMedium Value { get; }

        public string DisplayName { get; }

        public override string ToString() => DisplayName;
    }

    /// <summary>预置介质清单。</summary>
    public static class StorageMediumCatalog
    {
        public static IReadOnlyList<StorageMediumOption> All { get; } = new[]
        {
            new StorageMediumOption(StorageMedium.Eeprom, "EEPROM"),
            new StorageMediumOption(StorageMedium.Fram, "FRAM"),
            new StorageMediumOption(StorageMedium.Eeram, "EERAM"),
            new StorageMediumOption(StorageMedium.Sram, "SRAM"),
        };
    }
}
