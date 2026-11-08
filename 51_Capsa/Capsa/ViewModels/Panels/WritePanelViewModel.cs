using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Threading;
using Capsa.Diagnostics;
using Capsa.Models;
using Capsa.Services;
using Capsa.Services.Protocol;
using Capsa.ViewModels.Editors;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Capsa.ViewModels.Panels
{
    /// <summary>
    /// 写入模式面板：编辑器模式单选框 + 工具栏 + 编辑器 + 进度 &amp; “校验 &amp; 写入”。
    /// </summary>
    public sealed partial class WritePanelViewModel : ViewModelBase
    {
        private readonly DeviceInfoViewModel _device;
        private readonly ChipBoardViewModel _board;
        private readonly IDeviceService _deviceService;
        private readonly ITransferService _transfer;
        private readonly Func<IFilePickerService> _filesAccessor;
        private readonly Func<Task> _refreshDeviceAsync;

        // 进度上屏：后台只写“最新快照”，UI 用定时器按固定节奏取一次。
        // 这样即使界面渲染偶尔繁忙，进度条也不会落后于实际传输。
        private readonly DispatcherTimer _progressTimer;
        private readonly IProgress<TransferProgress> _writeProgress;
        private readonly IProgress<TransferProgress> _verifyProgress;

        /// <summary>写入阶段占进度条的比例，其余留给回读校验。</summary>
        private const double WriteShare = 0.85;

        /// <summary>当前传输速率（写入 / 校验各自重算）。</summary>
        private readonly TransferSpeedometer _speed = new();

        /// <summary>本轮写入的总字节数，用于把整卡百分比换算成“已占用的字节数”。</summary>
        private long _liveWriteTotal;

        /// <summary>
        /// 本次会话是否已有数据落到卡上（写阶段进度 &gt; 0 即置位）。
        /// 格式化被打断时据此决定是否强制发 UG_0 —— 部分覆写已破坏旧数据完整性。
        /// </summary>
        private bool _wroteAnyBytes;

        /// <summary>
        /// 本次会话是否已经把占用记录清过 0：
        /// 会话开始时（固件空闲态）统一清，普通写入与格式化一致 ——
        /// 失败 / 取消的兜底只在会话前清失败（_usedClearedDuringWrite = false）时补一次。
        /// ★ 写会话进行中绝不能再发 UG：半双工主从协议，插队命令会撕裂应答、
        ///   令后续 W_ 包超时（实测 [RX-OK\r\nUU-OK] + 2s timeout）。
        /// </summary>
        private bool _usedClearedDuringWrite;

        /// <summary>当前写入 / 格式化会话的取消源（中断按钮 + 掉线时用）。</summary>
        private CancellationTokenSource? _cts;

        /// <summary>会话是否可中断（写入、写入校验、格式化期间为 true）。</summary>
        [ObservableProperty]
        private bool _isCancelable;

        /// <summary>显示在进度条右侧的速率文本，如 12.4 KB/s。</summary>
        [ObservableProperty]
        private string _speedText = "—";

        // 快照必须是“整体替换”：直接共享结构体会出现撕裂读（进度 41% 但卡片 1%）。
        // 实际写入卡里的文件名（UTF-8 ≤64 B 保留原名，超长退回 dump.<ext>）：附在数据末尾供导出还原
        private string _payloadFileName = string.Empty;

        private ProgressSnapshot? _latest;
        private int _hasPending;

        public WritePanelViewModel(
            DeviceInfoViewModel device,
            ChipBoardViewModel board,
            IDeviceService deviceService,
            ITransferService transfer,
            Func<IFilePickerService> filesAccessor,
            Func<Task> refreshDeviceAsync)
        {
            _device = device;
            _board = board;
            _deviceService = deviceService;
            _transfer = transfer;
            _filesAccessor = filesAccessor;
            _refreshDeviceAsync = refreshDeviceAsync;

            Session = new EditorSessionViewModel
            {
                EmptyHint = "Import any file — its extension is stored at the end of the payload",
            };
            Session.BytesReplaced += OnSessionBytesReplaced;
            _device.PropertyChanged += OnDevicePropertyChanged;

            // 芯片卡片 WP 徽章点击 → 切换写保护（SET wp_mask → PCF8574 立即生效）
            _board.WpToggleRequested += address => _ = ToggleWriteProtectAsync(address);

            _writeProgress = new SnapshotProgress(this, phase: 0);
            _verifyProgress = new SnapshotProgress(this, phase: 1);

            _progressTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(200) };
            _progressTimer.Tick += (_, _) => FlushProgress();
        }

        public EditorSessionViewModel Session { get; }

        [ObservableProperty]
        private double _progressValue;

        [ObservableProperty]
        private string _progressText = "0%";

        [ObservableProperty]
        private string _statusText = "Waiting for payload";

        [ObservableProperty]
        private string _contentSummary = "Buffer empty";

        [ObservableProperty]
        private bool _isBusy;

        /// <summary>设备已初始化且当前没有任务在跑。</summary>
        public bool IsReady => _device.IsInitialized;

        /// <summary>没有任务在跑（格式化不要求卡已初始化，用它控制按钮可用性）。</summary>
        public bool IsNotBusy => !IsBusy;

        partial void OnIsBusyChanged(bool value)
        {
            OnPropertyChanged(nameof(IsNotBusy));
            OnPropertyChanged(nameof(IsReady));
        }

        /// <summary>打开“芯片健康检查”窗口，逐颗芯片做 0xFF / 0x00 读写校验。</summary>
        [RelayCommand]
        private async Task CheckHealthAsync()
        {
            if (!IsReady || IsBusy)
            {
                return;
            }

            var port = AppServices.Serial.ConnectedPortName;
            if (string.IsNullOrWhiteSpace(port))
            {
                StatusText = "Connect a serial port before checking chip health";
                return;
            }

            var testRan = await AppServices.HealthCheckDialog.ShowAsync(port).ConfigureAwait(true);
            if (!testRan)
            {
                return;
            }

            IsBusy = true;
            StatusText = "Applying health check results…";
            try
            {
                await _deviceService.CheckHealthAsync().ConfigureAwait(true);
                await _refreshDeviceAsync().ConfigureAwait(true);
                StatusText = $"Health check complete · {_device.HealthScoreText} ({_device.HealthLevelText})";
            }
            catch (Exception ex)
            {
                Capsa.Diagnostics.ErrorLog.Add("Health check", ex.Message, ex);
                StatusText = $"Health check failed: {ex.Message}";
            }
            finally
            {
                IsBusy = false;
            }
        }

        /// <summary>打开“初始化存储卡”窗口，完成后把结果应用到主界面。</summary>
        [RelayCommand]
        private async Task ReinitializeAsync()
        {
            if (IsBusy)
            {
                return;
            }

            var port = AppServices.Serial.ConnectedPortName;
            if (string.IsNullOrWhiteSpace(port))
            {
                StatusText = "Connect a serial port before initializing the card";
                return;
            }

            var result = await AppServices.InitializeDialog.ShowAsync(port).ConfigureAwait(true);
            if (result is null)
            {
                StatusText = "Initialization canceled";
                return;
            }

            IsBusy = true;
            StatusText = "Applying initialization result…";
            try
            {
                await _deviceService.ReinitializeAsync().ConfigureAwait(true);
                await _refreshDeviceAsync().ConfigureAwait(true);

                // 用初始化窗口的真实结果覆盖左侧属性面板（名称 / UID / 容量 / 健康分）。
                _device.Apply(BuildDeviceInfo(result));

                StatusText = $"Card {result.DeviceName} initialized · UID {result.Uid}";
            }
            catch (Exception ex)
            {
                Capsa.Diagnostics.ErrorLog.Add("Apply init result", ex.Message, ex);
                StatusText = $"Failed to apply init result: {ex.Message}";
            }
            finally
            {
                IsBusy = false;
            }
        }

        /// <summary>
        /// 点击芯片卡片的 WP 徽章：切换该片写保护。
        /// 固件经 PCF8574 立即拉起 / 释放 WP；被保护的芯片剔出有效容量，
        /// 后续写入 / 读取自动跳过它，左侧面板显示黄色不可用段。
        /// </summary>
        private async Task ToggleWriteProtectAsync(int address)
        {
            if (IsBusy)
            {
                return;
            }

            var slot = _board.Find(address);
            var protect = slot?.WriteProtected != true;

            IsBusy = true;
            StatusText = $"{(protect ? "Protecting" : "Unprotecting")} 0x{address:X2}…";
            try
            {
                var ok = await _deviceService
                    .SetWriteProtectBitAsync(address, protect)
                    .ConfigureAwait(true);

                if (!ok)
                {
                    StatusText = $"Write-protect change rejected at 0x{address:X2}";
                    return;
                }

                // 本地立即反映（随后 _refreshDeviceAsync 用真实掩码覆盖）
                if (slot is not null)
                {
                    slot.WriteProtected = protect;
                    slot.WriteEnableLatch = !protect;
                }

                await _refreshDeviceAsync().ConfigureAwait(true);
                StatusText = protect
                    ? $"0x{address:X2} is now write-protected (capacity excluded)"
                    : $"0x{address:X2} is writable again";
            }
            catch (Exception ex)
            {
                Capsa.Diagnostics.ErrorLog.Add("Write protect", ex.Message, ex);
                StatusText = $"Write-protect change failed: {ex.Message}";
            }
            finally
            {
                IsBusy = false;
            }
        }

        /// <summary>
        /// 格式化：把每一片可写芯片的**完整地址空间**写成 0x00，走与普通写入完全相同的
        /// 会话（ROUTER → ADDR 0..末地址 → INT_W → W_* → TX-OVER），因此底部进度条、
        /// 速率、Stop 按钮、左侧卡片进度条全都一致。
        ///
        /// 收尾三步（缺一不可，任一步失败都要在界面上说出来）：
        ///   ① SET（9 字段）—— 属性完整、init_flag = 1、时间戳刷新；
        ///   ② UG_0        —— 已用容量归零（"格式化后还有容量信息"就是这一步没成功）；
        ///   ③ DI 全 0     —— 数据哈希失效，累计写入次数保留（寿命指标）。
        ///
        /// 卡未初始化时先发一次 SET 把属性建立起来：固件在 INT_W 前会校验
        /// Meta_InfoValid()，没有属性的卡根本开不了写入会话。
        /// </summary>
        [RelayCommand]
        private async Task FormatAsync()
        {
            if (IsBusy)
            {
                StatusText = "Another transfer is running — wait for it or press Stop";
                return;
            }

            if (string.IsNullOrWhiteSpace(AppServices.Serial.ConnectedPortName))
            {
                StatusText = "Connect a serial port before formatting";
                return;
            }

            // ① 未初始化：先用 SET（9 字段）建立属性，否则固件拒绝开写入会话。
            //    这一步必须排在"取几何"之前 —— 未初始化的卡没有容量可读，
            //    先判 geometry 会永远卡在 "Card geometry unknown"。
            if (!_device.IsInitialized)
            {
                StatusText = "Card has no attributes — configuring via SET (9 fields)…";
                var configured = await _deviceService
                    .ConfigureCardAsync(BuildFormatConfig(), CancellationToken.None)
                    .ConfigureAwait(true);

                if (!configured)
                {
                    StatusText = "Format aborted · SET (9 fields) was rejected by the card";
                    return;
                }

                await _refreshDeviceAsync().ConfigureAwait(true);
            }

            // ② 取几何（此刻卡一定已初始化）
            var chipBytes = _device.CapacityBytes;
            var mask = _device.WriteProtectMask;
            var writable = 0;
            for (var i = 0; i < 8; i++)
            {
                if ((mask & (1 << i)) == 0)
                {
                    writable++;
                }
            }

            if (chipBytes <= 0)
            {
                StatusText = "Card geometry unknown — re-read the card before formatting";
                return;
            }
            if (writable == 0)
            {
                StatusText = "All 8 chips are write-protected — release WP before formatting";
                return;
            }

            // ③ 目标范围 = 每个可写片的全部地址 0x0000 .. chipBytes-1（一片不落）
            var total = writable * chipBytes;
            FormatRangeText =
                $"{writable} writable chip(s) × {ByteSize.Format(chipBytes)} = " +
                $"{ByteSize.Format(total)} · 0x0000–0x{chipBytes - 1:X4} per chip";

            // 格式化 = 把一个"全 0x00 文件"写进阵列：编辑器当场装满 0x00，
            // 用户在 hex 视图里看到的就是本次要写的内容，语义与导入文件完全一致
            var zeros = new byte[total];
            Session.LoadBytes(zeros, EditorChangeSource.Import, $"format · {writable}×{ByteSize.Format(chipBytes)} of 0x00");

            await RunWriteSessionAsync(zeros, formatMode: true).ConfigureAwait(true);
        }

        /// <summary>格式化目标范围（界面显示，让人一眼看清写了多少、覆盖到哪）。</summary>
        [ObservableProperty]
        private string _formatRangeText = "—";

        /// <summary>
        /// 快速格式化：**只动记录、不动数据** —— 发 `UG_0` 把"已使用容量"直接归零、
        /// DI 清哈希（累计写入次数保留），数据字节原样留在芯片上。
        /// 与完整格式化（逐片写 0x00）的取舍：
        ///   · 秒级完成，无总线往返刷写；
        ///   · 代价：芯片里的旧数据字节仍在，只是卡"不记得有文件"了 ——
        ///     之后任何一次普通写入都会从头覆写，读流程也只读到 used 为止。
        /// 卡未初始化时固件会拒绝 UG（Meta_IsInitialized 前置），所以必须先初始化。
        /// </summary>
        [RelayCommand]
        private async Task QuickFormatAsync()
        {
            if (IsBusy)
            {
                StatusText = "Another transfer is running — wait for it or press Stop";
                return;
            }

            if (string.IsNullOrWhiteSpace(AppServices.Serial.ConnectedPortName))
            {
                StatusText = "Connect a serial port before quick-formatting";
                return;
            }

            if (!_device.IsInitialized)
            {
                StatusText = "Card is not initialized — quick format needs card attributes (use Format card)";
                return;
            }

            IsBusy = true;
            try
            {
                StatusText = "Quick format · resetting used-space record (UG 0)…";

                // 收尾命令与取消源无关：这里没有会话，直接发
                var usedOk = await _deviceService
                    .SetUsedSpaceAsync(0, CancellationToken.None)
                    .ConfigureAwait(true);

                var hashOk = await _deviceService
                    .SetDataInfoAsync(new string('0', 16), _device.WriteCount, CancellationToken.None)
                    .ConfigureAwait(true);

                // 本地立即清零：卡片与左侧占用当场归零，再用真实 UGS 覆盖
                _board.BeginWrite();
                _device.SetLiveUsedBytes(0);
                await _refreshDeviceAsync().ConfigureAwait(true);

                if (usedOk && hashOk)
                {
                    StatusText = "Quick format done · used space → 0 · hash cleared · data bytes untouched";
                }
                else
                {
                    var failure = !usedOk ? "UG 0 rejected" : "DI rejected";
                    Capsa.Diagnostics.ErrorLog.Add("Quick format", failure);
                    StatusText = $"Quick format failed · {failure}";
                    await AppServices.Messages
                        .ShowErrorAsync("Quick format failed",
                            failure + "\r\n\r\n已用容量记录没有更新成功，详情可直接复制排查。")
                        .ConfigureAwait(true);
                }
            }
            catch (Exception ex)
            {
                Capsa.Diagnostics.ErrorLog.Add("Quick format", ex.Message, ex);
                StatusText = $"Quick format failed: {ex.Message}";
            }
            finally
            {
                IsBusy = false;
            }
        }

        /// <summary>
        /// 组装 SET 的 9 个字段。已初始化时沿用卡上的名称 / UID / 型号，
        /// 只把修改时间刷成当前时间；未初始化时才用面板默认值补齐。
        /// </summary>
        private CapsaInfo BuildFormatConfig()
        {
            var chipBytes = _device.CapacityBytes > 0 ? _device.CapacityBytes : 8192;
            var modelKbit = ParseModelKbit(_device.ChipModel, chipBytes);
            var name = _device.DeviceName;
            if (string.IsNullOrWhiteSpace(name) || name == "—")
            {
                name = "CAPSA";
            }

            var uid = SanitizeUid(_device.Uid);
            var now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();

            return new CapsaInfo
            {
                Name = name,
                WriteProtectMask = _device.WriteProtectMask,
                HealthMask = 0xFF,
                CapacityKb = chipBytes * 8 / 1024,   // 仅占位：固件 SET 后按 WP 重算为有效容量
                ModelKbit = modelKbit,
                ModifiedAt = now,
                ReadAt = now,
                Medium = 0,
                Uid = uid,
            };
        }

        /// <summary>
        /// "24C64" / "AT24C256" → Kbit 数值；解析不出时按片容量反推。
        /// ★ 不能直接抽全部数字："24C64" 会变成 2464 —— 必须先切掉前缀 "24C"。
        /// </summary>
        private static int ParseModelKbit(string chipModel, int chipBytes)
        {
            var text = (chipModel ?? string.Empty).ToUpperInvariant();
            var index = text.IndexOf("24C", StringComparison.Ordinal);
            var tail = index >= 0 ? text[(index + 3)..] : text;
            var digits = new string(tail.Where(char.IsDigit).ToArray());

            if (int.TryParse(digits, out var kbit) && kbit is >= 1 and <= 4096)
            {
                return kbit;
            }

            return chipBytes * 8 / 1024;   // 8192 B → 64 Kbit
        }

        /// <summary>UID 必须是 12 位十六进制（固件 parse_uid 只吃这个格式）。</summary>
        private static string SanitizeUid(string uid)
        {
            var hex = new string((uid ?? string.Empty)
                .Where(Uri.IsHexDigit)
                .Select(char.ToUpperInvariant)
                .Take(12)
                .ToArray());

            if (hex.Length == 12)
            {
                return hex;
            }

            // 缺失时给一个稳定但唯一的 12 位值，避免写成全 0 被当成"无记录"
            var seed = (uint)DateTimeOffset.UtcNow.ToUnixTimeSeconds();
            return seed.ToString("X8") + Random.Shared.Next(0, 0x10000).ToString("X4");
        }

        [RelayCommand]
        private async Task ImportFileAsync()
        {
            if (IsBusy)
            {
                return;
            }

            var picked = await _filesAccessor().PickBinaryFileAsync().ConfigureAwait(true);
            if (picked is null)
            {
                return;
            }

            var (name, data) = picked.Value;
            ApplyImportedFile(name, data);
        }

        /// <summary>
        /// 装载一个导入文件：记住扩展名（写入时附在数据末尾），并把内容放进缓冲区。
        /// 独立出来便于走查 / 测试直接调用，无需打开文件对话框。
        /// </summary>
        internal void ApplyImportedFile(string name, byte[] data)
        {
            _payloadFileName = FilePayloadCodec.BuildStoredName(name);
            Session.LoadBytes(data, EditorChangeSource.Import, name);

            StatusText = _payloadFileName.Length > 0
                ? $"Imported {name} ({ByteSize.Format(data.Length)}) · stored name \"{_payloadFileName}\""
                : $"Imported {name} ({ByteSize.Format(data.Length)})";
        }

        [RelayCommand]
        private async Task WriteVerifyAsync()
        {
            if (!IsReady || IsBusy)
            {
                return;
            }

            var content = Session.Snapshot();
            if (content.Length == 0)
            {
                StatusText = "Buffer is empty — nothing to write";
                return;
            }

            // 把原文件扩展名附在数据末尾：读取时据此还原文件格式
            var payload = FilePayloadCodec.Wrap(content, _payloadFileName);

            var capacity = _device.ArrayEffectiveBytes;
            if (capacity > 0 && payload.Length > capacity)
            {
                StatusText =
                    $"File too large · {ByteSize.Format(content.Length)} + trailer exceeds " +
                    $"{ByteSize.Format(capacity)} effective capacity";
                return;
            }

            await RunWriteSessionAsync(payload, formatMode: false).ConfigureAwait(true);
        }

        [RelayCommand]
        private void CancelWrite()
        {
            if (!IsBusy)
            {
                return;
            }

            // 先让下位机退出会话（否则它会白等 RX-OK 2 秒），再取消主机侧循环
            _transfer.RequestAbort();
            _cts?.Cancel();
            StatusText = "Cancelling…";
        }

        /// <summary>
        /// 写入会话主流程（普通写入与格式化共用）：
        /// 写入 → 回读校验 → 上报占用 / 哈希。
        ///
        /// ★ 占用记录（UG）与数据完整性的关系 —— 任何覆写一旦发生，
        ///   "已使用 N 字节"的旧记录就是谎言（用户会误以为卡里还有完整文件）：
        ///   · 格式化：**会话一开始就发 UG_0**（意图就是清，等失败再补是错的
        ///     —— 上位机崩溃 / 断线时补偿代码根本没机会跑）；
        ///   · 普通写入：**首个数据包落到卡上后立即 UG_0**（首包未发前不算破坏，
        ///     发出去那一刻旧记录就失效了），成功后正常上报 payload.Length；
        ///   · 失败 / 取消：若首包已写而还没来得及清 → 兜底补清。
        /// </summary>
        private async Task RunWriteSessionAsync(byte[] payload, bool formatMode)
        {
            IsBusy = true;
            IsCancelable = true;
            _wroteAnyBytes = false;
            _usedClearedDuringWrite = false;
            _cts = new CancellationTokenSource();
            var token = _cts.Token;

            ProgressValue = 0;
            ProgressText = "0%";
            StatusText = formatMode
                ? $"Formatting · {FormatRangeText}"
                : "Preparing to write…";

            // 整卡进度归零：上一轮写过的卡片不能带着旧填充进场
            _board.BeginWrite();

            // 左侧占用同步归零：整卡覆写意味着旧数据先被清掉，再随写入增长
            _liveWriteTotal = payload.Length;
            _device.SetLiveUsedBytes(0);

            // ★ 会话开始（固件还在空闲态）就把占用记录清 0 —— 普通写入与格式化统一。
            //   ★★ 这里绝对不能用后台任务推迟到"首包之后"：写会话进行中固件在
            //   流式处理 W_ 包，插队的 UG_0 会与数据流竞争 —— 实测应答撕裂成
            //   [RX-OK\r\nUU-OK]、后续 W_ 包 2 s 超时。半双工主从协议里
            //   任何命令只能在自己会话之外发。CancellationToken.None：
            //   这条记录必须发出去，哪怕随后的写入会话立刻被取消。
            _usedClearedDuringWrite = true;
            try
            {
                await _deviceService
                    .SetUsedSpaceAsync(0, CancellationToken.None)
                    .ConfigureAwait(true);
            }
            catch (Exception ex)
            {
                ErrorLog.Add(
                    formatMode ? "Format" : "Write",
                    "Failed to clear used space before session — card may still show stale usage",
                    ex);
            }

            _speed.Reset();
            _speed.Start(payload.Length);
            SpeedText = "—";

            StartProgressPump();

            try
            {
                var result = await _transfer
                    .WriteAsync(payload, _writeProgress, token)
                    .ConfigureAwait(true);

                if (!result.Success)
                {
                    ErrorLog.Add(
                        formatMode ? "Format" : "Write",
                        result.Message,
                        null,
                        $"payload {payload.Length:N0} B · wrote-any={_wroteAnyBytes}");

                    if (_wroteAnyBytes && !_usedClearedDuringWrite)
                    {
                        await EnsureUsedClearedAsync("write failed after data was written").ConfigureAwait(true);
                        StatusText = $"{(formatMode ? "Format" : "Write")} failed · {result.Message} · used space reset to 0 (data already overwritten)";
                    }
                    else
                    {
                        StatusText = result.Message;
                    }

                    await _refreshDeviceAsync().ConfigureAwait(true);

                    // ★ 失败必须弹可复制详情：状态栏只有一行，真正的原因
                    //   （最后几条协议交互）在详情窗口里。
                    await AppServices.Messages
                        .ShowErrorAsync(formatMode ? "Format failed" : "Write failed", result.Message)
                        .ConfigureAwait(true);
                    return;
                }

                // 写完立即回读校验，确保与缓冲区一致（速率按本阶段重新计时）
                StatusText = "Verifying written data…";
                _speed.Start(payload.Length);
                var verify = await _transfer
                    .VerifyAsync(payload, _verifyProgress, token)
                    .ConfigureAwait(true);

                if (!verify.Success)
                {
                    ErrorLog.Add(
                        formatMode ? "Format verify" : "Write verify",
                        verify.Message,
                        null,
                        $"payload {payload.Length:N0} B");

                    if (_wroteAnyBytes && !_usedClearedDuringWrite)
                    {
                        await EnsureUsedClearedAsync("verify failed after data was written").ConfigureAwait(true);
                        StatusText = $"{(formatMode ? "Format" : "Write")} failed · {verify.Message} · used space reset to 0";
                    }
                    else
                    {
                        StatusText = verify.Message;
                    }

                    await _refreshDeviceAsync().ConfigureAwait(true);

                    await AppServices.Messages
                        .ShowErrorAsync(formatMode ? "Format verify failed" : "Write verify failed", verify.Message)
                        .ConfigureAwait(true);
                    return;
                }

                if (formatMode)
                {
                    // 格式化收尾 —— 三步都必须成功，否则"格式化完还显示旧容量"。
                    // 顺序：SET（属性/时间戳）→ UG_0（占用归零）→ DI（哈希失效，次数保留）。
                    StatusText = "Recording format result (SET → UG 0 → DI)…";

                    var configOk = await _deviceService
                        .ConfigureCardAsync(BuildFormatConfig(), token)
                        .ConfigureAwait(true);

                    var usedOk = await _deviceService
                        .SetUsedSpaceAsync(0, token)
                        .ConfigureAwait(true);

                    // 全 0 哈希 = "无记录"占位；写入次数原样保留（寿命指标）
                    var hashOk = await _deviceService
                        .SetDataInfoAsync(new string('0', 16), _device.WriteCount, token)
                        .ConfigureAwait(true);

                    // 本地立即归零：不等刷新的往返，卡片与左侧占用当场清空
                    _board.BeginWrite();
                    _device.SetLiveUsedBytes(0);

                    ProgressValue = 100;
                    ProgressText = "100%";
                    await _refreshDeviceAsync().ConfigureAwait(true);

                    var failures = new List<string>();
                    if (!configOk) failures.Add("SET rejected");
                    if (!usedOk) failures.Add("UG 0 rejected");
                    if (!hashOk) failures.Add("DI rejected");

                    StatusText = failures.Count == 0
                        ? $"Formatted · {ByteSize.Format(payload.Length)} of 0x00 written & verified · used 0 · hash cleared"
                        : $"Formatted with warnings · {ByteSize.Format(payload.Length)} written · {string.Join(", ", failures)}";

                    if (failures.Count > 0)
                    {
                        ErrorLog.Add(
                            "Format",
                            string.Join(", ", failures),
                            null,
                            $"payload {payload.Length:N0} B written & verified");

                        await AppServices.Messages
                            .ShowErrorAsync("Format finished with warnings",
                                string.Join(Environment.NewLine, failures) +
                                "\r\n\r\n数据已写入并校验，但收尾记录（SET / UG 0 / DI）没有全部成功。" +
                                "\r\n本窗口的内容可以直接复制，用于排查固件应答。")
                            .ConfigureAwait(true);
                    }
                }
                else
                {
                    // 校验通过：把实际占用上报到 24C02（UG），再用 UGS 回填界面
                    StatusText = "Recording used space & data hash…";
                    var recorded = await _deviceService
                        .SetUsedSpaceAsync(payload.Length, token)
                        .ConfigureAwait(true);

                    // 整卡哈希由上位机计算、写入次数由上位机 +1，一起通过 DI 存进 24C02
                    var hash = DataIntegrity.ComputeHashHex(payload);
                    var writeCount = _device.WriteCount + 1;
                    var integrityOk = await _deviceService
                        .SetDataInfoAsync(hash, writeCount, token)
                        .ConfigureAwait(true);

                    ProgressValue = 100;
                    ProgressText = "100%";
                    await _refreshDeviceAsync().ConfigureAwait(true);

                    StatusText =
                        $"Write & verify OK · {ByteSize.Format(payload.Length)} · " +
                        (integrityOk
                            ? $"hash {hash[..8]}… · write #{writeCount}"
                            : "hash not recorded (DI failed)") +
                        (recorded ? string.Empty : " · used space not recorded");
                }
            }
            catch (OperationCanceledException)
            {
                // 打断（Stop / 掉线）≠ 没写过：只要已有数据落卡且还没清过记录，
                // 就必须补一次 UG_0。收尾命令用 CancellationToken.None ——
                // token 已经取消，但记录必须发出。
                if (_wroteAnyBytes && !_usedClearedDuringWrite)
                {
                    await EnsureUsedClearedAsync("write cancelled after data was written").ConfigureAwait(true);
                    StatusText = $"{(formatMode ? "Format" : "Write")} cancelled · used space reset to 0 (data already overwritten)";
                }
                else
                {
                    StatusText = "Cancelled by user";
                }

                ErrorLog.Add(
                    formatMode ? "Format" : "Write",
                    "Cancelled by user",
                    null,
                    $"payload {payload.Length:N0} B · wrote-any={_wroteAnyBytes}");

                await _refreshDeviceAsync().ConfigureAwait(true);
            }
            catch (Exception ex)
            {
                ErrorLog.Add(
                    formatMode ? "Format" : "Write",
                    ex.Message,
                    ex,
                    $"payload {payload.Length:N0} B · wrote-any={_wroteAnyBytes}");

                if (_wroteAnyBytes && !_usedClearedDuringWrite)
                {
                    await EnsureUsedClearedAsync($"exception after data was written · {ex.Message}").ConfigureAwait(true);
                    StatusText = $"{(formatMode ? "Format" : "Write")} failed: {ex.Message} · used space reset to 0";
                }
                else
                {
                    StatusText = (formatMode ? "Format failed: " : "Write failed: ") + ex.Message;
                }

                // 失败时用设备真实状态覆盖实时进度，避免卡片停在半格
                await _refreshDeviceAsync().ConfigureAwait(true);
            }
            finally
            {
                // 收尾时丢弃排队中的进度快照，避免“Verifying…”覆盖最终结论
                StopProgressPump();
                _speed.Finish();
                SpeedText = _speed.AverageText;   // 完成后显示平均速率
                Interlocked.Exchange(ref _hasPending, 0);
                Volatile.Write(ref _latest, null);
                IsCancelable = false;
                _cts?.Dispose();
                _cts = null;
                IsBusy = false;
            }
        }

        /// <summary>
        /// 兜底收尾：UG_0（占用归零）+ DI 清哈希（次数保留）。
        /// 适用于"首包已写、但记录还没来得及清"就失败 / 被打断的场景 ——
        /// 部分覆写已经破坏了旧数据的完整性，记录必须跟着归零。
        /// 格式化因为在会话开始时就清过（<see cref="_usedClearedDuringWrite"/>），
        /// 正常不会再走到这里。
        /// </summary>
        private async Task EnsureUsedClearedAsync(string reason)
        {
            try
            {
                // ★ CancellationToken.None：会话取消后 token 已失效，但 UG_0 必须发出去
                await _deviceService
                    .SetUsedSpaceAsync(0, CancellationToken.None)
                    .ConfigureAwait(true);

                await _deviceService
                    .SetDataInfoAsync(new string('0', 16), _device.WriteCount, CancellationToken.None)
                    .ConfigureAwait(true);

                ErrorLog.Add(
                    "Write",
                    $"Used space reset · {reason} (partial overwrite destroyed old integrity)");
            }
            catch (Exception ex)
            {
                ErrorLog.Add(
                    "Write",
                    "Failed to reset used space — card still shows stale usage",
                    ex,
                    reason);
            }
            finally
            {
                _board.BeginWrite();
                _device.SetLiveUsedBytes(0);
            }
        }

        // ---- 进度上屏 ----

        private void StartProgressPump()
        {
            Interlocked.Exchange(ref _hasPending, 0);
            _progressTimer.Start();
        }

        private void StopProgressPump() => _progressTimer.Stop();

        /// <summary>把后台线程记录的最新快照搬到界面（定时器 / 收尾时调用）。</summary>
        private void FlushProgress()
        {
            if (Interlocked.Exchange(ref _hasPending, 0) == 0)
            {
                return;
            }

            var snapshot = Volatile.Read(ref _latest);
            if (snapshot is null)
            {
                return;
            }

            var p = snapshot.Value;

            SpeedText = _speed.Sample(p.Percent);

            if (snapshot.Phase == 0)
            {
                ProgressValue = p.Percent * WriteShare;
                ProgressText = $"{ProgressValue:0}%";
                StatusText = p.Message;

                if (p.Percent > 0)
                {
                    _wroteAnyBytes = true;   // 已有数据落卡：失败 / 取消的兜底据此补清
                    // 占用记录已在会话开始时清过（_usedClearedDuringWrite = true）——
                    // ★ 千万不要在写会话进行中发 UG_0：它会插进 W_ 数据流，
                    //   实测造成应答撕裂与后续包超时（半双工主从协议，
                    //   命令只能在自己会话之外发）。
                }

                // 8 片组成阵列：按地址顺序写入，比当前片小的都已经写满；
                // 当前这一片按片内百分比驱动卡片进度条。
                if (p.ChipAddress != 0)
                {
                    for (var addr = 0x50; addr < p.ChipAddress; addr++)
                    {
                        _board.MarkFull(addr);
                    }

                    _board.ReportProgress(p.ChipAddress, p.ChipBytes);
                }

                // 左侧占用跟着整卡百分比一起长（整卡覆写，已写多少就占多少）
                _device.SetLiveUsedBytes((long)(_liveWriteTotal * p.Percent / 100d));
            }
            else
            {
                ProgressValue = (WriteShare + p.Percent / 100d * (1 - WriteShare)) * 100;
                ProgressText = $"{ProgressValue:0}%";
                StatusText = p.Message;

                // 进入回读校验时数据已经写满，占用保持在最大值
                _device.SetLiveUsedBytes(_liveWriteTotal);
            }
        }

        /// <summary>后台线程只保存最新值，绝不在传输线程里碰 UI。</summary>
        private sealed class SnapshotProgress : IProgress<TransferProgress>
        {
            private readonly WritePanelViewModel _owner;
            private readonly int _phase;

            public SnapshotProgress(WritePanelViewModel owner, int phase)
            {
                _owner = owner;
                _phase = phase;
            }

            public void Report(TransferProgress value)
            {
                // 引用赋值是原子的，避免 UI 读到“半新半旧”的结构体
                Volatile.Write(ref _owner._latest, new ProgressSnapshot(value, _phase));
                Interlocked.Exchange(ref _owner._hasPending, 1);
            }
        }

        /// <summary>一次进度快照 + 所处阶段。</summary>
        private sealed record ProgressSnapshot(TransferProgress Value, int Phase);

        private void OnSessionBytesReplaced(object? sender, EditorChangeSource source)
        {
            ContentSummary = Session.ByteLength == 0
                ? "Buffer empty"
                : $"{Session.SizeText} · {Session.SourceName}";

            if (Session.ByteLength > 0 && source is EditorChangeSource.Import)
            {
                ProgressValue = 0;
                ProgressText = "0%";
            }
        }

        /// <summary>把初始化结果转换为左侧面板使用的设备信息。</summary>
        private DeviceInfo BuildDeviceInfo(CardInitResult result) => new()
        {
            DeviceName = result.DeviceName,
            Uid = result.Uid,
            ChipModel = result.Chip.Name,
            ChipType = result.Medium.DisplayName,

            // ChipCapacity = 单片容量；阵列容量单独给，占用百分比靠它计算
            CapacityBytes = (int)Math.Min(int.MaxValue, result.Chip.Bytes),
            ArrayCapacityBytes = (int)Math.Min(int.MaxValue, result.ArrayCapacityBytes),

            UsedBytes = 0,
            LastWriteTime = result.InitializedAt,
            LastAccessTime = result.InitializedAt,
            HealthScore = (int)Math.Round(result.HealthyChips * 100d / 8d),

            // 哈希与累计写入次数沿用刚刷新到的真实值：
            // 初始化会让旧哈希失效（已由刷新读到全 0 → 无记录），
            // 但写入次数是寿命指标，绝不能被初始化结果覆盖成 0。
            DataHash = _device.DataHash,
            WriteCount = _device.WriteCount,

            // WP 掩码沿用刚刷新到的真实值：被保护的芯片保持保护状态，
            // 否则初始化面板一覆盖，卡片和黄色段就会与实际 PCF8574 输出不符。
            WriteProtectMask = _device.WriteProtectMask,
        };

        private void OnDevicePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName is nameof(DeviceInfoViewModel.IsInitialized))
            {
                OnPropertyChanged(nameof(IsReady));
            }
        }
    }
}
