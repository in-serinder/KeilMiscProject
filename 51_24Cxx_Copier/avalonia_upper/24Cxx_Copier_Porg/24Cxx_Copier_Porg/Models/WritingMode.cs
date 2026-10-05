namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// The mode used when submitting a write operation to the selected device(s).
/// </summary>
public enum WritingMode
{
    /// <summary>Write a plain UTF-8 text payload to the device.</summary>
    Text,

    /// <summary>Write raw binary bytes taken from a file.</summary>
    BinaryFile,

    /// <summary>Fill the whole device with 0xFF.</summary>
    Fill0xFF,

    /// <summary>Fill the whole device with 0x00.</summary>
    Fill0x00,

    /// <summary>Fill the whole device with a user supplied byte value.</summary>
    FillCustom
}
