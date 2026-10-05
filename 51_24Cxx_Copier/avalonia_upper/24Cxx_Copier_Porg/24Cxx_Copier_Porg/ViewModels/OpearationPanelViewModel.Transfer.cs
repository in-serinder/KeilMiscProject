using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using _24Cxx_Copier_Porg.Models;
using CommunityToolkit.Mvvm.Input;

namespace _24Cxx_Copier_Porg.ViewModels;

/// <summary>
/// Transfer orchestration for <see cref="OpearationPanelViewModel"/>:
/// building the payload, running the per-address write queue and the
/// per-address read (download) flow, plus progress and log bookkeeping.
///
/// Kept in its own file so the main view-model stays focused on the interface.
/// The actual protocol work lives in <see cref="CopierTransferService"/>.
/// </summary>
public partial class OpearationPanelViewModel
{
    /// <summary>Raised when a byte buffer should be shown in a hex viewer window.</summary>
    public event EventHandler<HexDumpRequest>? HexDumpRequested;

    private CancellationTokenSource? _transferCts;

    /// <summary>Wires each address row's download button to the read flow.</summary>
    private void WireDownloadHandlers()
    {
        foreach (var slot in AddressSlots)
        {
            slot.DownloadRequested = DownloadSlotAsync;
        }
    }

    /// <summary>
    /// Loads the given file as raw binary and shows it in the hex viewer.
    /// Called by the view after the user picks a file in binary-file mode.
    /// </summary>
    public void PreviewFile(string filePath)
    {
        TargetFilePath = filePath;

        var result = WritePayloadBuilder.BuildFromFile(filePath);
        if (!result.Success)
        {
            Log(LogKind.Error, result.Error);
            return;
        }

        WriteInfo.WriteAddress = 0;
        WriteInfo.WriteSize = result.Data.Length;

        Log(LogKind.Operation,
            $"Loaded file '{System.IO.Path.GetFileName(filePath)}' ({result.Data.Length} bytes).");

        HexDumpRequested?.Invoke(this,
            new HexDumpRequest(result.Data,
                $"File — {System.IO.Path.GetFileName(filePath)} ({result.Data.Length} bytes)"));
    }

    private bool CanSubmit() => SelectedSlots.Any() && !IsBusy;

    // ---------------------------------------------------------------------
    // Write flow (Submit)
    // ---------------------------------------------------------------------

    private async void Submit()
    {
        if (IsBusy)
        {
            return;
        }

        // 1) Build the payload from the selected writing mode. For the fill
        //    modes the payload spans the whole selected chip.
        var fillLength = SelectedChip?.Model?.Bytes ?? 0;
        var build = WritePayloadBuilder.Build(
            WritingMode, TextContent, TargetFilePath, CustomFillValue, fillLength);

        if (!build.Success)
        {
            Log(LogKind.Error, build.Error);
            return;
        }

        var payload = build.Data;
        Log(LogKind.Operation,
            $"Payload ready: {payload.Length} byte(s) via {WritingMode}.");

        // 2) Feed the reserved info panel with the size that will be written.
        WriteInfo.WriteAddress = 0;
        WriteInfo.WriteSize = payload.Length;

        if (WriteInfo.IsOverflow)
        {
            Log(LogKind.Error,
                $"Write size {payload.Length} exceeds {SelectedChipName} capacity. Aborted.");
            return;
        }

        // 3) Show the payload in a hex viewer — except for the fill modes
        //    (0xFF / 0x00 / custom), where the buffer is just a run of one byte.
        var isFillMode = IsConstantFillMode || IsFillCustomMode;
        if (!isFillMode)
        {
            HexDumpRequested?.Invoke(this,
                new HexDumpRequest(payload, $"Write payload — {payload.Length} bytes ({WritingMode})"));
        }

        // 4) Run one write task per selected address, sequentially.
        var targets = SelectedSlots.ToList();
        await RunQueueAsync(targets, payload).ConfigureAwait(true);
    }

    /// <summary>
    /// Runs the write flow for each selected address in turn, updating the
    /// per-address and overall progress.
    /// </summary>
    private async Task RunQueueAsync(IReadOnlyList<AddressSlotViewModel> targets, byte[] payload)
    {
        _transferCts = new CancellationTokenSource();
        var ct = _transferCts.Token;

        IsBusy = true;
        TotalProgress = 0;
        TotalMaximum = Math.Max(1, targets.Count * payload.Length);
        SubmitCommand.NotifyCanExecuteChanged();

        // Reset all rows for a clean run.
        foreach (var slot in AddressSlots)
        {
            slot.Reset();
        }

        try
        {
            using var session = new CopierProtocolSession(PortName, msg => LogRaw(msg));
            session.Open();

            var service = new CopierTransferService(session, msg => LogRaw(msg));
            var completedBytes = 0;

            foreach (var slot in targets)
            {
                ct.ThrowIfCancellationRequested();

                slot.IsIndeterminate = false;
                slot.SetByteProgress(0, payload.Length);
                slot.Description = $"Writing 0x{slot.Address:X2}…";

                var slotProgress = new CopierTransferService.ProgressCallback((done, total) =>
                {
                    slot.SetByteProgress(done, total);
                    TotalProgress = completedBytes + done;
                });

                try
                {
                    await service.WriteAsync(slot.Address, payload, slotProgress, ct)
                        .ConfigureAwait(true);

                    completedBytes += payload.Length;
                    TotalProgress = completedBytes;
                    slot.Description = $"Written 0x{slot.Address:X2}";
                    Log(LogKind.Operation, $"Address 0x{slot.Address:X2} done.");
                }
                catch (OperationCanceledException)
                {
                    slot.Description = "Cancelled";
                    Log(LogKind.Error, "Write cancelled by user.");
                    break;
                }
                catch (Exception ex)
                {
                    slot.Description = "Failed";
                    Log(LogKind.Error, $"Address 0x{slot.Address:X2} failed: {ex.Message}");
                    // Best-effort abort before moving on / stopping.
                    session.Abort();
                    break;
                }
            }

            Log(LogKind.Operation, "Write queue finished.");
        }
        catch (Exception ex)
        {
            Log(LogKind.Error, $"Transfer error: {ex.Message}");
        }
        finally
        {
            IsBusy = false;
            _transferCts?.Dispose();
            _transferCts = null;
            SubmitCommand.NotifyCanExecuteChanged();
        }
    }

    // ---------------------------------------------------------------------
    // Read flow (per-address download)
    // ---------------------------------------------------------------------

    /// <summary>
    /// Reads a whole chip's contents starting at the given address and shows
    /// the assembled buffer in a hex viewer.
    /// </summary>
    private async Task DownloadSlotAsync(AddressSlotViewModel slot)
    {
        if (IsBusy || slot.IsDownloading)
        {
            return;
        }

        var chip = SelectedChip?.Model;
        if (chip is null)
        {
            Log(LogKind.Error, "No chip selected; cannot download.");
            return;
        }

        var length = chip.Bytes;

        slot.IsDownloading = true;
        slot.IsIndeterminate = false;
        slot.SetByteProgress(0, length);
        slot.Description = $"Reading 0x{slot.Address:X2}…";

        _transferCts = new CancellationTokenSource();
        var ct = _transferCts.Token;

        try
        {
            using var session = new CopierProtocolSession(PortName, msg => LogRaw(msg));
            session.Open();

            var service = new CopierTransferService(session, msg => LogRaw(msg));

            var progress = new CopierTransferService.ProgressCallback(
                (done, total) => slot.SetByteProgress(done, total));

            var data = await service.ReadAsync(slot.Address, length, progress, ct)
                .ConfigureAwait(true);

            slot.Description = $"Read 0x{slot.Address:X2} ({data.Length} B)";
            Log(LogKind.Operation,
                $"Address 0x{slot.Address:X2} downloaded ({data.Length} bytes).");

            HexDumpRequested?.Invoke(this,
                new HexDumpRequest(data,
                    $"Read from 0x{slot.Address:X2} — {chip.Name} ({data.Length} bytes)"));
        }
        catch (OperationCanceledException)
        {
            slot.Description = "Cancelled";
            Log(LogKind.Error, "Read cancelled by user.");
        }
        catch (Exception ex)
        {
            slot.Description = "Failed";
            Log(LogKind.Error, $"Download of 0x{slot.Address:X2} failed: {ex.Message}");
        }
        finally
        {
            slot.IsDownloading = false;
            _transferCts?.Dispose();
            _transferCts = null;
        }
    }

    private void Cancel()
    {
        _transferCts?.Cancel();
        Log(LogKind.Operation, "Cancel requested.");
    }

    // ---------------------------------------------------------------------
    // Logging helpers
    // ---------------------------------------------------------------------

    /// <summary>Adds a tagged log entry, marshalled to the UI thread.</summary>
    private void Log(LogKind kind, string message)
    {
        var entry = new LogEntry(kind, message);
        if (Avalonia.Threading.Dispatcher.UIThread.CheckAccess())
        {
            Logs.Add(entry);
        }
        else
        {
            Avalonia.Threading.Dispatcher.UIThread.Post(() => Logs.Add(entry));
        }
    }

    /// <summary>
    /// Adds a pre-formatted transport log line ("[UART] …") coming from the
    /// protocol session / transfer service.
    /// </summary>
    private void LogRaw(string raw)
    {
        var entry = LogEntry.FromString(raw);
        Log(entry.Kind, entry.Message);
    }
}

/// <summary>Request to display a byte buffer in the hex viewer.</summary>
public sealed class HexDumpRequest
{
    public HexDumpRequest(byte[] data, string title)
    {
        Data = data;
        Title = title;
    }

    public byte[] Data { get; }

    public string Title { get; }
}
