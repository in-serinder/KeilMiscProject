using System;
using System.Globalization;
using Avalonia.Data.Converters;

namespace _24Cxx_Copier_Porg.Converters;

/// <summary>
/// Two-way converter that maps an enum value to a boolean suitable for a
/// radio-button's <c>IsChecked</c>. Provide the target enum member through the
/// <see cref="Parameter"/> (as a string) so a single converter instance can be
/// reused across a radio group.
/// </summary>
public sealed class EnumToBoolConverter : IValueConverter
{
    public static readonly EnumToBoolConverter Instance = new();

    /// <summary>
    /// Static factory used from XAML, e.g.
    /// <c>{Binding Mode, Converter={x:Static conv:EnumToBoolConverter.Instance}, ConverterParameter=Text}</c>
    /// </summary>
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is null || parameter is null)
        {
            return false;
        }

        return string.Equals(value.ToString(), parameter.ToString(), StringComparison.Ordinal);
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        // Radio buttons push the enum member when checked; ignore uncheck.
        if (value is true && parameter is not null && targetType.IsEnum)
        {
            return Enum.Parse(targetType, parameter.ToString()!);
        }

        return Avalonia.Data.BindingOperations.DoNothing;
    }
}
