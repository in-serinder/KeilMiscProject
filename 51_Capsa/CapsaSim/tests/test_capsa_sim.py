"""
Capsa 下位机模拟器 · 单元测试

覆盖任务书第十五节的 15 个测试场景，直接驱动状态机（不经过串口）。

    python -m unittest discover -s tests -v
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from capsa_machine import CapsaMachine, SimConfig          # noqa: E402
from capsa_devices import ARRAY_ADDRESSES, MODEL_BYTES    # noqa: E402


class FakeIO:
    """收集下位机输出，并提供预置的上位机应答。"""

    def __init__(self) -> None:
        self.out: list[str] = []
        self.inbox: deque[str] = deque()

    def write(self, text: str) -> None:
        self.out.append(text)

    def read_frame(self, timeout: float | None = None) -> str | None:
        return self.inbox.popleft() if self.inbox else None

    # -- 断言辅助 ------------------------------------------------------ #

    def clear(self) -> None:
        self.out.clear()

    @property
    def last(self) -> str:
        return self.out[-1] if self.out else ""

    def packets(self) -> list[str]:
        return [line for line in self.out if line.startswith(">R_")]


INIT_FIELDS = "DEV1_00_FF_64_64_1700000000_1700000100_0_A1B2C3D4E5F6"


class SimulatorTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.data_dir = Path(self._tmp.name)
        self.io = FakeIO()
        self.machine = self.make_machine()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    # ------------------------------------------------------------------ #

    def make_machine(self, rx_timeout: float = 0.05) -> CapsaMachine:
        return CapsaMachine(
            SimConfig(data_dir=self.data_dir, rx_timeout=rx_timeout, delays=False),
            self.io,
        )

    def run_cmd(self, command: str) -> str:
        self.io.clear()
        self.machine.handle(command)
        return self.io.last

    def init_card(self, fields: str = INIT_FIELDS) -> None:
        response = self.run_cmd(f"SET_{fields}")
        self.assertEqual(response, "[SET-OK]")

    def prepare_write(self, chip: int = 0x50, last_addr: str = "0x001F") -> None:
        self.assertEqual(self.run_cmd(f"ROUTER_0x{chip:02X}"), "[RX-OK]")
        self.assertEqual(self.run_cmd(f"ADDR_{last_addr}"), "[RX-OK]")
        self.assertEqual(self.run_cmd("INT_W"), "[RX-OK]")

    # ------------------------------------------------------------------ #
    # 1. 设备类型
    # ------------------------------------------------------------------ #

    def test_01_type_capasa(self) -> None:
        self.assertEqual(self.run_cmd("TYPE-CAPASA"), "[YES]")

    # ------------------------------------------------------------------ #
    # 2. 空白卡 GET
    # ------------------------------------------------------------------ #

    def test_02_get_on_blank_card(self) -> None:
        self.assertFalse(self.machine.meta.initialized)
        self.assertEqual(self.run_cmd("GET_!"), "[SET_ERR]")

    # ------------------------------------------------------------------ #
    # 3. 首次 SET 全字段
    # ------------------------------------------------------------------ #

    def test_03_first_set_and_get(self) -> None:
        self.init_card()

        self.assertTrue(self.machine.meta.initialized)
        self.assertEqual(self.run_cmd("GET_!"), ">INFO_" + INIT_FIELDS + "<")

        # 检测区被写成 0x00，初始化标志置 1
        self.assertEqual(self.machine.meta.init_flag, 0x01)
        self.assertEqual(self.machine.meta.data[0xF6:0x100], b"\x00" * 10)

    # ------------------------------------------------------------------ #
    # 4. 型号字段 0 按 24C64
    # ------------------------------------------------------------------ #

    def test_04_model_zero_means_24c64(self) -> None:
        fields = "DEV1_00_FF_64_0_1700000000_1700000100_0_A1B2C3D4E5F6"
        self.init_card(fields)

        info = self.run_cmd("GET_!")
        self.assertTrue(info.startswith(">INFO_DEV1_"))
        self.assertEqual(info.split("_")[5], "64")
        self.assertEqual(self.machine.meta.model, 64)
        self.assertEqual(self.machine.chips[0x50].capacity, MODEL_BYTES[64])

    # ------------------------------------------------------------------ #
    # 5. 只改名称
    # ------------------------------------------------------------------ #

    def test_05_set_only_name(self) -> None:
        self.init_card()
        self.assertEqual(self.run_cmd("SET_DEV2_!_!_!_!_!_!_!_!"), "[SET-OK]")

        info = self.run_cmd("GET_!")
        self.assertEqual(
            info,
            ">INFO_DEV2_00_FF_64_64_1700000000_1700000100_0_A1B2C3D4E5F6<",
        )

    # ------------------------------------------------------------------ #
    # 6 / 7. 写保护与 PCF8574
    # ------------------------------------------------------------------ #

    def test_06_write_protect_blocks_target_chip(self) -> None:
        self.init_card("DEV1_01_FF_64_64_1700000000_1700000100_0_A1B2C3D4E5F6")

        # 0x50 被保护
        self.assertEqual(self.run_cmd("ROUTER_0x50"), "[RX-OK]")
        self.assertEqual(self.run_cmd("ADDR_0x001F"), "[RX-OK]")
        self.assertEqual(self.run_cmd("INT_W"), "[ERR_WP]")

        # 0x51 未被保护
        self.assertEqual(self.run_cmd("ROUTER_0x51"), "[RX-OK]")
        self.assertEqual(self.run_cmd("ADDR_0x001F"), "[RX-OK]")
        self.assertEqual(self.run_cmd("INT_W"), "[RX-OK]")
        self.assertEqual(self.run_cmd("W_" + ",".join(["1"] * 16)), "[RX-OK]")

    def test_07_pcf8574_mirrors_wp_mask(self) -> None:
        self.init_card("DEV1_01_FF_64_64_1700000000_1700000100_0_A1B2C3D4E5F6")
        self.assertEqual(self.machine.pcf.bits(), "00000001")
        self.assertTrue(self.machine.pcf.is_protected(0x50))
        self.assertFalse(self.machine.pcf.is_protected(0x51))

        self.run_cmd("SET_!_F0_!_!_!_!_!_!_!")
        self.assertEqual(self.machine.pcf.bits(), "11110000")
        self.assertEqual(
            [self.machine.pcf.is_protected(addr) for addr in ARRAY_ADDRESSES],
            [False, False, False, False, True, True, True, True],
        )

    # ------------------------------------------------------------------ #
    # 8. 地址越界
    # ------------------------------------------------------------------ #

    def test_08_addr_out_of_range(self) -> None:
        self.init_card()
        self.assertEqual(self.run_cmd("ROUTER_0x50"), "[RX-OK]")
        self.assertEqual(self.run_cmd("ADDR_0x2000"), "[ERR_EXT]")
        self.assertEqual(self.run_cmd("ADDR_0x1FFF"), "[RX-OK]")

    # ------------------------------------------------------------------ #
    # 9. 缺少 ROUTER / ADDR
    # ------------------------------------------------------------------ #

    def test_09_int_write_without_router_or_addr(self) -> None:
        self.init_card()
        self.assertEqual(self.run_cmd("INT_W"), "[RX-ERR]")

        self.assertEqual(self.run_cmd("ROUTER_0x50"), "[RX-OK]")
        self.assertEqual(self.run_cmd("INT_W"), "[RX-ERR]")  # 还没有 ADDR

        self.assertEqual(self.run_cmd("ADDR_0x001F"), "[RX-OK]")
        self.assertEqual(self.run_cmd("INT_W"), "[RX-OK]")

    # ------------------------------------------------------------------ #
    # 10. 写两包再读回
    # ------------------------------------------------------------------ #

    def test_10_write_two_packets_and_read_back(self) -> None:
        self.init_card()
        self.prepare_write(last_addr="0x001F")

        first = [f"{i:02X}" for i in range(16)]
        second = [f"{i + 16:02X}" for i in range(16)]

        self.assertEqual(self.run_cmd("W_" + ",".join(first)), "[RX-OK]")
        self.assertEqual(self.run_cmd("W_" + ",".join(second)), "[RX-OK]")
        self.assertEqual(self.run_cmd("TX-OVER"), "[RX-OK]")

        self.assertEqual(self.machine.chips[0x50].read(0, 32), bytes(range(32)))

        # 读回：两包，每包 16B，主机逐包回 [RX-OK]
        self.io.inbox.extend(["RX-OK", "RX-OK"])
        self.io.clear()
        self.machine.handle("INT_R")

        self.assertEqual(self.io.out[0], "[RX-OK]")
        packets = self.io.packets()
        self.assertEqual(len(packets), 2)
        self.assertEqual(packets[0], ">R_" + ",".join(first) + "<")
        self.assertEqual(packets[1], ">R_" + ",".join(second) + "<")
        self.assertEqual(self.io.out[-1], ">TX-OVER<")

    # ------------------------------------------------------------------ #
    # 11. 末包不足 16B
    # ------------------------------------------------------------------ #

    def test_11_short_packet_is_padded_and_clipped(self) -> None:
        self.init_card()
        self.prepare_write(last_addr="0x0004")  # 只有 5 字节空间

        payload = [f"{i + 1:02X}" for i in range(16)]  # 16B 数据，只应写入 5B
        self.assertEqual(self.run_cmd("W_" + ",".join(payload)), "[RX-OK]")

        self.assertEqual(self.machine.chips[0x50].read(0, 8), bytes([1, 2, 3, 4, 5, 0xFF, 0xFF, 0xFF]))

        # 再发一包已经越界
        self.assertEqual(self.run_cmd("W_" + ",".join(payload)), "[ERR_EXT]")

        # 读回：5 字节 + 11 个 0x00 补齐
        self.io.inbox.append("RX-OK")
        self.io.clear()
        self.machine.handle("INT_R")

        packets = self.io.packets()
        self.assertEqual(len(packets), 1)
        expected = [f"{i:02X}" for i in [1, 2, 3, 4, 5]] + ["00"] * 11
        self.assertEqual(packets[0], ">R_" + ",".join(expected) + "<")

    # ------------------------------------------------------------------ #
    # 12. 读流程无应答
    # ------------------------------------------------------------------ #

    def test_12_read_timeout_returns_rx_err(self) -> None:
        self.init_card()
        self.machine = self.make_machine(rx_timeout=0.02)
        self.assertEqual(self.run_cmd("ROUTER_0x50"), "[RX-OK]")
        self.assertEqual(self.run_cmd("ADDR_0x001F"), "[RX-OK]")

        self.io.clear()
        self.machine.handle("INT_R")  # inbox 为空 -> 超时

        self.assertEqual(self.io.out[0], "[RX-OK]")
        self.assertEqual(self.io.out[-1], "[RX-ERR]")
        self.assertIsNone(self.machine.mode)

    # ------------------------------------------------------------------ #
    # 13. ABORT
    # ------------------------------------------------------------------ #

    def test_13_abort_interrupts_read(self) -> None:
        self.init_card()
        self.assertEqual(self.run_cmd("ROUTER_0x50"), "[RX-OK]")
        self.assertEqual(self.run_cmd("ADDR_0x001F"), "[RX-OK]")

        self.io.inbox.append("ABORT")
        self.io.clear()
        self.machine.handle("INT_R")

        self.assertEqual(self.io.out[-1], "[ABORT-OK]")
        self.assertIsNone(self.machine.mode)
        self.assertEqual(self.run_cmd("ABORT"), "[ABORT-OK]")

    # ------------------------------------------------------------------ #
    # 14. TX-OVER 保留 ROUTER / ADDR
    # ------------------------------------------------------------------ #

    def test_14_tx_over_keeps_router_and_addr(self) -> None:
        self.init_card()
        self.prepare_write(last_addr="0x001F")
        self.assertEqual(self.run_cmd("W_" + ",".join(["0"] * 16)), "[RX-OK]")
        self.assertEqual(self.run_cmd("TX-OVER"), "[RX-OK]")

        self.assertEqual(self.machine.current_chip, 0x50)
        self.assertEqual(self.machine.boundary, 0x1F)

        # 无需重新 ROUTER / ADDR 即可再次开写
        self.assertEqual(self.run_cmd("INT_W"), "[RX-OK]")

    # ------------------------------------------------------------------ #
    # 15. bin 文件
    # ------------------------------------------------------------------ #

    def test_15_bin_files_created_and_resized(self) -> None:
        self.init_card()

        for addr in ARRAY_ADDRESSES:
            path = self.data_dir / f"eeprom_0x{addr:02X}_24C64.bin"
            self.assertTrue(path.exists(), path.name)
            self.assertEqual(path.stat().st_size, 8192)

        meta = self.data_dir / "meta_24c02.bin"
        self.assertTrue(meta.exists())
        self.assertEqual(meta.stat().st_size, 256)

        # 写入后立即落盘
        self.prepare_write(last_addr="0x0000")
        self.assertEqual(self.run_cmd("W_AA,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00"), "[RX-OK]")
        self.assertEqual((self.data_dir / "eeprom_0x50_24C64.bin").read_bytes()[0], 170)

        # 型号改为 24C128：重建 bin，按最小长度保留，其余补 0xFF
        self.assertEqual(self.run_cmd("SET_!_!_!_!_128_!_!_!_!"), "[SET-OK]")

        old = self.data_dir / "eeprom_0x50_24C64.bin"
        new = self.data_dir / "eeprom_0x50_24C128.bin"
        self.assertFalse(old.exists())
        self.assertTrue(new.exists())
        self.assertEqual(new.stat().st_size, 16384)

        data = new.read_bytes()
        self.assertEqual(data[0], 170)
        self.assertEqual(data[8192:], b"\xFF" * 8192)

    # ------------------------------------------------------------------ #
    # 16. 首次 SET 后 UGS = 0
    # ------------------------------------------------------------------ #

    def test_16_ugs_after_first_set_is_zero(self) -> None:
        self.init_card()

        self.assertEqual(self.run_cmd("UGS"), "[UG-0]")
        self.assertEqual(self.machine.meta.data[0x2C:0x32], b"\x00" * 6)
        self.assertEqual(self.machine.meta.used_bytes, 0)

    # ------------------------------------------------------------------ #
    # 17. UG 写入后可读回，bin 为 6B 小端
    # ------------------------------------------------------------------ #

    def test_17_ug_sets_and_ugs_reads_back(self) -> None:
        self.init_card()

        self.assertEqual(self.run_cmd("UG_12345"), "[UG-OK]")
        self.assertEqual(self.run_cmd("UGS"), "[UG-12345]")

        raw = (self.data_dir / "meta_24c02.bin").read_bytes()[0x2C:0x32]
        self.assertEqual(raw, (12345).to_bytes(6, "little"))
        self.assertEqual(int.from_bytes(raw, "little"), 12345)

    # ------------------------------------------------------------------ #
    # 18 / 19. UG 取值范围（8 片 24C64 = 65536 B）
    # ------------------------------------------------------------------ #

    def test_18_ug_at_array_capacity_is_accepted(self) -> None:
        self.init_card()
        self.assertEqual(self.run_cmd("UG_65536"), "[UG-OK]")
        self.assertEqual(self.run_cmd("UGS"), "[UG-65536]")

    def test_19_ug_beyond_array_capacity_is_rejected(self) -> None:
        self.init_card()
        self.assertEqual(self.run_cmd("UG_65537"), "[RX-ERR]")
        self.assertEqual(self.run_cmd("UGS"), "[UG-0]")

        # 非十进制 / 负数同样拒绝
        for bad in ("UG_", "UG_12A", "UG_-1", "UG_0x10", "UG_ 12"):
            self.assertEqual(self.run_cmd(bad), "[RX-ERR]", bad)

    # ------------------------------------------------------------------ #
    # 20 / 21. 未初始化
    # ------------------------------------------------------------------ #

    def test_20_ug_on_blank_card(self) -> None:
        self.assertFalse(self.machine.meta.initialized)
        self.assertEqual(self.run_cmd("UG_100"), "[SET_ERR]")

    def test_21_ugs_on_blank_card(self) -> None:
        self.assertFalse(self.machine.meta.initialized)
        self.assertEqual(self.run_cmd("UGS"), "[SET_ERR]")

    # ------------------------------------------------------------------ #
    # 22. UG 不影响其他元数据
    # ------------------------------------------------------------------ #

    def test_22_ug_keeps_other_metadata(self) -> None:
        self.init_card()
        before_info = self.run_cmd("GET_!")
        before_meta = bytes(self.machine.meta.data[:0x2C])

        self.assertEqual(self.run_cmd("UG_4096"), "[UG-OK]")

        self.assertEqual(self.run_cmd("GET_!"), before_info)
        self.assertEqual(bytes(self.machine.meta.data[:0x2C]), before_meta)
        # 检测区与初始化标志不受影响
        self.assertEqual(self.machine.meta.data[0xF6:0x100], b"\x00" * 10)
        self.assertEqual(self.machine.meta.init_flag, 0x01)
        # 后续 SET 也不会覆盖已使用空间
        self.assertEqual(self.run_cmd("SET_DEV2_!_!_!_!_!_!_!_!"), "[SET-OK]")
        self.assertEqual(self.run_cmd("UGS"), "[UG-4096]")

    # ------------------------------------------------------------------ #
    # 25. 掩码 / 数据包统一十六进制
    # ------------------------------------------------------------------ #

    def test_25_hex_masks_and_packets(self) -> None:
        self.init_card()

        info = self.run_cmd("GET_!")
        fields = info.strip("<>").split("_")
        self.assertEqual(fields[2], "00", "WP 掩码应为 2 位十六进制")
        self.assertEqual(fields[3], "FF", "健康掩码应为 2 位十六进制")

        def set_cmd(wp: str, health: str) -> str:
            return "SET_" + "_".join(["!", wp, health, "!", "!", "!", "!", "!", "!"])

        # 十六进制掩码写入
        self.assertEqual(self.run_cmd(set_cmd("F0", "0F")), "[SET-OK]")
        self.assertEqual(self.machine.meta.wp_mask, 0xF0)
        self.assertEqual(self.machine.meta.health_mask, 0x0F)

        # 兼容旧版 8 位二进制写法
        self.assertEqual(self.run_cmd(set_cmd("00001111", "11111111")), "[SET-OK]")
        self.assertEqual(self.machine.meta.wp_mask, 0x0F)
        self.assertEqual(self.machine.meta.health_mask, 0xFF)

        # 非法的掩码
        for bad in ("FFF", "GG", "1", ""):
            self.assertEqual(self.run_cmd(set_cmd(bad, "FF")), "[SET_ERR]", bad)

        # 读包为 2 位大写十六进制
        self.assertEqual(self.run_cmd("ROUTER_0x50"), "[RX-OK]")
        self.assertEqual(self.run_cmd("ADDR_0x000F"), "[RX-OK]")
        self.io.inbox.append("RX-OK")
        self.io.clear()
        self.machine.handle("INT_R")

        packets = self.io.packets()
        self.assertEqual(len(packets), 1)
        tokens = packets[0][3:-1].split(",")
        self.assertEqual(len(tokens), 16)
        self.assertTrue(all(len(t) == 2 and t == t.upper() for t in tokens), packets[0])

    # ------------------------------------------------------------------ #
    # 26. 写包必须是十六进制
    # ------------------------------------------------------------------ #

    def test_26_write_packet_rejects_decimal(self) -> None:
        self.init_card()
        self.prepare_write(last_addr="0x001F")

        # 三位十进制不再被接受
        self.assertEqual(self.run_cmd("W_255,255,255"), "[RX-ERR]")
        # 十六进制可写
        self.assertEqual(self.run_cmd("W_FF,FE,01"), "[RX-OK]")
        self.assertEqual(self.machine.chips[0x50].read(0, 3), bytes([0xFF, 0xFE, 0x01]))

    # ------------------------------------------------------------------ #
    # 23. 映射边界：0x2C~0x31 / 0x32~0xF5 / 0xF6~0xFF
    # ------------------------------------------------------------------ #

    def test_23_used_space_layout_bounds(self) -> None:
        # 0x2C 之后被 DI 的哈希 / 次数占用，完整分段校验见 test_33
        from capsa_devices import META_HASH, META_USED

        self.assertEqual(META_USED, (0x2C, 6))
        self.assertEqual(META_HASH, (0x32, 8))
        self.assertEqual(META_USED[0] + META_USED[1], META_HASH[0])

    # ------------------------------------------------------------------ #
    # 24. 型号变更后 UG 上界随之改变
    # ------------------------------------------------------------------ #

    def test_24_ug_limit_follows_chip_model(self) -> None:
        self.init_card()
        self.assertEqual(self.run_cmd("SET_!_!_!_!_256_!_!_!_!"), "[SET-OK]")

        # 8 × 24C256 = 8 × 32768 = 262144 B
        self.assertEqual(self.machine.total_capacity_bytes, 262144)
        self.assertEqual(self.run_cmd("UG_262144"), "[UG-OK]")
        self.assertEqual(self.run_cmd("UG_262145"), "[RX-ERR]")

    # ------------------------------------------------------------------ #
    # 27 ~ 33. DI / DIG（数据哈希 + 累计写入次数）
    # ------------------------------------------------------------------ #

    def test_27_dig_after_first_set_is_zero(self) -> None:
        self.init_card()

        self.assertEqual(self.run_cmd("DIG"), "[DIG-0000000000000000-0]")
        self.assertEqual(self.machine.meta.data[0x32:0x3E], b"\x00" * 12)

    def test_28_di_sets_hash_and_count(self) -> None:
        self.init_card()

        self.assertEqual(self.run_cmd("DI_A1B2C3D4E5F60708_1"), "[DI-OK]")
        self.assertEqual(self.run_cmd("DIG"), "[DIG-A1B2C3D4E5F60708-1]")

        # bin 里的 0x32 ~ 0x3D 必须一致
        raw = (self.data_dir / "meta_24c02.bin").read_bytes()
        self.assertEqual(raw[0x32:0x3A].hex().upper(), "A1B2C3D4E5F60708")
        self.assertEqual(int.from_bytes(raw[0x3A:0x3E], "little"), 1)

    def test_29_di_partial_fields(self) -> None:
        self.init_card()
        self.assertEqual(self.run_cmd("DI_A1B2C3D4E5F60708_1"), "[DI-OK]")

        # 只改次数
        self.assertEqual(self.run_cmd("DI_!_2"), "[DI-OK]")
        self.assertEqual(self.run_cmd("DIG"), "[DIG-A1B2C3D4E5F60708-2]")

        # 只改哈希
        self.assertEqual(self.run_cmd("DI_1122334455667788_!"), "[DI-OK]")
        self.assertEqual(self.run_cmd("DIG"), "[DIG-1122334455667788-2]")

        # 都不改
        self.assertEqual(self.run_cmd("DI_!_!"), "[DI-OK]")
        self.assertEqual(self.run_cmd("DIG"), "[DIG-1122334455667788-2]")

    def test_30_di_on_blank_card(self) -> None:
        self.assertFalse(self.machine.meta.initialized)
        self.assertEqual(self.run_cmd("DI_A1B2C3D4E5F60708_1"), "[SET_ERR]")
        self.assertEqual(self.run_cmd("DIG"), "[SET_ERR]")

    def test_31_di_validation(self) -> None:
        self.init_card()

        for bad in ("DI_A1B2C3D4E5F6070_1",       # 15 位
                    "DI_A1B2C3D4E5F6070809_1",    # 17 位
                    "DI_ZZB2C3D4E5F60708_1",      # 非十六进制
                    "DI_A1B2C3D4E5F60708_4294967296",  # 超出 uint32
                    "DI_A1B2C3D4E5F60708_-1",
                    "DI_A1B2C3D4E5F60708",
                    "DI_A1B2C3D4E5F60708_1_2"):
            self.assertEqual(self.run_cmd(bad), "[RX-ERR]", bad)

    def test_32_di_keeps_other_metadata(self) -> None:
        self.init_card()
        before = self.run_cmd("GET_!")
        before_meta = bytes(self.machine.meta.data[:0x2C])

        self.assertEqual(self.run_cmd("DI_1122334455667788_7"), "[DI-OK]")

        self.assertEqual(self.run_cmd("GET_!"), before)
        self.assertEqual(bytes(self.machine.meta.data[:0x2C]), before_meta)
        # 检测区不受影响
        self.assertEqual(self.machine.meta.data[0xF6:0x100], b"\x00" * 10)

    def test_33_new_layout_bounds(self) -> None:
        from capsa_devices import META_DETECT, META_HASH, META_RESERVED, META_USED, META_WRITE_COUNT

        self.assertEqual(META_USED, (0x2C, 6))
        self.assertEqual(META_HASH, (0x32, 8))
        self.assertEqual(META_WRITE_COUNT, (0x3A, 4))
        self.assertEqual(META_RESERVED, (0x3E, 184))
        self.assertEqual(META_DETECT, (0xF6, 10))

        # 段落必须首尾相接、无空洞
        self.assertEqual(META_USED[0] + META_USED[1], META_HASH[0])
        self.assertEqual(META_HASH[0] + META_HASH[1], META_WRITE_COUNT[0])
        self.assertEqual(META_WRITE_COUNT[0] + META_WRITE_COUNT[1], META_RESERVED[0])
        self.assertEqual(META_RESERVED[0] + META_RESERVED[1], META_DETECT[0])

    # ------------------------------------------------------------------ #
    # 34 ~ 35. 初始化不得清除累计写入次数（EEPROM 寿命指标）
    # ------------------------------------------------------------------ #

    def test_34_reinit_preserves_write_count(self) -> None:
        # 首次初始化 → 记录写入
        self.init_card()
        self.assertEqual(self.run_cmd("DI_A1B2C3D4E5F60708_7"), "[DI-OK]")
        self.assertEqual(self.run_cmd("DIG"), "[DIG-A1B2C3D4E5F60708-7]")

        # 对已初始化卡重新初始化（后续 SET 走 update 路径）
        self.init_card("DEV2_00_FF_64_64_1700000200_1700000300_0_A1B2C3D4E5F6")
        self.assertEqual(self.run_cmd("DIG"), "[DIG-A1B2C3D4E5F60708-7]")

        # 即使检测区回到“未初始化”态（0xF6~0xFF 全 0xFF），first_set 也不能
        # 把已有的寿命计数抹掉
        self.machine.meta.data[0xF6:0x100] = b"\xFF" * 10
        self.assertFalse(self.machine.meta.initialized)
        self.init_card()
        self.assertEqual(self.run_cmd("DIG"), "[DIG-A1B2C3D4E5F60708-7]")

        # 已使用空间仍按初始化语义归零
        self.assertEqual(self.machine.meta.used_bytes, 0)

    def test_35_blank_card_di_is_zeroed(self) -> None:
        # 全新 EEPROM 是 0xFF：首次初始化必须把 DI 区规范成 0，否则
        # DIG 会读出 hash=FFFFFFFFFFFFFFFF / count=4294967295 这种垃圾值
        self.init_card()
        self.assertEqual(self.run_cmd("DIG"), "[DIG-0000000000000000-0]")



if __name__ == "__main__":
    unittest.main(verbosity=2)
