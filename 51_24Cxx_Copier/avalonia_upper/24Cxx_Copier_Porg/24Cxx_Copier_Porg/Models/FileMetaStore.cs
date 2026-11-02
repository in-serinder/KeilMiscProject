using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace _24Cxx_Copier_Porg.Models;

/// <summary>
/// One remembered write: which file was streamed into which slot.
///
/// The EEPROM only stores bytes — it cannot remember that those bytes used to
/// be a PNG or an ICO. So the name/extension/length live here, on the PC, and a
/// read-back is matched against it by CRC-32.
/// </summary>
public sealed class FileMetaRecord
{
    /// <summary>I2C slot address the payload was written to (0x50 - 0x57).</summary>
    public int Address { get; set; }

    /// <summary>File name including the extension, e.g. "logo.ico".</summary>
    public string FileName { get; set; } = string.Empty;

    /// <summary>Extension without the dot (`"ico"`), empty when the file had none.</summary>
    public string Extension { get; set; } = string.Empty;

    /// <summary>Payload length in bytes — the real end of the file inside the chip.</summary>
    public long Length { get; set; }

    /// <summary>CRC-32 of the first <see cref="Length"/> bytes, as written.</summary>
    public uint Crc32 { get; set; }

    public DateTimeOffset SavedAt { get; set; }

    [JsonIgnore]
    public string ExtensionText => string.IsNullOrEmpty(Extension) ? "(none)" : "." + Extension;

    [JsonIgnore]
    public string Summary => $"{FileName} ({Length} B, {ExtensionText})";
}

/// <summary>On-disk shape of the metadata file (kept versioned for future changes).</summary>
public sealed class FileMetaDocument
{
    public int Version { get; set; } = 1;

    public List<FileMetaRecord> Records { get; set; } = new();
}

/// <summary>
/// Persistent store of "this slot was written with this file" records.
///
/// File: <c>%APPDATA%\24Cxx_Copier\file-meta.json</c>. Failing to read or write
/// it is never fatal — the copier still writes raw bytes, it just cannot restore
/// the original file type on export.
/// </summary>
public sealed class FileMetaStore
{
    /// <summary>How many records are kept (oldest are dropped).</summary>
    public const int MaxRecords = 64;

    private static readonly Lazy<FileMetaStore> LazyInstance = new(() => new FileMetaStore());

    public static FileMetaStore Instance => LazyInstance.Value;

    private readonly object _gate = new();
    private FileMetaDocument _doc = new();

    /// <summary>Full path of the JSON sidecar file.</summary>
    public string StorePath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "24Cxx_Copier",
        "file-meta.json");

    private FileMetaStore() => Load();

    private void Load()
    {
        try
        {
            if (!File.Exists(StorePath))
            {
                return;
            }

            var json = File.ReadAllText(StorePath);
            var doc = JsonSerializer.Deserialize<FileMetaDocument>(json);
            if (doc?.Records is not null)
            {
                _doc = doc;
            }
        }
        catch
        {
            // Corrupt / unreadable sidecar: start from scratch instead of
            // blocking the copier.
            _doc = new FileMetaDocument();
        }
    }

    private void Save()
    {
        try
        {
            var dir = Path.GetDirectoryName(StorePath);
            if (!string.IsNullOrEmpty(dir))
            {
                Directory.CreateDirectory(dir);
            }

            var json = JsonSerializer.Serialize(_doc, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(StorePath, json);
        }
        catch
        {
            // Best effort: the transfer itself already succeeded.
        }
    }

    /// <summary>
    /// Records that <paramref name="content"/> (read from <paramref name="filePath"/>)
    /// was written to <paramref name="address"/>. Re-writing the same content to
    /// the same slot replaces the old record instead of piling up duplicates.
    /// </summary>
    public void Remember(int address, string filePath, ReadOnlySpan<byte> content)
    {
        if (content.Length == 0 || string.IsNullOrWhiteSpace(filePath))
        {
            return;
        }

        var name = Path.GetFileName(filePath);
        var ext = Path.GetExtension(name).TrimStart('.').ToLowerInvariant();
        var crc = Crc32.Compute(content);
        var length = content.Length;          // span 不能进 lambda，先取成普通值

        lock (_gate)
        {
            _doc.Records.RemoveAll(r => r.Address == address && r.Crc32 == crc && r.Length == length);
            _doc.Records.Insert(0, new FileMetaRecord
            {
                Address = address,
                FileName = name,
                Extension = ext,
                Length = length,
                Crc32 = crc,
                SavedAt = DateTimeOffset.Now,
            });

            if (_doc.Records.Count > MaxRecords)
            {
                _doc.Records.RemoveRange(MaxRecords, _doc.Records.Count - MaxRecords);
            }

            Save();
        }
    }

    /// <summary>
    /// Finds the record whose payload matches the beginning of
    /// <paramref name="readData"/>. The read-back buffer is normally the whole
    /// chip, so the comparison is only over the recorded length.
    /// </summary>
    public FileMetaRecord? Match(int address, ReadOnlySpan<byte> readData)
    {
        var available = readData.Length;      // span 不能进 lambda，先取长度

        List<FileMetaRecord> candidates;
        lock (_gate)
        {
            candidates = _doc.Records
                .Where(r => r.Address == address && r.Length > 0 && r.Length <= available)
                .OrderByDescending(r => r.SavedAt)
                .ToList();
        }

        foreach (var record in candidates)
        {
            if (Crc32.Compute(readData, (int)record.Length) == record.Crc32)
            {
                return record;
            }
        }

        return null;
    }

    /// <summary>Newest record for a slot, regardless of content (used for hints).</summary>
    public FileMetaRecord? Latest(int address)
    {
        lock (_gate)
        {
            return _doc.Records
                .Where(r => r.Address == address)
                .OrderByDescending(r => r.SavedAt)
                .FirstOrDefault();
        }
    }

    /// <summary>Snapshot of all records, newest first.</summary>
    public IReadOnlyList<FileMetaRecord> Records
    {
        get
        {
            lock (_gate)
            {
                return _doc.Records.ToList();
            }
        }
    }
}
