# -*- coding: utf-8 -*-
"""PDL 应用图标生成器：深色圆角底 + 蓝色功率波形 + 橙色触发点（扁平风）。
输出 Assets/app-*.png 与 app.ico，几何与 Assets/app_icon.svg 一致。"""
import os
from PIL import Image, ImageDraw

OUT = r"F:\SYSFILE\DeskTop\Misc.Git\KeilMiscProject\STM32_PDL\PDL_upper\PDL_upper\Assets"
os.makedirs(OUT, exist_ok=True)

BG = (30, 33, 39, 255)        # #1E2127 卡片色
RING = (46, 51, 61, 255)      # #2E333D 描边
WAVE = (74, 158, 255, 255)    # #4A9EFF CH0 蓝
TRIG = (255, 176, 32, 255)    # #FFB020 触发橙

# 512 坐标系下的几何
RECT_R = 112
RING_W = 6
WAVE_W = 34
PTS = [(112, 320), (196, 320), (256, 196), (302, 320), (400, 320)]
DOT_R = 27


def render(size: int, ss: int = 4) -> Image.Image:
    """supersample 绘制再降采样，边缘平滑。"""
    S = size * ss
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def sc(v):  # 512 -> S
        return int(round(v * S / 512.0))

    # 底色圆角方块（内缩半个描边宽避免被裁）
    m = max(1, sc(RING_W / 2))
    d.rounded_rectangle([m, m, S - 1 - m, S - 1 - m], radius=sc(RECT_R),
                        fill=BG, outline=RING, width=max(1, sc(RING_W)))

    pts = [(sc(x), sc(y)) for x, y in PTS]
    w = max(2, sc(WAVE_W))
    d.line(pts, fill=WAVE, width=w, joint="curve")
    # 圆头端点
    for p in (pts[0], pts[-1]):
        rr = w // 2
        d.ellipse([p[0] - rr, p[1] - rr, p[0] + rr, p[1] + rr], fill=WAVE)

    # 触发点（峰值处橙色实心圆）
    cr = sc(DOT_R)
    cx, cy = pts[2]
    d.ellipse([cx - cr, cy - cr, cx + cr, cy + cr], fill=TRIG)

    return img.resize((size, size), Image.LANCZOS)


for sz in (512, 256, 128, 64, 48, 32, 16):
    render(sz).save(os.path.join(OUT, f"app-{sz}.png"))

# 多尺寸 .ico（Windows 用）
ico_src = render(256)
ico_src.save(os.path.join(OUT, "app.ico"),
             sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("icons written to", OUT)
