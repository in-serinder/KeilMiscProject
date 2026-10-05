"""端到端测试：用 SubprocessHost 驱动模拟下位机。

运行：
    python -m tests.test_sim
或：
    python tests/test_sim.py
"""
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.host_client import SubprocessHost  # noqa: E402

PASS = 0
FAIL = 0
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def check(name: str, cond: bool, detail: str = "") -> None:
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name} {detail}")


def new_host(tmpdir: str, extra: list[str] | None = None) -> SubprocessHost:
    return SubprocessHost(
        ["--data-dir", tmpdir, "--rxok-timeout", "2.0", *(extra or [])],
        timeout=3.0, cwd=ROOT)


# ---------- 测试用例 ----------

def t1_roundtrip_0x50(tmp: str) -> None:
    print("T1: ROUTER 0x50 (24C01) 写 0x00~0x7F 读回")
    h = new_host(tmp)
    assert h.router(0x50) == "R-S"
    assert h.edge(0x7F) == "R-S"
    assert h.int_w() == "R-S"
    data = [(i * 3 + 1) & 0xFF for i in range(128)]
    h.write_stream(data)
    # 读回前重新建立会话（TX-OVER 后回到 IDLE）
    assert h.router(0x50) == "R-S"
    assert h.edge(0x7F) == "R-S"
    assert h.int_r() == "R-S"
    got = h.read_all()
    check("数据一致", got[:128] == data)
    h.close()


def t2_roundtrip_0x53(tmp: str) -> None:
    print("T2: ROUTER 0x53 (24C64) 写 0x0000~0x001F 读回")
    h = new_host(tmp)
    assert h.router(0x53) == "R-S"
    assert h.edge(0x001F) == "R-S"
    assert h.int_w() == "R-S"
    data = [0xA0 + i for i in range(32)]
    h.write_stream(data)
    assert h.router(0x53) == "R-S"
    assert h.edge(0x001F) == "R-S"
    assert h.int_r() == "R-S"
    got = h.read_all()
    check("数据一致", got[:32] == data)
    h.close()


def t3_roundtrip_0x57(tmp: str) -> None:
    print("T3: ROUTER 0x57 (24C1024) 写 0x00000~0x0001F 读回")
    h = new_host(tmp)
    assert h.router(0x57) == "R-S"
    assert h.edge(0x001F) == "R-S"
    assert h.int_w() == "R-S"
    data = [0x10 + i for i in range(32)]
    h.write_stream(data)
    assert h.router(0x57) == "R-S"
    assert h.edge(0x001F) == "R-S"
    assert h.int_r() == "R-S"
    got = h.read_all()
    check("数据一致", got[:32] == data)
    h.close()


def t4_short_last_packet(tmp: str) -> None:
    print("T4: 最后一包不足 16 字节，EDGE=0x000A 补 0x00")
    h = new_host(tmp)
    assert h.router(0x53) == "R-S"
    assert h.edge(0x000A) == "R-S"
    assert h.int_w() == "R-S"
    data = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
    h.write_stream(data)
    assert h.router(0x53) == "R-S"
    assert h.edge(0x000A) == "R-S"
    assert h.int_r() == "R-S"
    # 读回：第一包 11 有效 + 5 个补 0
    frame = h._read_frame()
    inner = frame[frame.find("[") + 1:frame.rfind("]")]
    pkt = [int(x, 16) for x in inner.split(",")]
    check("包长=16", len(pkt) == 16, f"len={len(pkt)}")
    check("有效 11 字节正确", pkt[:11] == data, f"{pkt[:11]}")
    check("后 5 字节补 0", pkt[11:] == [0, 0, 0, 0, 0], f"{pkt[11:]}")
    h.rx_ok()
    over = h._read_frame()
    check("收到 TX-OVER", over == "TX-OVER", over)
    h.close()


def t5_edge_oob_0x50(tmp: str) -> None:
    print("T5: ROUTER 0x50 时 EDGE=0x0080 越界 -> R-ERR")
    h = new_host(tmp)
    assert h.router(0x50) == "R-S"
    check("EDGE 越界 R-ERR", h.edge(0x0080) == "R-ERR")
    h.close()


def t6_edge_oob_0x57(tmp: str) -> None:
    print("T6: ROUTER 0x57 EDGE=0x20000 越界 -> R-ERR")
    h = new_host(tmp)
    assert h.router(0x57) == "R-S"
    check("EDGE 越界 R-ERR", h.edge(0x20000) == "R-ERR")
    h.close()


def t7_edge_without_router(tmp: str) -> None:
    print("T7: 未发 ROUTER 直接发 EDGE -> R-ERR")
    h = new_host(tmp)
    check("无 ROUTER 发 EDGE", h.edge(0x0010) == "R-ERR")
    h.close()


def t8_w_without_int_w(tmp: str) -> None:
    print("T8: 未发 INT_W 直接发 W_ -> R-ERR")
    h = new_host(tmp)
    assert h.router(0x50) == "R-S"
    assert h.edge(0x000F) == "R-S"
    body = "W_[" + ",".join("00" for _ in range(16)) + "]"
    check("未 INT_W 发 W_", h.cmd(body) == "R-ERR")
    h.close()


def t9_read_rxok_timeout(tmp: str) -> None:
    print("T9: 读流程不回 RX-OK，2s 后回 R-ERR")
    h = new_host(tmp)
    assert h.router(0x50) == "R-S"
    assert h.edge(0x007F) == "R-S"
    assert h.int_r() == "R-S"
    frame = h._read_frame()
    check("首先收到数据包", frame.startswith("R_["), frame)
    # 故意不回 RX-OK
    t0 = time.monotonic()
    err = h._read_frame(timeout=4.0)
    dt = time.monotonic() - t0
    check("超时后回 R-ERR", err == "R-ERR", err)
    check("约 2s 超时", 1.6 < dt < 3.0, f"dt={dt:.2f}s")
    h.close()


def t10_write_cycle_and_bin(tmp: str) -> None:
    print("T10: 写包后存在写周期等待；默认延迟落盘，TX-OVER 后 bin 才更新")
    # 用 100ms 的写周期放大信号，避免系统调度抖动导致误判；
    # 若写周期被忽略则总耗时仅约 I2C 传输时间(~0.8ms)。
    h = new_host(tmp, extra=["--write-cycle", "100"])
    assert h.router(0x53) == "R-S"
    assert h.edge(0x000F) == "R-S"
    assert h.int_w() == "R-S"
    bin_path = os.path.join(tmp, "eeprom_0x53_24C64.bin")
    # 记录写入前的旧内容（该 tmpdir 可能已被先前用例写过）
    with open(bin_path, "rb") as fh:
        old = list(fh.read(16))
    data = [0xE1 + i for i in range(16)]
    t0 = time.monotonic()
    rxok, rs = h.send_w(data)
    dt = time.monotonic() - t0
    check("先 RX-OK", rxok == "RX-OK", rxok)
    check("后 R-S", rs == "R-S", rs)
    check("含写周期等待", dt > 0.08, f"dt={dt*1000:.1f}ms")
    check("bin 文件存在", os.path.exists(bin_path))
    # 默认延迟落盘：W 包阶段 bin 应仍是旧内容，而非新数据
    with open(bin_path, "rb") as fh:
        before = list(fh.read(16))
    check("W 阶段 bin 尚未更新（延迟落盘）", before == old,
          f"before={before} old={old}")
    # TX-OVER 触发统一落盘
    check("TX-OVER 应答", h.tx_over() == "R-S")
    for _ in range(50):
        with open(bin_path, "rb") as fh:
            content = fh.read(16)
        if list(content) == data:
            break
        time.sleep(0.02)
    check("TX-OVER 后 bin 内容已更新", list(content) == data, f"{list(content)}")
    h.close()


def t21_deferred_flush_on_abort(tmp: str) -> None:
    print("T21: ABORT 时兜底落盘（保留内存已写数据）")
    h = new_host(tmp)
    assert h.router(0x53) == "R-S"
    assert h.edge(0x000F) == "R-S"
    assert h.int_w() == "R-S"
    data = [0xC3] * 16
    rxok, rs = h.send_w(data)
    check("写包 RX-OK", rxok == "RX-OK", rxok)
    check("写包 R-S", rs == "R-S", rs)
    # 不发 TX-OVER，直接 ABORT
    check("ABORT 应答", h.cmd("ABORT") == "R-S")
    bin_path = os.path.join(tmp, "eeprom_0x53_24C64.bin")
    for _ in range(50):
        with open(bin_path, "rb") as fh:
            content = fh.read(16)
        if list(content) == data:
            break
        time.sleep(0.02)
    check("ABORT 后 bin 已落盘", list(content) == data, f"{list(content)}")
    h.close()


def t22_flush_each_packet(tmp: str) -> None:
    print("T22: --flush-each-packet 时 W 包阶段即落盘")
    h = new_host(tmp, extra=["--flush-each-packet", "--no-i2c-sleep"])
    assert h.router(0x53) == "R-S"
    assert h.edge(0x000F) == "R-S"
    assert h.int_w() == "R-S"
    data = [0x77] * 16
    h.send_w(data)
    bin_path = os.path.join(tmp, "eeprom_0x53_24C64.bin")
    for _ in range(50):
        with open(bin_path, "rb") as fh:
            content = fh.read(16)
        if list(content) == data:
            break
        time.sleep(0.02)
    check("W 阶段 bin 已更新（每包落盘）", list(content) == data,
          f"{list(content)}")
    h.tx_over()
    h.close()


def t11_inject_fail(tmp: str) -> None:
    print("T11: 写失败注入 -> R-ERR 且地址不增加")
    h = new_host(tmp, extra=["--inject-fail", "0x0010"])
    assert h.router(0x53) == "R-S"
    assert h.edge(0x001F) == "R-S"
    assert h.int_w() == "R-S"
    # 第 1 包 (addr 0x00) 成功
    rxok, rs = h.send_w(list(range(16)))
    check("第1包 RX-OK", rxok == "RX-OK", rxok)
    check("第1包 R-S", rs == "R-S", rs)
    # 第 2 包 从 addr 0x10 开始 -> 注入失败
    rxok, rs = h.send_w(list(range(16, 32)))
    check("第2包 RX-OK", rxok == "RX-OK", rxok)
    check("第2包 R-ERR", rs == "R-ERR", rs)
    h.close()


def t12_bin_sizes(tmp: str) -> None:
    print("T12: 各 bin 文件自动生成且大小正确")
    empty = os.path.join(tmp, "sizecheck")
    os.makedirs(empty, exist_ok=True)
    h = new_host(empty)
    h.close()  # 触发加载即可，无需命令
    expect = {
        "eeprom_0x50_24C01.bin": 128,
        "eeprom_0x51_24C08.bin": 1024,
        "eeprom_0x52_24C16.bin": 2048,
        "eeprom_0x53_24C64.bin": 8192,
        "eeprom_0x54_24C128.bin": 16384,
        "eeprom_0x55_24C256.bin": 32768,
        "eeprom_0x56_24C512.bin": 65536,
        "eeprom_0x57_24C1024.bin": 131072,
    }
    for name, size in expect.items():
        path = os.path.join(empty, name)
        got = os.path.getsize(path) if os.path.exists(path) else -1
        check(f"{name} = {size}", got == size, f"got {got}")


def t13_router_bad(tmp: str) -> None:
    print("T13: ROUTER 地址越界 -> R-ERR")
    h = new_host(tmp)
    check("0x4F 非法", h.router(0x4F) == "R-ERR")
    check("0x58 非法", h.router(0x58) == "R-ERR")
    h.close()


def t14_bad_data_format(tmp: str) -> None:
    print("T14: 数据格式错误 -> R-ERR")
    h = new_host(tmp)
    assert h.router(0x50) == "R-S"
    assert h.edge(0x000F) == "R-S"
    assert h.int_w() == "R-S"
    check("非 hex",
          h.cmd("W_[ZZ,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00]") == "R-ERR")
    h.router(0x50)
    h.edge(0x000F)
    h.int_w()
    check("长度不足", h.cmd("W_[00,01,02]") == "R-ERR")
    h.close()


def t15_persistence_across_restart(tmp: str) -> None:
    print("T15: 重启后 bin 数据持久化")
    h = new_host(tmp)
    assert h.router(0x53) == "R-S"
    assert h.edge(0x000F) == "R-S"
    assert h.int_w() == "R-S"
    data = [0x5A] * 16
    h.send_w(data)
    h.tx_over()
    h.close()
    # 重新启动，读回
    h2 = new_host(tmp)
    assert h2.router(0x53) == "R-S"
    assert h2.edge(0x000F) == "R-S"
    assert h2.int_r() == "R-S"
    got = h2.read_all()
    check("重启后数据保留", got[:16] == data, f"{got[:16]}")
    h2.close()


def t16_type_echo(tmp: str) -> None:
    print("T16: TYPE_ECHO 查询工作设备 -> YES")
    h = new_host(tmp)
    check("空闲时 TYPE_ECHO", h.type_echo() == "YES")
    # 设置会话过程中也应能随时应答
    assert h.router(0x50) == "R-S"
    check("设置 ROUTER 后 TYPE_ECHO", h.type_echo() == "YES")
    # 不影响状态：仍可正常设置 EDGE 并读写
    assert h.edge(0x000F) == "R-S"
    check("TYPE_ECHO 后状态未受影响", h.int_w() == "R-S")
    h.close()


def t17_robust_unknown_frames(tmp: str) -> None:
    print("T17: 未知/畸形帧（如 >TYPE-ECHO<）不崩溃，回 R-ERR 且可继续工作")
    h = new_host(tmp)
    # 用户实际输入过的写法：带连字符
    check(">TYPE-ECHO< -> R-ERR", h.cmd("TYPE-ECHO") == "R-ERR")
    # 其他垃圾帧
    check("乱码 -> R-ERR", h.cmd("HELLO_WORLD") == "R-ERR")
    check("空帧 -> R-ERR", h.cmd("") == "R-ERR")
    check("仅下划线 -> R-ERR", h.cmd("_") == "R-ERR")
    check("W_ 无方括号 -> R-ERR", h.cmd("W_") == "R-ERR")
    check("EDGE 非法 -> R-ERR", h.cmd("EDGE_xyz") == "R-ERR")
    # 进程仍存活，能正常应答后续命令
    check("崩溃后仍可 ROUTER", h.router(0x50) == "R-S")
    check("崩溃后仍可 EDGE", h.edge(0x000F) == "R-S")
    check("崩溃后仍可 INT_W", h.int_w() == "R-S")
    check("崩溃后仍可 TYPE_ECHO", h.type_echo() == "YES")
    h.close()


def t18_robust_garbage_bytes(tmp: str) -> None:
    print("T18: 帧外垃圾字节/换行被忽略，合法帧仍可解析")
    from tools.host_client import SubprocessHost
    h = SubprocessHost(
        ["--data-dir", tmp, "--no-i2c-sleep"], timeout=3.0, cwd=ROOT)
    # 直接写入原始字节：帧外垃圾 + 换行 + 合法帧
    h._writer(b"garbage without delimiters\r\n")
    h._writer(b">ROUTER_0x50<")
    frame = h._read_frame()
    check("帧外垃圾被忽略，ROUTER 正常应答", frame == "R-S", frame)
    h._writer(b"\x00\x01 junk >TYPE_ECHO< trailing")
    check("帧内/外混合后 TYPE_ECHO", h._read_frame() == "YES")
    h.close()


def t19_crlf_separator(tmp: str) -> None:
    print("T19: 帧尾标准换行间隔 \\r\\n（发送含 \\r\\n，且解析容错）")
    from tools.host_client import SubprocessHost
    h = SubprocessHost(
        ["--data-dir", tmp, "--no-i2c-sleep"], timeout=3.0, cwd=ROOT)
    # 发送带 \r\n 的帧 -> 正常应答
    h._writer(b">TYPE_ECHO<\r\n")
    check("带 \\r\\n 的帧正常应答", h._read_frame() == "YES")
    # 连续多帧（每帧都带 \r\n）-> 逐一正常应答
    h._writer(b">ROUTER_0x53<\r\n>EDGE_0x001F<\r\n>INT_W<\r\n")
    check("连续多帧 1", h._read_frame() == "R-S")
    check("连续多帧 2", h._read_frame() == "R-S")
    check("连续多帧 3", h._read_frame() == "R-S")
    # 模拟下位机输出应包含 \r\n：读一帧后紧跟的回包也带 \r\n
    h.send_w([0] * 16)
    h.tx_over()
    # 仅用 \n（无 \r）作为间隔也应被接收方容错
    h._writer(b">TYPE_ECHO<\n")
    check("仅 \\n 间隔也可解析", h._read_frame() == "YES")
    h.close()


def t20_crlf_in_output(tmp: str) -> None:
    print("T20: 下位机发出帧尾确实带 \\r\\n")
    from tools.host_client import SubprocessHost
    h = SubprocessHost(
        ["--data-dir", tmp, "--no-i2c-sleep"], timeout=3.0, cwd=ROOT)
    h._writer(b">TYPE_ECHO<\r\n")
    # 直接读原始字节，验证 >YES< 后紧跟 \r\n
    raw = b""
    while not raw.endswith(b"\r\n"):
        b = h.proc.stdout.read(1)
        if not b:
            break
        raw += b
    check("输出以 >YES<\\r\\n 结尾", raw == b">YES<\r\n", raw)
    h.close()


def main() -> int:
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        t1_roundtrip_0x50(tmp)
        t2_roundtrip_0x53(tmp)
        t3_roundtrip_0x57(tmp)
        t4_short_last_packet(tmp)
        t5_edge_oob_0x50(tmp)
        t6_edge_oob_0x57(tmp)
        t7_edge_without_router(tmp)
        t8_w_without_int_w(tmp)
        t9_read_rxok_timeout(tmp)
        t10_write_cycle_and_bin(tmp)
        t11_inject_fail(tmp)
        t12_bin_sizes(tmp)
        t13_router_bad(tmp)
        t14_bad_data_format(tmp)
        t15_persistence_across_restart(tmp)
        t16_type_echo(tmp)
        t17_robust_unknown_frames(tmp)
        t18_robust_garbage_bytes(tmp)
        t19_crlf_separator(tmp)
        t20_crlf_in_output(tmp)
        t21_deferred_flush_on_abort(tmp)
        t22_flush_each_packet(tmp)
    print(f"\n==== {PASS} passed, {FAIL} failed ====")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
