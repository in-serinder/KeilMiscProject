using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using Capsa.Models;

namespace Capsa.ViewModels.Panels
{
    /// <summary>8 个存储单元（0x50 - 0x57）的卡片集合。</summary>
    public sealed class ChipBoardViewModel : ViewModelBase
    {
        public ChipBoardViewModel()
        {
            for (var i = 0; i < 8; i++)
            {
                var slot = new ChipSlotViewModel(0x50 + i);
                slot.WpToggleRequested += s => WpToggleRequested?.Invoke(s.Address);
                Slots.Add(slot);
            }
        }

        public ObservableCollection<ChipSlotViewModel> Slots { get; } = new();

        /// <summary>某片的 WP 徽章被点击（参数 = 7 位地址）。上层决定是否执行切换。</summary>
        public event Action<int>? WpToggleRequested;

        public void Load(IReadOnlyList<SlotStatus> slots)
        {
            for (var i = 0; i < Slots.Count; i++)
            {
                Slots[i].Apply(i < slots.Count ? slots[i] : null);
            }
        }

        /// <summary>按 7 位地址取卡片（0x50 - 0x57）。</summary>
        public ChipSlotViewModel? Find(int address)
        {
            var index = address - 0x50;
            return index >= 0 && index < Slots.Count ? Slots[index] : null;
        }

        /// <summary>写入 / 读取过程中按“已写字节数”驱动单片卡片。</summary>
        public void ReportProgress(int address, int writtenBytes)
        {
            Find(address)?.SetLiveBytes(writtenBytes);
        }

        /// <summary>进入读取模式：显示绿色覆盖层并清零。</summary>
        public void BeginRead()
        {
            foreach (var slot in Slots)
            {
                slot.SetReadBytes(0);
                slot.IsReadActive = slot.IsPresent;
            }
        }

        /// <summary>读取过程中按已读字节数驱动某片的绿色覆盖层。</summary>
        public void ReportReadProgress(int address, int readBytes)
        {
            Find(address)?.SetReadBytes(readBytes);
        }

        /// <summary>
        /// 读取完成后按整卡读数铺满各片：chip(i) = clamp(total - i×容量, 0, 容量)。
        /// </summary>
        public void ApplyReadCoverage(long totalBytes)
        {
            for (var i = 0; i < Slots.Count; i++)
            {
                var slot = Slots[i];
                if (!slot.IsPresent || slot.CapacityBytes <= 0)
                {
                    continue;
                }

                var remaining = totalBytes - (long)i * slot.CapacityBytes;
                slot.SetReadBytes((int)Math.Clamp(remaining, 0, slot.CapacityBytes));
            }
        }

        /// <summary>退出读取模式：隐藏绿色覆盖层，卡片只保留蓝色占用。</summary>
        public void ClearReadOverlay()
        {
            foreach (var slot in Slots)
            {
                slot.IsReadActive = false;
            }
        }

        /// <summary>
        /// 开始一轮写入：先清空所有卡片的实时占用，让进度条从 0 起步。
        /// 否则上一轮残留的蓝色填充会让人误以为“还没写就已经有数据”。
        /// </summary>
        public void BeginWrite()
        {
            foreach (var slot in Slots)
            {
                slot.IsReadActive = false;
                slot.SetLiveBytes(0);
            }
        }

        /// <summary>把某片标记为已写满（顺序写入时跳过了它的收尾回调）。</summary>
        public void MarkFull(int address)
        {
            var slot = Find(address);
            if (slot is not null)
            {
                slot.SetLiveBytes(slot.CapacityBytes);
            }
        }

        public void Reset()
        {
            foreach (var slot in Slots)
            {
                slot.Reset();
            }
        }
    }
}
