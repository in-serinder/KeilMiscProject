"""
Capsa 下位机模拟器 · 协议与状态机

帧格式：
    上位机 -> 下位机   >命令<          例如 >TYPE-CAPASA<
    下位机 -> 上位机   [应答]         例如 [RX-OK]
                       >数据<         例如 >INFO_...<  /  >R_...<

状态机负责：命令解析、SET/GET、ROUTER/ADDR、写流程、读流程（含 2s 应答超时）、
写保护判定与 I2C 时序模拟。
"""

from __future__ import annotations

import logging
import random
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

from capsa_devices import (
    ARRAY_ADDRESSES,
    DEFAULT_MODEL,
    Eeprom24Cxx,
    MEDIUM_NAMES,
    Meta24C02,
    MODEL_BYTES,
    PCF8574,
    USED_MAX,
    model_bytes,
    random_uid,
    unpack_uid,
)

LOG = logging.getLogger("capsa.sim")

#: 一包数据字节数
PACKET_SIZE = 16

#: I2C 时序（秒）
I2C_PACKET_DELAY = 0.0008   # 16B @250kHz ≈ 0.8ms
I2C_PAGE_WRITE_DELAY = 0.005  # 写后等待 5ms


class MachineIO(Protocol):
    """状态机使用的最小 IO 接口，便于单测替换。"""

    def write(self, text: str) -> None:
        ...

    def read_frame(self, timeout: float | None) -> str | None:
        ...


@dataclass
class SimConfig:
    data_dir: Path
    rx_timeout: float = 2.0
    delays: bool = True


def parse_int(text: str) -> int | None:
    """解析 0x50 / 80 / 0b1010 形式的整数；失败返回 None。"""
    text = text.strip()
    if not text:
        return None
    try:
        if text.lower().startswith(("0x", "-0x")):
            return int(text, 16)
        if text.lower().startswith(("0b", "-0b")):
            return int(text, 2)
        return int(text, 10)
    except ValueError:
        return None


def parse_hex_byte(text: str) -> int | None:
    """解析 1~2 位十六进制字节（"FF" / "0" / "a5"）；失败返回 None。"""
    token = text.strip()
    if not 1 <= len(token) <= 2:
        return None
    try:
        value = int(token, 16)
    except ValueError:
        return None
    return value if 0 <= value <= 0xFF else None


class CapsaMachine:
    """Capsa 存储卡下位机状态机。"""

    def __init__(self, config: SimConfig, io: MachineIO) -> None:
        self.config = config
        self.io = io

        data_dir = Path(config.data_dir)
        data_dir.mkdir(parents=True, exist_ok=True)

        self.meta = Meta24C02(data_dir)
        self.pcf = PCF8574(0x58)
        self.pcf.apply(self.meta.wp_mask if self.meta.initialized else 0x00)

        model = self.meta.model if self.meta.initialized and self.meta.model in MODEL_BYTES else DEFAULT_MODEL
        self.chips: dict[int, Eeprom24Cxx] = {
            addr: Eeprom24Cxx(data_dir, addr, model) for addr in ARRAY_ADDRESSES
        }

        # 运行态
        self.current_chip = ARRAY_ADDRESSES[0]   # 默认 0x50
        self.routed = False                      # 是否收到过 ROUTER
        self.boundary: int | None = None         # ADDR 设定的末尾地址
        self.mode: str | None = None             # "write" / "read" / None
        self.pointer = 0                         # 包内当前地址

        LOG.info(
            "machine ready · meta=%s · array=%s",
            "initialized" if self.meta.initialized else "blank",
            f"8×24C{model} @0x50-0x57, {model_bytes(model) // 1024}KB each",
        )
        LOG.info("PCF8574 initial output 0b%s", self.pcf.bits())

    # ------------------------------------------------------------------ #
    # 基础工具
    # ------------------------------------------------------------------ #

    def _sleep(self, seconds: float) -> None:
        if self.config.delays and seconds > 0:
            time.sleep(seconds)

    def _reply(self, text: str) -> None:
        self.io.write(f"[{text}]")

    def _send(self, text: str) -> None:
        self.io.write(f">{text}<")

    @property
    def chip(self) -> Eeprom24Cxx:
        return self.chips[self.current_chip]

    def _wp_active(self, address: int | None = None) -> bool:
        return self.pcf.is_protected(self.current_chip if address is None else address)

    @property
    def total_capacity_bytes(self) -> int:
        """阵列总容量（8 片之和，B），用作 UG 的取值范围上界。"""
        return sum(len(chip.data) for chip in self.chips.values())

    # ------------------------------------------------------------------ #
    # 入口
    # ------------------------------------------------------------------ #

    def handle(self, frame: str) -> None:
        """处理一帧命令（已去掉 > < 包装）。"""
        command = (frame or "").strip()
        if not command:
            return

        upper = command.upper()

        try:
            if upper == "TYPE-CAPASA":
                LOG.info("TYPE-CAPASA -> YES")
                return self._reply("YES")

            if upper.startswith("SET_"):
                return self._handle_set(command)

            if upper == "GET_!" or upper == "GET":
                return self._handle_get()

            if upper == "UGS":
                return self._handle_ugs()

            if upper.startswith("UG_"):
                return self._handle_ug(command)

            if upper.startswith("ROUTER_"):
                return self._handle_router(command)

            if upper.startswith("ADDR_"):
                return self._handle_addr(command)

            if upper == "INT_W":
                return self._handle_int_write()

            if upper.startswith("W_"):
                return self._handle_write_packet(command)

            if upper == "TX-OVER":
                self.mode = None
                # 写会话结束：把本片 bin 补齐持久化（写包过程中只做浅落盘）
                self.chip.persist(durable=True)
                LOG.info("TX-OVER · write session closed (router=0x%02X, addr=0x%04X kept) · bin persisted",
                         self.current_chip, self.boundary or 0)
                return self._reply("RX-OK")

            if upper == "INT_R":
                return self._handle_int_read()

            if upper == "RX-OK" and self.mode != "read":
                # 读流程之外收到的主机应答属于噪声，静默忽略
                LOG.debug("ignore stray [RX-OK] outside read session")
                return

            if upper == "ABORT":
                self.mode = None
                self.chip.persist(durable=True)
                LOG.info("ABORT · read/write cancelled (router/addr kept)")
                return self._reply("ABORT-OK")

            LOG.warning("unknown command %r", command)
            return self._reply("RX-ERR")

        except Exception as exc:  # pragma: no cover - 兜底保护
            LOG.exception("command %r failed: %s", command, exc)
            return self._reply("RX-ERR")

    # ------------------------------------------------------------------ #
    # SET / GET
    # ------------------------------------------------------------------ #

    def _handle_set(self, command: str) -> None:
        fields = command[4:].split("_")
        if len(fields) != 9:
            LOG.warning("SET rejected: expected 9 fields, got %d", len(fields))
            return self._reply("SET_ERR")

        raw_name, raw_wp, raw_health, raw_cap, raw_model, raw_mtime, raw_rtime, raw_medium, raw_uid = fields

        # 名称
        name: str | None = None
        if raw_name != "!":
            if "!" in raw_name or "_" in raw_name:
                LOG.warning("SET rejected: name may not contain '_' or '!'")
                return self._reply("SET_ERR")
            if len(raw_name.encode("utf-8")) > 20:
                LOG.warning("SET rejected: name longer than 20 bytes")
                return self._reply("SET_ERR")
            name = raw_name

        def parse_mask(text: str, label: str) -> int | None:
            """掩码为 1 字节：标准写法 2 位十六进制（"FF"），兼容旧版 8 位二进制。"""
            if text == "!":
                return None
            if len(text) == 2:
                try:
                    return int(text, 16)
                except ValueError:
                    pass
            if len(text) == 8 and all(ch in "01" for ch in text):
                return int(text, 2)
            LOG.warning("SET rejected: %s mask must be 2 hex chars, got %r", label, text)
            raise ValueError(label)

        try:
            wp = parse_mask(raw_wp, "WP")
            health = parse_mask(raw_health, "health")
        except ValueError:
            return self._reply("SET_ERR")

        capacity = None
        if raw_cap != "!":
            value = parse_int(raw_cap)
            if value is None or not 0 <= value <= 0xFFFF:
                LOG.warning("SET rejected: bad capacity %r", raw_cap)
                return self._reply("SET_ERR")
            capacity = value

        model = None
        if raw_model != "!":
            value = parse_int(raw_model)
            if value is None:
                LOG.warning("SET rejected: bad model %r", raw_model)
                return self._reply("SET_ERR")
            # 型号 0 按 24C64 处理
            model = DEFAULT_MODEL if value == 0 else value
            if model not in MODEL_BYTES:
                LOG.warning("SET rejected: unsupported model %s", value)
                return self._reply("SET_ERR")

        def parse_time(text: str, label: str) -> int | None:
            if text == "!":
                return None
            value = parse_int(text)
            if value is None or not 0 <= value <= 0xFFFFFFFF:
                LOG.warning("SET rejected: bad %s %r", label, text)
                raise ValueError(label)
            return value

        try:
            modified_at = parse_time(raw_mtime, "modified time")
            read_at = parse_time(raw_rtime, "read time")
        except ValueError:
            return self._reply("SET_ERR")

        medium = None
        if raw_medium != "!":
            value = parse_int(raw_medium)
            if value is None or not 0 <= value <= 3:
                LOG.warning("SET rejected: bad medium %r", raw_medium)
                return self._reply("SET_ERR")
            medium = value

        uid: bytes | None = None
        if raw_uid != "!":
            uid = unpack_uid(raw_uid)
            if uid is None:
                LOG.warning("SET rejected: UID must be 12 hex chars, got %r", raw_uid)
                return self._reply("SET_ERR")

        if not self.meta.initialized:
            self._first_set(
                name=name if name is not None else "",
                wp=wp if wp is not None else 0x00,
                health=health if health is not None else 0xFF,
                capacity=capacity if capacity is not None else 0,
                model=model if model is not None else DEFAULT_MODEL,
                modified_at=modified_at if modified_at is not None else 0,
                read_at=read_at if read_at is not None else 0,
                medium=medium if medium is not None else 0,
                uid=uid if uid is not None else random_uid(),
            )
            return self._reply("SET-OK")

        self._update_set(
            name=name,
            wp=wp,
            health=health,
            capacity=capacity,
            model=model,
            modified_at=modified_at,
            read_at=read_at,
            medium=medium,
            uid=uid,
        )
        return self._reply("SET-OK")

    def _first_set(
        self,
        *,
        name: str,
        wp: int,
        health: int,
        capacity: int,
        model: int,
        modified_at: int,
        read_at: int,
        medium: int,
        uid: bytes,
    ) -> None:
        LOG.info(
            "first SET · name=%r wp=0b%s health=0b%s cap=%dKB model=24C%d medium=%s uid=%s",
            name, format(wp, "08b"), format(health, "08b"), capacity, model,
            MEDIUM_NAMES.get(medium, medium), uid.hex().upper(),
        )

        self.meta.first_set(
            name=name,
            wp_mask=wp,
            health_mask=health,
            capacity_kb=capacity,
            model=model,
            modified_at=modified_at,
            read_at=read_at,
            medium=medium,
            uid=uid,
        )
        self._sleep(I2C_PAGE_WRITE_DELAY)  # 写 24C02 后等待 5ms

        self._apply_wp_mask(wp, reason="first SET")
        self._apply_model(model)

    def _update_set(
        self,
        *,
        name: str | None,
        wp: int | None,
        health: int | None,
        capacity: int | None,
        model: int | None,
        modified_at: int | None,
        read_at: int | None,
        medium: int | None,
        uid: bytes | None,
    ) -> None:
        changed_wp = wp is not None and wp != self.meta.wp_mask
        changed_model = model is not None and model != self.meta.model

        LOG.info(
            "SET update · %s",
            ", ".join(
                f"{key}={'!' if value is None else value}"
                for key, value in (
                    ("name", name),
                    ("wp", format(wp, "08b") if wp is not None else None),
                    ("health", format(health, "08b") if health is not None else None),
                    ("cap", capacity),
                    ("model", model),
                    ("mtime", modified_at),
                    ("rtime", read_at),
                    ("medium", medium),
                    ("uid", uid.hex().upper() if uid else None),
                )
            ),
        )

        self.meta.update(
            name=name,
            wp_mask=wp,
            health_mask=health,
            capacity_kb=capacity,
            model=model,
            modified_at=modified_at,
            read_at=read_at,
            medium=medium,
            uid=uid,
        )
        self._sleep(I2C_PAGE_WRITE_DELAY)

        if changed_wp:
            self._apply_wp_mask(wp, reason="SET")

        if changed_model:
            self._apply_model(model)

    def _apply_wp_mask(self, mask: int, *, reason: str) -> None:
        LOG.info(
            "PCF8574 <- 0b%s (%s) · 0x50..0x57 = %s",
            format(mask, "08b"),
            reason,
            " ".join(
                f"0x{addr:02X}:{'WP' if mask & (1 << (addr - 0x50)) else '--'}"
                for addr in ARRAY_ADDRESSES
            ),
        )
        self.pcf.apply(mask)
        self._sleep(I2C_PAGE_WRITE_DELAY)

    def _apply_model(self, model: int) -> None:
        if model not in MODEL_BYTES:
            return

        LOG.info("array model -> 24C%d (%d bytes/chip)", model, model_bytes(model))
        for chip in self.chips.values():
            chip.resize(model)

    def _handle_get(self) -> None:
        if not self.meta.initialized:
            LOG.info("GET on blank card -> SET_ERR")
            return self._reply("SET_ERR")

        fields = self.meta.to_info_fields()
        payload = "INFO_" + "_".join(fields)
        LOG.info("GET -> %s", payload)
        self._send(payload)

    # ------------------------------------------------------------------ #
    # UG / UGS（已使用空间）
    # ------------------------------------------------------------------ #

    def _handle_ug(self, command: str) -> None:
        """UG_<字节数>：写 24C02 0x2C ~ 0x31（uint48 小端）后返回 [UG-OK]。"""
        raw = command[3:]

        if not self.meta.initialized:
            LOG.info("UG %r rejected · 24C02 not initialized -> SET_ERR", raw)
            return self._reply("SET_ERR")

        # 十进制字符串，长度 15 已足够表达 uint48，超长直接拒绝以免 int() 抛错
        if not raw or len(raw) > 20 or not raw.isascii() or not raw.isdigit():
            LOG.warning("UG rejected: %r is not a decimal byte count", raw)
            return self._reply("RX-ERR")

        value = int(raw)
        limit = self.total_capacity_bytes
        if value > limit or value > USED_MAX:
            LOG.warning("UG rejected: %d B exceeds array capacity %d B", value, limit)
            return self._reply("RX-ERR")

        self.meta.set_used_bytes(value)
        self._sleep(I2C_PAGE_WRITE_DELAY)  # 24C02 写周期 5ms

        LOG.info(
            "UG -> used %d B / %d B (%.1f%%) · 24C02 0x2C-0x31 = %s",
            value, limit, value * 100 / max(1, limit),
            value.to_bytes(6, "little").hex(" ").upper(),
        )
        return self._reply("UG-OK")

    def _handle_ugs(self) -> None:
        """UGS：读 24C02 0x2C ~ 0x31 并返回 [UG-<字节数>]。"""
        if not self.meta.initialized:
            LOG.info("UGS on blank card -> SET_ERR")
            return self._reply("SET_ERR")

        value = self.meta.used_bytes
        LOG.info("UGS -> %d B", value)
        return self._reply(f"UG-{value}")

    # ------------------------------------------------------------------ #
    # ROUTER / ADDR
    # ------------------------------------------------------------------ #

    def _handle_router(self, command: str) -> None:
        value = parse_int(command[7:])
        if value is None or value not in self.chips:
            LOG.warning("ROUTER rejected: invalid address %r", command[7:])
            return self._reply("RX-ERR")

        self.current_chip = value
        self.routed = True
        self.boundary = None
        self.mode = None
        self.pointer = 0

        LOG.info("ROUTER -> 0x%02X (24C%d, %d bytes), boundary reset",
                 value, self.chip.model, self.chip.capacity)
        return self._reply("RX-OK")

    def _handle_addr(self, command: str) -> None:
        if not self.routed:
            LOG.warning("ADDR rejected: ROUTER first")
            return self._reply("RX-ERR")

        value = parse_int(command[5:])
        last = self.chip.capacity - 1
        if value is None or value < 0:
            LOG.warning("ADDR rejected: bad value %r", command[5:])
            return self._reply("RX-ERR")

        if value > last:
            LOG.warning("ADDR 0x%X out of range (0x0000-0x%04X)", value, last)
            return self._reply("ERR_EXT")

        self.boundary = value
        self.mode = None
        self.pointer = 0
        LOG.info("ADDR -> end=0x%04X (%d bytes) on 0x%02X · chip range 0x0000-0x%04X",
                 value, value + 1, self.current_chip, last)
        return self._reply("RX-OK")

    # ------------------------------------------------------------------ #
    # 写流程
    # ------------------------------------------------------------------ #

    def _handle_int_write(self) -> None:
        if not self.routed or self.boundary is None:
            LOG.warning("INT_W rejected: ROUTER/ADDR missing")
            return self._reply("RX-ERR")

        if self._wp_active():
            LOG.warning("INT_W rejected: 0x%02X is write protected (PCF8574=0b%s)",
                        self.current_chip, self.pcf.bits())
            return self._reply("ERR_WP")

        self.mode = "write"
        self.pointer = 0
        LOG.info("INT_W -> start writing 0x%02X, range 0x0000-0x%04X",
                 self.current_chip, self.boundary)
        return self._reply("RX-OK")

    def _handle_write_packet(self, command: str) -> None:
        if self.mode != "write":
            LOG.warning("W_ rejected: no active write session")
            return self._reply("RX-ERR")

        if self._wp_active():
            LOG.warning("W_ rejected: 0x%02X is write protected", self.current_chip)
            return self._reply("ERR_WP")

        if self.boundary is None or self.pointer > self.boundary:
            LOG.warning("W_ rejected: pointer 0x%04X beyond ADDR 0x%04X",
                        self.pointer, self.boundary or 0)
            return self._reply("ERR_EXT")

        tokens = [t for t in command[2:].split(",") if t.strip()]
        if not tokens or len(tokens) > PACKET_SIZE:
            LOG.warning("W_ rejected: %d bytes in packet (max %d)", len(tokens), PACKET_SIZE)
            return self._reply("RX-ERR")

        payload = bytearray()
        for token in tokens:
            value = parse_hex_byte(token)
            if value is None:
                LOG.warning("W_ rejected: bad hex byte %r", token)
                return self._reply("RX-ERR")
            payload.append(value)

        # 不足 16B 补 0x00，但补的字节不会写到 ADDR 之后
        payload.extend(b"\x00" * (PACKET_SIZE - len(payload)))

        effective = min(PACKET_SIZE, self.boundary - self.pointer + 1)
        chunk = bytes(payload[:effective])

        self.chip.write(self.pointer, chunk)
        self._sleep(I2C_PACKET_DELAY)
        self._sleep(I2C_PAGE_WRITE_DELAY)

        LOG.info(
            "W_ 0x%02X @0x%04X +%dB (%s%s) · %s · wait %.1fms",
            self.current_chip,
            self.pointer,
            effective,
            chunk[:4].hex(" "),
            " …" if effective > 4 else "",
            "bin flushed",
            (I2C_PACKET_DELAY + I2C_PAGE_WRITE_DELAY) * 1000,
        )

        self.pointer += effective
        return self._reply("RX-OK")

    # ------------------------------------------------------------------ #
    # 读流程
    # ------------------------------------------------------------------ #

    def _handle_int_read(self) -> None:
        if not self.routed or self.boundary is None:
            LOG.warning("INT_R rejected: ROUTER/ADDR missing")
            return self._reply("RX-ERR")

        self.mode = "read"
        self.pointer = 0
        LOG.info("INT_R -> read 0x%02X, range 0x0000-0x%04X",
                 self.current_chip, self.boundary)
        self._reply("RX-OK")
        self._pump_read()

    def _pump_read(self) -> None:
        assert self.boundary is not None
        packets = 0

        while self.pointer <= self.boundary:
            effective = min(PACKET_SIZE, self.boundary - self.pointer + 1)
            chunk = self.chip.read(self.pointer, effective)
            chunk = chunk + b"\x00" * (PACKET_SIZE - len(chunk))

            self._send("R_" + ",".join(f"{b:02X}" for b in chunk))
            self._sleep(I2C_PACKET_DELAY)
            packets += 1

            answer = self.io.read_frame(self.config.rx_timeout)
            if answer is None:
                LOG.warning("read packet %d: no answer within %.1fs -> RX-ERR", packets, self.config.rx_timeout)
                self.mode = None
                return self._reply("RX-ERR")

            normalised = answer.strip().upper()
            if normalised == "RX-OK":
                self.pointer += effective
                continue

            if normalised == "ABORT":
                LOG.info("read aborted by host at 0x%04X", self.pointer)
                self.mode = None
                return self._reply("ABORT-OK")

            LOG.warning("read packet %d: unexpected answer %r -> RX-ERR", packets, answer)
            self.mode = None
            return self._reply("RX-ERR")

        self.mode = None
        LOG.info("read finished · %d packets · TX-OVER", packets)
        self._send("TX-OVER")

    # ------------------------------------------------------------------ #
    # 诊断
    # ------------------------------------------------------------------ #

    def summary(self) -> str:
        return (
            f"router=0x{self.current_chip:02X} routed={self.routed} "
            f"addr={'-' if self.boundary is None else f'0x{self.boundary:04X}'} "
            f"mode={self.mode or 'idle'} wp=0b{self.pcf.bits()}"
        )


def build_machine(data_dir: str | Path, rx_timeout: float = 2.0, delays: bool = True,
                  io: MachineIO | None = None) -> CapsaMachine:
    """便捷构造（测试与 CLI 共用）。"""
    if io is None:
        raise ValueError("io is required")
    return CapsaMachine(SimConfig(data_dir=Path(data_dir), rx_timeout=rx_timeout, delays=delays), io)


__all__ = [
    "CapsaMachine",
    "MachineIO",
    "SimConfig",
    "parse_int",
    "PACKET_SIZE",
    "build_machine",
]
