"""以 Mock 模式驱动 Capsa 窗口：截图 / 点击，用于验证格式化流程。"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

from PIL import ImageGrab

user32 = ctypes.WinDLL("user32", use_last_error=True)


def find_window(title_part="Capsa", pid=None):
    result = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        length = user32.GetWindowTextLengthW(hwnd)
        if length == 0:
            return True
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)
        if title_part not in buf.value:
            return True
        if pid is not None:
            owner = wt.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value != pid:
                return True
        result.append((hwnd, buf.value))
        return True

    user32.EnumWindows(cb, 0)
    return result


SW_RESTORE = 9
SWP_NOZORDER = 0x0004
SWP_SHOWWINDOW = 0x0040


def restore(hwnd):
    user32.ShowWindow(hwnd, SW_RESTORE)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.6)


def resize(hwnd, x, y, w, h):
    user32.SetWindowPos(hwnd, 0, x, y, w, h, SWP_NOZORDER | SWP_SHOWWINDOW)
    time.sleep(0.5)


HWND_TOPMOST = -1
HWND_NOTOPMOST = -2


def bring_to_front(hwnd):
    user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, 0x0001 | 0x0002)
    user32.SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, 0x0001 | 0x0002)
    user32.BringWindowToTop(hwnd)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.4)


def rect(hwnd):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def shot(path, hwnd=None):
    if hwnd:
        l, t, r, b = rect(hwnd)
    else:
        l, t, r, b = 0, 0, *[int(v) for v in (user32.GetSystemMetrics(0), user32.GetSystemMetrics(1))]
    img = ImageGrab.grab(bbox=(l, t, r, b), all_screens=True)
    img.save(path)
    return img.size


def click(x, y, hwnd=None):
    if hwnd:
        l, t, _, _ = rect(hwnd)
        x, y = l + x, t + y
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    user32.SetCursorPos(int(x), int(y))
    time.sleep(0.1)
    user32.mouse_event(0x0002, 0, 0, 0, 0)   # left down
    user32.mouse_event(0x0004, 0, 0, 0, 0)   # left up
    time.sleep(0.2)


if __name__ == "__main__":
    pid = int(sys.argv[1]) if len(sys.argv) > 1 else None
    action = sys.argv[2] if len(sys.argv) > 2 else "shot"
    base = "F:/SYSFILE/DeskTop/Misc.Git/KeilMiscProject/51_Capsa/Capsa/"

    wins = find_window(pid=pid)
    if not wins:
        sys.exit("window not found")
    hwnd, title = wins[0]
    restore(hwnd)
    resize(hwnd, 10, 10, 1060, 820)

    if action == "connect":
        click(650, 103, hwnd)
        time.sleep(2.0)
    elif action == "format":
        # 工具栏 "Format card"
        click(872, 458, hwnd)
        time.sleep(0.8)
    elif action.startswith("click:"):
        _, x, y = action.split(":")
        click(int(x), int(y), hwnd)
        time.sleep(0.6)

    name = sys.argv[3] if len(sys.argv) > 3 else "docs_fmt_shot.png"
    bring_to_front(hwnd)
    print("size", shot(base + name, hwnd))
