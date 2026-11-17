#!/usr/bin/env python3
"""生成 z_elderly_care/include/lcd_font_zh.h 中文字库
   2x 超采样 + LANCZOS 缩放 + 低阈值，笔画完整"""
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "/mnt/c/Windows/Fonts/simhei.ttf"
OUTPUT = "/home/wzl/rk2206_ohos/vendor/lockzhiner/rk2206/samples/z_elderly_care/include/lcd_font_zh.h"

# UI 全部用到的汉字（去重）
CHARS = "温湿度体人气无有正常告警门磁摔倒血氧智慧养老监护系统守望星居传感节点打开关闭"

def render_char(font, ch, size, threshold=80):
    """2x 超采样渲染 + LANCZOS 缩放，保留笔画"""
    big = size * 2
    img = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(img)
    bbox = font.getbbox(ch)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x = (big - w) // 2 - bbox[0]
    y = (big - h) // 2 - bbox[1]
    draw.text((x, y), ch, fill=255, font=font)
    img = img.resize((size, size), Image.LANCZOS)

    pixels = img.load()
    bytes_per_row = (size + 7) // 8
    rows = []
    for row in range(size):
        row_bytes = [0] * bytes_per_row
        for col in range(size):
            if pixels[col, row] > threshold:
                row_bytes[col // 8] |= (1 << (col % 8))  # LSB = 左侧像素
        rows.append(row_bytes)
    return rows

def gen_table(font, size, type_name, var_name):
    bpr = (size + 7) // 8
    msk = size * bpr
    lines = []
    lines.append(f"/* {size}x{size} 中文点阵 */")
    lines.append("typedef struct")
    lines.append("{")
    lines.append("    unsigned char Index[3];")
    lines.append(f"    unsigned char Msk[{msk}];")
    lines.append(f"}} {type_name};")
    lines.append("")
    lines.append(f"static const {type_name} {var_name}[] =")
    lines.append("{")
    for ch in CHARS:
        rows = render_char(font, ch, size)
        vals = [f"\"{ch}\""]
        for row in rows:
            for b in row:
                vals.append(f"0x{b:02X}")
        lines.append("    " + ", ".join(vals) + f", /* {ch} */")
    lines.append("};")
    lines.append("")
    lines.append(f"#define {var_name.upper()}_COUNT  (sizeof({var_name}) / sizeof({var_name}[0]))")
    return "\n".join(lines)

def main():
    print(f"字符数: {len(CHARS)}  字符: {CHARS}")

    font16 = ImageFont.truetype(FONT_PATH, 18)  # 18pt -> 16x16
    font24 = ImageFont.truetype(FONT_PATH, 26)  # 26pt -> 24x24

    t16 = gen_table(font16, 16, "zh_font16_t", "tfont_zh16")
    t24 = gen_table(font24, 24, "zh_font24_t", "tfont_zh24")

    header = """/*
 * 智慧养老 · 中文字库 (z_elderly_care 自带)
 * 自动生成，勿手改 · tools/gen_font.py
 * 字体: SimHei · 2x 超采样 + LANCZOS + 阈值 80
 */
#ifndef LCD_FONT_ZH_H
#define LCD_FONT_ZH_H

#include <stdint.h>

"""
    footer = "\n#endif /* LCD_FONT_ZH_H */\n"

    with open(OUTPUT, "w", encoding="utf-8") as f:
        f.write(header + t16 + "\n\n" + t24 + "\n" + footer)

    print(f"✅ 已写入 {OUTPUT}")

if __name__ == "__main__":
    main()
