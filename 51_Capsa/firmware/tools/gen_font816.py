#!/usr/bin/env python3
"""gen_font816.py - 生成 8x16 ASCII 点阵字库（C51 code 段）

原 ssd13xx.c 里的 SSD13xx_Font16x16 全是 0x00（占位空表），不能用于 OLED。
这里用 Consolas 渲染 95 个可打印字符，二值化成 8x16 位图，输出 C 数组。

排列：每个字符 16 字节，前 8 字节 = 上半页（y 0~7），后 8 字节 = 下半页（y 8~15）。
每字节是一列 8 个像素，bit0 = 最上面一行（SSD1315 页内 LSB 在上）。

用法：python tools/gen_font816.py firmware/hardware/font8x16.h
"""

import sys

from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "C:/Windows/Fonts/consola.ttf"
SIZE = 15
W, H = 8, 16
FIRST, LAST = 32, 126


def render(ch: str) -> list[int]:
    """返回 16 字节的列数据。"""
    img = Image.new("L", (W, H), 0)
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT_PATH, SIZE)

    # 水平居中；基线放在 y=13 附近，使字母主体落在 1~14 行
    try:
        left, top, right, bottom = draw.textbbox((0, 0), ch, font=font)
        tw = right - left
        th = bottom - top
    except Exception:
        left = top = tw = th = 0

    x = (W - tw) // 2 - left
    y = (H - th) // 2 - top
    draw.text((x, y), ch, fill=255, font=font)

    px = img.load()
    out = []
    for col in range(W):
        b = 0
        for row in range(8):
            if px[col, row] > 110:
                b |= 1 << row
        out.append(b)
    for col in range(W):
        b = 0
        for row in range(8, 16):
            if px[col, row] > 110:
                b |= 1 << (row - 8)
        out.append(b)
    return out


def main() -> None:
    out_path = sys.argv[1] if len(sys.argv) > 1 else "font8x16.h"
    chars = [chr(c) for c in range(FIRST, LAST + 1)]

    lines = []
    lines.append("/* font8x16.h - 8x16 ASCII 点阵（Consolas %dpx, 由 tools/gen_font816.py 生成）" % SIZE)
    lines.append(" * 每字符 16 字节：前 8 = 上半页，后 8 = 下半页；每字节一列，bit0 在上。")
    lines.append(" * 索引 = ASCII - 32 */")
    lines.append("#ifndef __FONT8X16_H__")
    lines.append("#define __FONT8X16_H__")
    lines.append("")
    lines.append("const unsigned char code Font8x16[%d][16] = {" % len(chars))

    for ch in chars:
        data = render(ch)
        rows = ", ".join("0x%02X" % b for b in data)
        label = ch if ch != "\\" else "backslash"
        label = label if label != '"' else "quote"
        lines.append("    { %s }, // %s" % (rows, label))

    lines.append("};")
    lines.append("")
    lines.append("#endif /* __FONT8X16_H__ */")

    text = "\n".join(lines) + "\n"
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    # 控制台预览几个字符，确认不是空白
    for ch in "AR0%":
        data = render(ch)
        print("--- %s ---" % ch)
        for page in range(2):
            for row in range(8):
                line = "".join("#" if (data[page * 8 + col] >> row) & 1 else "." for col in range(8))
                print(line)

    print("wrote", out_path, len(chars), "chars")


if __name__ == "__main__":
    main()
