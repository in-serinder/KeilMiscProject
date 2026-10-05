"""交互式演示：启动模拟下位机子进程并执行一次写/读回放。

运行：
    python tools/demo.py
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.host_client import SubprocessHost  # noqa: E402


def main() -> int:
    data_dir = os.path.join(os.getcwd(), "eeprom_data")
    print("== 启动模拟下位机 (ROUTER 0x53 / 24C64) ==")
    h = SubprocessHost(
        ["--data-dir", data_dir, "--no-display"],
        timeout=3.0,
        cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    print("ROUTER_0x53 ->", h.router(0x53))
    print("EDGE_0x001F ->", h.edge(0x001F))
    print("INT_W       ->", h.int_w())
    data = [0x11 * (i + 1) & 0xFF for i in range(32)]
    h.write_stream(data)
    print("已写入 32 字节:", [f"{b:02X}" for b in data])

    # 写流程结束后下位机回到 IDLE，读回前重新建立会话
    print("ROUTER_0x53 ->", h.router(0x53))
    print("EDGE_0x001F ->", h.edge(0x001F))
    print("INT_R       ->", h.int_r())
    got = h.read_all()
    print("读回:", [f"{b:02X}" for b in got[:32]])
    print("一致:", got[:32] == data)

    h.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
