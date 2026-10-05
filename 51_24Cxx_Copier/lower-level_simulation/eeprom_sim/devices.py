"""24CXX 设备表与 EEPROM 内存模型。

每个 I2C 设备对应一个独立 bin 文件作为模拟永久存储。
"""
from __future__ import annotations

import os
import threading
import time
from dataclasses import dataclass


@dataclass(frozen=True)
class DeviceSpec:
    """一片 24CXX 的静态规格。"""

    name: str            # 型号，如 24C01
    i2c_addr: int        # 7 位 I2C 芯片地址，如 0x50
    size: int            # 容量（字节数）
    max_addr: int        # 最大地址，如 0x7F
    addr_bytes: int      # 模拟 I2C 时使用的地址字节数（1 或 2）
    bin_name: str        # 独立 bin 文件名

    @property
    def write_addr(self) -> int:
        """实际 I2C 写地址 = (chip << 1) | 0。"""
        return (self.i2c_addr << 1) | 0

    @property
    def read_addr(self) -> int:
        """实际 I2C 读地址 = (chip << 1) | 1。"""
        return (self.i2c_addr << 1) | 1


# 总线上模拟存在的 8 个设备：0x50 ~ 0x57
DEVICE_TABLE: dict[int, DeviceSpec] = {
    0x50: DeviceSpec("24C01", 0x50, 128, 0x7F, 1,
                     "eeprom_0x50_24C01.bin"),
    0x51: DeviceSpec("24C08", 0x51, 1024, 0x3FF, 1,
                     "eeprom_0x51_24C08.bin"),
    0x52: DeviceSpec("24C16", 0x52, 2048, 0x7FF, 1,
                     "eeprom_0x52_24C16.bin"),
    0x53: DeviceSpec("24C64", 0x53, 8192, 0x1FFF, 2,
                     "eeprom_0x53_24C64.bin"),
    0x54: DeviceSpec("24C128", 0x54, 16384, 0x3FFF, 2,
                     "eeprom_0x54_24C128.bin"),
    0x55: DeviceSpec("24C256", 0x55, 32768, 0x7FFF, 2,
                     "eeprom_0x55_24C256.bin"),
    0x56: DeviceSpec("24C512", 0x56, 65536, 0xFFFF, 2,
                     "eeprom_0x56_24C512.bin"),
    0x57: DeviceSpec("24C1024", 0x57, 131072, 0x1FFFF, 2,
                     "eeprom_0x57_24C1024.bin"),
}

# 默认填充值
DEFAULT_FILL = 0xFF


class EepromDevice:
    """单片 EEPROM：内存数组 + 落盘 bin 文件。

    线程安全性：串口收帧线程为单线程顺序处理命令，
    这里仍加锁以防上层将来引入多线程访问。
    """

    def __init__(self, spec: DeviceSpec, data_dir: str,
                 inject_fail_addrs: set[int] | None = None,
                 flush_each_packet: bool = False) -> None:
        self.spec = spec
        self.data_dir = data_dir
        self._lock = threading.RLock()
        # 错误注入：这些“当前地址”上的写操作会失败
        self._inject_fail_addrs = inject_fail_addrs or set()
        # 是否每包立即落盘（默认 False：内存为主，流程结束时落盘）
        self.flush_each_packet = flush_each_packet
        # 内存是否比磁盘新（有未落盘的改动）
        self.dirty = False
        self.mem = bytearray(spec.size)
        self.bin_path = os.path.join(data_dir, spec.bin_name)
        self._load()

    # ---- 持久化 ----
    def _load(self) -> None:
        if os.path.exists(self.bin_path):
            with open(self.bin_path, "rb") as fh:
                raw = fh.read()
            if len(raw) == self.spec.size:
                self.mem[:] = raw
            else:
                # 大小不符时以现有数据填充，多余截断，不足补 0xFF
                n = min(len(raw), self.spec.size)
                self.mem[:n] = raw[:n]
                if len(raw) < self.spec.size:
                    self.mem[len(raw):] = bytes([DEFAULT_FILL]) * (
                        self.spec.size - len(raw))
        else:
            self.mem[:] = bytes([DEFAULT_FILL]) * self.spec.size
            self._flush()
        self.dirty = False

    def _flush(self) -> None:
        """原子落盘；短暂被占用（杀软/其他句柄）时重试。

        仅在多次重试后仍失败才抛出最后一次异常。
        """
        os.makedirs(self.data_dir, exist_ok=True)
        tmp = self.bin_path + ".tmp"
        last_exc: Exception | None = None
        for attempt in range(6):
            try:
                with open(tmp, "wb") as fh:
                    fh.write(self.mem)
                    fh.flush()
                    os.fsync(fh.fileno())
                os.replace(tmp, self.bin_path)
                self.dirty = False
                return
            except OSError as exc:  # 含 PermissionError
                last_exc = exc
                time.sleep(0.03 * (attempt + 1))  # 递增退避
        assert last_exc is not None
        raise last_exc

    def flush(self) -> bool:
        """把内存数据落盘（若脏）。返回是否成功。

        成功或本就不脏返回 True；多次重试仍失败返回 False。
        """
        with self._lock:
            if not self.dirty:
                return True
            try:
                self._flush()
                return True
            except OSError:
                return False

    # ---- 访问 ----
    def read_block(self, addr: int, length: int) -> bytes:
        """从 addr 起读 length 字节（读内存）。"""
        with self._lock:
            return bytes(self.mem[addr:addr + length])

    def write_block(self, addr: int, data: bytes) -> bool:
        """把 data 写入 addr 起点（内存）。

        返回 True 表示成功，False 表示被注入的错误导致失败。
        默认不立即落盘；由上层在流程结束/异常时调用 flush()。
        若 flush_each_packet 为真，则写入后立即落盘。
        """
        with self._lock:
            if addr in self._inject_fail_addrs:
                return False
            end = addr + len(data)
            if end > self.spec.size:
                return False
            self.mem[addr:end] = data
            self.dirty = True
            if self.flush_each_packet:
                # 立即落盘；失败仅告警，内存仍是主存储
                try:
                    self._flush()
                except OSError:
                    pass
            return True

    def set_inject_fail(self, addrs: set[int]) -> None:
        self._inject_fail_addrs = set(addrs)
