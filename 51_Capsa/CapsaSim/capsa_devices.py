"""
Capsa 下位机模拟器 · 设备模型层

用内存数组模拟 I2C 器件，并持久化到独立 bin 文件：

    I2C1 (存储阵列)  8 × 24Cxx   0x50 ~ 0x57
    I2C2 (控制总线)  24C02 元信息 0x50
                     PCF8574      0x58（8 路写保护，仅内存）

所有多字节字段统一小端（little-endian）。
"""

from __future__ import annotations

import os
import random
from pathlib import Path

# --------------------------------------------------------------------------- #
# 常量
# --------------------------------------------------------------------------- #

#: 型号（Kbit） -> 容量（字节）
MODEL_BYTES: dict[int, int] = {
    64: 8 * 1024,
    128: 16 * 1024,
    256: 32 * 1024,
    512: 64 * 1024,
    1024: 128 * 1024,
}

DEFAULT_MODEL = 64
DEFAULT_CAPACITY_BYTES = MODEL_BYTES[DEFAULT_MODEL]

#: 阵列 7 位地址
ARRAY_ADDRESSES = tuple(0x50 + i for i in range(8))

#: 24C02 元信息布局（地址, 长度）
META_SIZE = 256
META_INIT_FLAG = 0x00
META_WP_MASK = 0x01
META_HEALTH_MASK = 0x02
META_NAME = (0x03, 20)
META_CAPACITY = (0x17, 2)
META_MTIME = (0x19, 4)
META_RTIME = (0x1D, 4)
META_MEDIUM = 0x21
META_MODEL = (0x22, 4)
META_UID = (0x26, 6)
#: 当前已使用空间：uint48（6B 小端），单位 B，仅由 UG 更新
META_USED = (0x2C, 6)
META_RESERVED = (0x32, 196)
META_DETECT = (0xF6, 10)

#: UG 可写入的最大值（uint48）
USED_MAX = (1 << 48) - 1

MEDIUM_NAMES = {0: "EEPROM", 1: "FRAM", 2: "EERAM", 3: "SRAM"}


# --------------------------------------------------------------------------- #
# 工具
# --------------------------------------------------------------------------- #

def _atomic_write(path: Path, payload: bytes, durable: bool = True) -> None:
    """先写临时文件再替换，避免掉电写坏 bin。

    ``durable=False`` 时只 flush 不 fsync：每次 16 B 包都强制落盘会让
    模拟器比真实 I2C 慢一个数量级（Windows 上每包新建临时文件还会触发杀软扫描），
    写流程中先用浅落盘，``TX-OVER`` 时再做一次持久化。
    """
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "wb") as fp:
        fp.write(payload)
        fp.flush()
        if durable:
            os.fsync(fp.fileno())
    os.replace(tmp, path)


def model_bytes(model: int) -> int:
    """型号（Kbit）-> 字节数；未知型号按 24C64 处理。"""
    return MODEL_BYTES.get(int(model), DEFAULT_CAPACITY_BYTES)


def model_file_tag(model: int) -> str:
    """24C64 -> "24C64"（bin 文件名的一部分）。"""
    return f"24C{int(model)}"


def model_from_capacity(capacity_bytes: int) -> int:
    """字节数 -> 型号（Kbit），用于日志与容错。"""
    for model, size in MODEL_BYTES.items():
        if size == capacity_bytes:
            return model
    return DEFAULT_MODEL


# --------------------------------------------------------------------------- #
# 24Cxx（I2C1 阵列）
# --------------------------------------------------------------------------- #

class Eeprom24Cxx:
    """单片 24Cxx EEPROM：内存镜像 + bin 持久化。"""

    def __init__(self, data_dir: Path, address: int, model: int = DEFAULT_MODEL) -> None:
        self.data_dir = Path(data_dir)
        self.address = address
        self.model = int(model) if int(model) in MODEL_BYTES else DEFAULT_MODEL
        self.data = bytearray()
        self._load()

    # -- 文件 ------------------------------------------------------------- #

    @property
    def filename(self) -> str:
        return f"eeprom_0x{self.address:02X}_{model_file_tag(self.model)}.bin"

    @property
    def path(self) -> Path:
        return self.data_dir / self.filename

    @property
    def capacity(self) -> int:
        return model_bytes(self.model)

    def _load(self) -> None:
        size = self.capacity
        path = self.path

        if path.exists():
            raw = path.read_bytes()
            if len(raw) == size:
                self.data = bytearray(raw)
                return

            # 尺寸不符（型号变更 / 文件损坏）：按最小长度保留，其余补 0xFF
            self.data = bytearray(b"\xFF" * size)
            keep = min(len(raw), size)
            self.data[:keep] = raw[:keep]
            self.persist()
            return

        self.data = bytearray(b"\xFF" * size)
        self.persist()

    def persist(self, durable: bool = True) -> None:
        _atomic_write(self.path, bytes(self.data), durable)

    # -- 型号变更 ---------------------------------------------------------- #

    def resize(self, model: int) -> None:
        model = int(model)
        if model not in MODEL_BYTES or model == self.model:
            return

        old_path = self.path
        old = bytes(self.data)

        self.model = model
        size = self.capacity
        self.data = bytearray(b"\xFF" * size)
        keep = min(len(old), size)
        self.data[:keep] = old[:keep]

        self.persist()
        if old_path != self.path and old_path.exists():
            try:
                old_path.unlink()
            except OSError:
                pass

    # -- 读写 -------------------------------------------------------------- #

    def read(self, offset: int, length: int) -> bytes:
        offset = max(0, min(offset, self.capacity))
        length = max(0, min(length, self.capacity - offset))
        return bytes(self.data[offset:offset + length])

    def write(self, offset: int, payload: bytes, durable: bool = False) -> None:
        """写入字节并立即写回 bin；写流程中默认浅落盘，TX-OVER 时再持久化。"""
        if offset < 0:
            raise ValueError("offset must be >= 0")

        payload = bytes(payload)[: max(0, self.capacity - offset)]
        if not payload:
            return

        self.data[offset:offset + len(payload)] = payload
        self.persist(durable)

    def fill(self, value: int = 0xFF) -> None:
        self.data = bytearray(bytes([value]) * self.capacity)
        self.persist()

    def __repr__(self) -> str:  # pragma: no cover - 仅日志
        return f"<24C{self.model} @0x{self.address:02X} {self.capacity}B>"


# --------------------------------------------------------------------------- #
# 24C02（I2C2 元信息）
# --------------------------------------------------------------------------- #

class Meta24C02:
    """存储卡元信息：256B 映射，按文档第 4 节布局。"""

    def __init__(self, data_dir: Path) -> None:
        self.data_dir = Path(data_dir)
        self.path = self.data_dir / "meta_24c02.bin"
        self.data = bytearray()
        self._load()

    # -- 文件 ------------------------------------------------------------- #

    def _load(self) -> None:
        if self.path.exists():
            raw = self.path.read_bytes()
            self.data = bytearray(raw[:META_SIZE])
            if len(self.data) < META_SIZE:
                self.data.extend(b"\xFF" * (META_SIZE - len(self.data)))
            return

        self.data = bytearray(b"\xFF" * META_SIZE)
        self.persist()

    def persist(self) -> None:
        _atomic_write(self.path, bytes(self.data))

    # -- 初始化判定 -------------------------------------------------------- #

    @property
    def initialized(self) -> bool:
        """0xF6 ~ 0xFF 全 0xFF = 未初始化。"""
        offset, length = META_DETECT
        return any(b != 0xFF for b in self.data[offset:offset + length])

    @property
    def init_flag(self) -> int:
        return self.data[META_INIT_FLAG]

    # -- 字段读取 ---------------------------------------------------------- #

    def _read(self, field: tuple[int, int]) -> bytes:
        offset, length = field
        return bytes(self.data[offset:offset + length])

    def _write(self, field: tuple[int, int], payload: bytes) -> None:
        offset, length = field
        payload = bytes(payload)[:length]
        self.data[offset:offset + len(payload)] = payload

    def _read_uint(self, field: tuple[int, int]) -> int:
        return int.from_bytes(self._read(field), "little")

    def _write_uint(self, field: tuple[int, int], value: int) -> None:
        _, length = field
        self._write(field, int(value).to_bytes(length, "little"))

    @property
    def wp_mask(self) -> int:
        return self.data[META_WP_MASK]

    @property
    def health_mask(self) -> int:
        return self.data[META_HEALTH_MASK]

    @property
    def name(self) -> str:
        raw = self._read(META_NAME).split(b"\x00", 1)[0]
        return raw.decode("utf-8", "replace")

    @property
    def capacity_kb(self) -> int:
        return self._read_uint(META_CAPACITY)

    @property
    def modified_at(self) -> int:
        return self._read_uint(META_MTIME)

    @property
    def read_at(self) -> int:
        return self._read_uint(META_RTIME)

    @property
    def medium(self) -> int:
        return self.data[META_MEDIUM]

    @property
    def model(self) -> int:
        return self._read_uint(META_MODEL)

    @property
    def uid(self) -> bytes:
        return self._read(META_UID)

    @property
    def uid_hex(self) -> str:
        return self.uid.hex().upper()

    @property
    def used_bytes(self) -> int:
        """当前已使用空间（B）。UG 写入，下位机不做统计。"""
        return self._read_uint(META_USED)

    def set_used_bytes(self, value: int) -> None:
        """UG：写 0x2C ~ 0x31（6B 小端）后立即落盘，不改动其他字段。"""
        self._write_uint(META_USED, max(0, min(int(value), USED_MAX)))
        self.persist()

    # -- 写入 -------------------------------------------------------------- #

    def first_set(
        self,
        *,
        name: str,
        wp_mask: int,
        health_mask: int,
        capacity_kb: int,
        model: int,
        modified_at: int,
        read_at: int,
        medium: int,
        uid: bytes,
    ) -> None:
        """首次初始化：0x00~0xF5 清零后写入字段，检测区写 0x00。

        0x00 ~ 0xF5 的清零同时把 0x2C ~ 0x31（已使用空间）写成
        6 字节 0x00，符合“首次 SET 该字段默认 0”的要求。
        """
        self.data[0x00:0xF6] = b"\x00" * 0xF6
        self._write(META_USED, b"\x00" * META_USED[1])

        self._apply(
            name=name,
            wp_mask=wp_mask,
            health_mask=health_mask,
            capacity_kb=capacity_kb,
            model=model,
            modified_at=modified_at,
            read_at=read_at,
            medium=medium,
            uid=uid,
        )

        self.data[META_INIT_FLAG] = 0x01

        offset, length = META_DETECT
        self.data[offset:offset + length] = b"\x00" * length

        self.persist()

    def update(
        self,
        *,
        name: str | None = None,
        wp_mask: int | None = None,
        health_mask: int | None = None,
        capacity_kb: int | None = None,
        model: int | None = None,
        modified_at: int | None = None,
        read_at: int | None = None,
        medium: int | None = None,
        uid: bytes | None = None,
    ) -> None:
        """后续 SET：只更新非 None 字段，0x00 bit0 保持 0x01。"""
        self._apply(
            name=name,
            wp_mask=wp_mask,
            health_mask=health_mask,
            capacity_kb=capacity_kb,
            model=model,
            modified_at=modified_at,
            read_at=read_at,
            medium=medium,
            uid=uid,
        )

        self.data[META_INIT_FLAG] |= 0x01
        self.persist()

    def _apply(
        self,
        *,
        name: str | None,
        wp_mask: int | None,
        health_mask: int | None,
        capacity_kb: int | None,
        model: int | None,
        modified_at: int | None,
        read_at: int | None,
        medium: int | None,
        uid: bytes | None,
    ) -> None:
        if name is not None:
            raw = name.encode("utf-8")[: META_NAME[1]]
            self._write(META_NAME, raw.ljust(META_NAME[1], b"\x00"))

        if wp_mask is not None:
            self.data[META_WP_MASK] = wp_mask & 0xFF

        if health_mask is not None:
            self.data[META_HEALTH_MASK] = health_mask & 0xFF

        if capacity_kb is not None:
            self._write_uint(META_CAPACITY, capacity_kb & 0xFFFF)

        if model is not None:
            self._write_uint(META_MODEL, model)

        if modified_at is not None:
            self._write_uint(META_MTIME, modified_at & 0xFFFFFFFF)

        if read_at is not None:
            self._write_uint(META_RTIME, read_at & 0xFFFFFFFF)

        if medium is not None:
            self.data[META_MEDIUM] = medium & 0xFF

        if uid is not None:
            self._write(META_UID, uid)

    # -- INFO 字段 --------------------------------------------------------- #

    def to_info_fields(self) -> list[str]:
        """按 SET/INFO 的 9 字段顺序输出。"""
        return [
            self.name,
            f"{self.wp_mask:02X}",
            f"{self.health_mask:02X}",
            str(self.capacity_kb),
            str(self.model or DEFAULT_MODEL),
            str(self.modified_at),
            str(self.read_at),
            str(self.medium),
            self.uid_hex,
        ]

    def describe(self) -> str:
        if not self.initialized:
            return "24C02 meta 256B [blank] · detect area 0xF6-0xFF all 0xFF"

        return (
            f"24C02 meta 256B [initialized] name={self.name!r} "
            f"model=24C{self.model or DEFAULT_MODEL} cap={self.capacity_kb}KB "
            f"used={self.used_bytes}B "
            f"medium={MEDIUM_NAMES.get(self.medium, self.medium)} "
            f"wp=0b{self.wp_mask:08b} health=0b{self.health_mask:08b} uid={self.uid_hex}"
        )


# --------------------------------------------------------------------------- #
# PCF8574（I2C2 写保护输出）
# --------------------------------------------------------------------------- #

class PCF8574:
    """8 路写保护输出；1 = 开启写保护，0 = 关闭。仅内存状态。"""

    def __init__(self, address: int = 0x58) -> None:
        self.address = address
        self.state = 0x00

    def apply(self, mask: int) -> None:
        self.state = mask & 0xFF

    def is_protected(self, chip_address: int) -> bool:
        bit = chip_address - 0x50
        if not 0 <= bit <= 7:
            return False
        return bool(self.state & (1 << bit))

    def bits(self) -> str:
        return format(self.state, "08b")

    def __repr__(self) -> str:  # pragma: no cover - 仅日志
        return f"<PCF8574 @0x{self.address:02X} 0b{self.bits()}>"


def random_uid() -> bytes:
    """随机 6 字节 UID。"""
    return bytes(random.getrandbits(8) for _ in range(6))


def unpack_uid(text: str) -> bytes | None:
    """12 位十六进制字符串 -> 6 字节；非法返回 None。"""
    text = text.strip()
    if len(text) != 12:
        return None
    try:
        return bytes.fromhex(text)
    except ValueError:
        return None
