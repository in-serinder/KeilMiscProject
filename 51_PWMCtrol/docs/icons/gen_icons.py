#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PWMCtrol 图标生成器。

同一份几何定义同时产出：
  * SVG（矢量，可直接给设计用）
  * PNG（4x 超采样后 LANCZOS 降采样，保证小尺寸边缘干净）
  * ICO（Windows 应用图标，含 16/24/32/48/64/128/256 七档）

设计语言对齐上位机界面：
  底色 #171C23（面板色），描边 #2E3743，LED 红 #FF5533，琥珀 #E8B339，蓝 #3FA9F5
"""
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))

# ----------------------------------------------------------------- 配色
C_TILE = "#171C23"
C_TILE_EDGE = "#2E3743"
C_LED = "#FF5533"
C_AMBER = "#E8B339"
C_BLUE = "#3FA9F5"

S = 512          # 主画布
SS = 4           # 超采样倍数


# ------------------------------------------------------- 几何：方波折线
def wave_points(x0, x1, y_low, y_high, duty, periods):
    """返回方波的折线顶点，起点在 x0 的低电平处，先上升沿。"""
    period = (x1 - x0) / float(periods)
    high_len = period * duty / 100.0
    pts = [(x0, y_low)]
    x = x0
    while x < x1 - 0.5:
        xr = min(x + high_len, x1)
        pts.append((x, y_high))
        pts.append((xr, y_high))
        if xr >= x1 - 0.5:
            break
        pts.append((xr, y_low))
        xn = min(x + period, x1)
        pts.append((xn, y_low))
        x = xn
    return pts


def wave_points_duty(x0, x1, y_low, y_high, duties):
    """每个周期一个占空比，得到一条连续的"占空比递增"方波。"""
    n = len(duties)
    period = (x1 - x0) / float(n)
    pts = [(x0, y_low)]
    for i, duty in enumerate(duties):
        left = x0 + i * period
        hi = left + period * duty / 100.0
        pts.append((left, y_high))
        pts.append((hi, y_high))
        if i < n - 1:
            pts.append((hi, y_low))
            pts.append((left + period, y_low))
    pts.append((x1, y_high))       # 收尾：最后一周期的下降沿留在画面外
    return pts


def wave_rects(pts, width):
    """把折线展开成矩形列表（水平段 / 垂直段各一个矩形）。

    用矩形而不是 Pillow 的 line(joint=...)，是为了得到真正的直角拐点，
    和 SVG 的 stroke-linejoin="miter" 完全一致。
    """
    h = width / 2.0
    rects = []
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if abs(y1 - y0) < 0.01:                     # 水平段
            rects.append((min(x0, x1) - h, y0 - h, max(x0, x1) + h, y0 + h))
        else:                                        # 垂直段
            rects.append((x0 - h, min(y0, y1), x0 + h, max(y0, y1)))
    return rects


# ------------------------------------------------------------- SVG 拼装
def svg_header(w=S):
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
            'viewBox="0 0 %d %d">\n' % (w, w, S, S))


def polylines_svg(paths):
    out = []
    for pts, width, color in paths:
        d = " ".join("%.2f,%.2f" % p for p in pts)
        out.append('  <polyline points="%s" fill="none" stroke="%s" '
                   'stroke-width="%.1f" stroke-linejoin="miter" '
                   'stroke-linecap="butt"/>\n' % (d, color, width))
    return "".join(out)


def tile_svg(fill=C_TILE, edge=C_TILE_EDGE, r=112, edge_w=5):
    return ('  <rect x="0" y="0" width="%d" height="%d" rx="%d" fill="%s"/>\n'
            '  <rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="%.1f" '
            'fill="none" stroke="%s" stroke-width="%.1f"/>\n'
            % (S, S, r, fill,
               edge_w / 2.0, edge_w / 2.0, S - edge_w, S - edge_w,
               r - edge_w / 2.0, edge, edge_w))


# ---------------------------------------------------------- 位图渲染
def render(draw_fn, size=S):
    big = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    # 画布是 size*SS 像素，而几何坐标按 512 的设计稿写的 —— 换算比例是 size*SS/512，
    # 不是 SS。写错的话 16px 会按 512 坐标画到 64px 画布上，整块糊掉。
    draw_fn(d, size * SS, size * SS / float(S))
    return big.resize((size, size), Image.LANCZOS)


def rounded_tile(d, n, sc, fill=C_TILE, edge=C_TILE_EDGE, r=112, edge_w=5):
    d.rounded_rectangle([0, 0, n - 1, n - 1], radius=r * sc, fill=fill)
    if edge_w <= 0:                     # 小尺寸不描边：1px 的环在 16px 下只会糊成脏边
        return
    e = edge_w * sc
    d.rounded_rectangle([e / 2, e / 2, n - 1 - e / 2, n - 1 - e / 2],
                        radius=(r - edge_w / 2) * sc, outline=edge, width=max(1, int(e)))


def draw_paths(d, paths, sc=1.0):
    for pts, width, color in paths:
        for (a, b, c, e) in wave_rects(pts, width):
            d.rectangle([a * sc, b * sc, c * sc, e * sc], fill=color)


# =================================================== 方案 A：单条粗方波
def variant_a(stroke=34, periods=2.0, y_low=344, y_high=168, legend=True):
    pts = wave_points(80, 432, y_low, y_high, duty=50, periods=periods)
    paths = [(pts, stroke, C_LED)]
    # 底部三颗通道色点：大尺寸下是"三通道"图例，小尺寸自然消失，不干扰读数
    ticks = []
    if legend:
        for x, c in ((136, C_LED), (256, C_AMBER), (376, C_BLUE)):
            ticks.append(([(x - 11, 432), (x + 11, 432)], 22, c))

    svg = svg_header() + tile_svg() + polylines_svg(paths + ticks) + "</svg>\n"

    def draw(d, n, sc):
        rounded_tile(d, n, sc, edge_w=5 if n / float(SS) >= 32 else 0)
        draw_paths(d, paths + ticks, sc)

    return svg, draw


# ====================================== 方案 A2：一条线，三段不同占空比
def variant_a2():
    pts = wave_points_duty(76, 436, 344, 168, duties=(25, 50, 75))
    paths = [(pts, 34, C_LED)]

    svg = (svg_header() + tile_svg()
           + polylines_svg(paths) + "</svg>\n")

    def draw(d, n, sc):
        rounded_tile(d, n, sc)
        draw_paths(d, paths, sc)

    return svg, draw


# ================================================ 方案 B：三条通道波形
def variant_b():
    lanes = [
        (204, 34.0, C_LED),
        (286, 58.0, C_AMBER),
        (368, 76.0, C_BLUE),
    ]
    paths = []
    for y_low, duty, color in lanes:
        pts = wave_points(80, 432, y_low, y_low - 56, duty=duty, periods=2)
        paths.append((pts, 22, color))

    svg = svg_header() + tile_svg() + polylines_svg(paths) + "</svg>\n"

    def draw(d, n, sc):
        rounded_tile(d, n, sc)
        draw_paths(d, paths, sc)

    return svg, draw


# =============================================== 方案 C：旋钮环 + 内嵌波形
def variant_c():
    cx, cy, r = 256, 256, 168
    ring = ([[]], 0, "")
    # 环：用矩形近似会很难看，这里单独在 draw 里画 arc；SVG 用 circle + stroke-dasharray
    pts = wave_points(cx - 88, cx + 88, cy + 34, cy - 34, duty=50, periods=2)
    paths = [(pts, 26, C_LED)]

    svg = (svg_header() + tile_svg()
           + '  <circle cx="%d" cy="%d" r="%d" fill="none" stroke="%s" '
             'stroke-width="30"/>\n' % (cx, cy, r, C_TILE_EDGE)
           + '  <circle cx="%d" cy="%d" r="%d" fill="none" stroke="%s" '
             'stroke-width="30" stroke-linecap="round" '
             'stroke-dasharray="120 936" transform="rotate(-118 %d %d)"/>\n'
             % (cx, cy, r, C_LED, cx, cy)
           + polylines_svg(paths) + "</svg>\n")

    def draw(d, n, sc):
        rounded_tile(d, n, sc)
        w = 30 * sc
        d.ellipse([(cx - r) * sc, (cy - r) * sc, (cx + r) * sc, (cy + r) * sc],
                  outline=C_TILE_EDGE, width=int(w))
        d.arc([(cx - r) * sc, (cy - r) * sc, (cx + r) * sc, (cy + r) * sc],
              start=-118 - 30, end=-118, fill=C_LED, width=int(w))
        draw_paths(d, paths, sc)

    return svg, draw


# ---------------------------------------------------------------- 输出
def save(name, svg, draw, sizes=(16, 24, 32, 48, 64, 128, 256, 512)):
    with open(os.path.join(HERE, name + ".svg"), "w", encoding="utf-8") as f:
        f.write(svg)

    pngs = {}
    for s in sizes:
        img = render(draw, s)
        img.save(os.path.join(HERE, "%s-%d.png" % (name, s)))
        pngs[s] = img

    pngs[256].save(os.path.join(HERE, name + ".ico"), format="ICO",
                   sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
    return pngs


def _cjk_font(size):
    """PIL 默认位图字体没有中文字形，标签会变成方框；用系统雅黑。"""
    for name in ("msyh.ttc", "msyhbd.ttc", "simhei.ttf"):
        path = os.path.join("C:/Windows/Fonts", name)
        if os.path.exists(path):
            try:
                from PIL import ImageFont
                return ImageFont.truetype(path, size)
            except Exception:
                pass
    from PIL import ImageFont
    return ImageFont.load_default()


def contact_sheet(variants, out):
    """对比图：三个方向 × 五档尺寸 + 深/浅背景下的 32px 实际观感。"""
    sizes = (256, 128, 64, 32, 16)
    gap, pad, label_w, row_h = 24, 28, 92, 264
    w = label_w + sum(sizes) + gap * len(sizes) + pad
    h = pad * 2 + row_h * len(variants) + 108
    sheet = Image.new("RGB", (w, h), "#0B0E12")
    d = ImageDraw.Draw(sheet)
    font = _cjk_font(15)

    for row, (name, imgs) in enumerate(variants):
        y = pad + row * row_h
        d.text((pad, y + 110), name, fill="#8A97A8", font=font)
        x = label_w
        for s in sizes:
            thumb = imgs[s]
            sheet.paste(thumb, (x + (256 - s) // 2 if s == 256 else x,
                                y + (256 - s) // 2), thumb)
            d.text((x, y + 236), "%dpx" % s, fill="#4A5563", font=font)
            x += s + gap

    # 任务栏观感：深色 / 浅色两行
    y = pad + row_h * len(variants) + 8
    d.text((pad, y + 4), "任务栏", fill="#8A97A8", font=font)
    for i, (bg, fg) in enumerate((("#1F1F1F", "#111"), ("#F3F3F3", "#111"))):
        box = Image.new("RGB", (300, 44), bg)
        xx = 6
        for name, imgs in variants:
            box.paste(imgs[32], (xx, 6), imgs[32])
            xx += 38
        sheet.paste(box, (label_w, y + i * 52))
    sheet.save(out)
    return out


def finalize():
    """定稿：A' 纯波形（3 周期 / 50% 占空比 / 34px 线宽）作为应用图标。"""
    svg, draw = variant_a(legend=False)
    sizes = (16, 20, 24, 32, 40, 48, 64, 128, 256, 512)
    imgs = {}
    for sz in sizes:
        img = render(draw, sz)
        img.save(os.path.join(HERE, "app-%d.png" % sz))
        imgs[sz] = img

    with open(os.path.join(HERE, "app.svg"), "w", encoding="utf-8") as f:
        f.write(svg)

    imgs[256].save(os.path.join(HERE, "app.ico"), format="ICO",
                   sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])

    # 深浅背景下的实际观感（真机上任务栏可能是浅色主题）
    pad, row_h = 16, 156
    sheet = Image.new("RGB", (pad * 2 + 700, row_h * 2 + pad), "#0B0E12")
    f = _cjk_font(13)
    d = ImageDraw.Draw(sheet)
    for i, bg in enumerate(("#101216", "#F2F2F2")):
        y = pad + i * row_h
        d.rectangle([pad, y, pad + 684, y + row_h - 26], fill=bg)
        # 小尺寸沿基线排一行，像任务栏那样贴着看
        x = pad + 10
        base = y + 112
        for sz in (16, 20, 24, 32, 40, 48):
            sheet.paste(imgs[sz], (x, base - sz), imgs[sz])
            x += sz + 12
        sheet.paste(imgs[128], (pad + 480, y + 8), imgs[128])
        d.text((pad + 10, y + row_h - 22),
               "深色任务栏" if i == 0 else "浅色任务栏", fill="#8A97A8", font=f)
    sheet.save(os.path.join(HERE, "preview-bg.png"))

    print("finalized", os.path.join(HERE, "app.ico"))


def alternatives():
    """出备选方案图，方便日后回溯"为什么不是另外两个"。"""
    items = [("A 单波形（采用）", variant_a, dict(legend=False)),
             ("B 三通道泳道", variant_b, {}),
             ("C 旋钮+波形", variant_c, {})]
    f = _cjk_font(15)
    cell, pad = 268, 20
    im = Image.new("RGB", (pad + cell * 3 + pad, 320), "#0B0E12")
    d = ImageDraw.Draw(im)
    for i, (label, fn, kw) in enumerate(items):
        svg, draw = fn(**kw)
        x = pad + i * cell
        im.paste(render(draw, 240), (x, 44), render(draw, 240))
        d.text((x, 14), label, fill="#8A97A8", font=f)
    im.save(os.path.join(HERE, "alternatives.png"))


if __name__ == "__main__":
    alternatives()
    finalize()
