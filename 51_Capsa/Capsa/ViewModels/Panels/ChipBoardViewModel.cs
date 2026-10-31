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
                Slots.Add(new ChipSlotViewModel(0x50 + i));
            }
        }

        public ObservableCollection<ChipSlotViewModel> Slots { get; } = new();

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
