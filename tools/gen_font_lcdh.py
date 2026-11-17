#!/usr/bin/env python3
"""生成 lcd_font.h 中文字库 - 高质量渲染"""
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "/mnt/c/Windows/Fonts/simhei.ttf"
LCD_FONT_H = "/home/wzl/rk2206_ohos/vendor/lockzhiner/rk2206/samples/b23_iot_lcd/include/lcd_font.h"

# 全部需要的汉字
CHARS = "小凌派温湿度体人气无有正常告警门磁摔倒血氧智慧养老监护系统守望星护居打开关闭"

def render_char(font, ch, size, threshold=80):
    """渲染字符 - 大字号+低阈值保留笔画"""
    # 用2倍画布渲染再缩放，细节更好
    big = size * 2
    img = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(img)
    bbox = font.getbbox(ch)
    w, h = bbox[2]-bbox[0], bbox[3]-bbox[1]
    x = (big - w) // 2 - bbox[0]
    y = (big - h) // 2 - bbox[1]
    draw.text((x, y), ch, fill=255, font=font)

    # 缩放到目标尺寸
    img = img.resize((size, size), Image.LANCZOS)

    pixels = img.load()
    bytes_per_row = (size + 7) // 8
    rows = []
    for row in range(size):
        row_bytes = [0] * bytes_per_row
        for col in range(size):
            if pixels[col, row] > threshold:
                byte_idx = col // 8
                bit_idx = col % 8  # LSB first
                row_bytes[byte_idx] |= (1 << bit_idx)
        rows.append(row_bytes)
    return rows

def gen_table(font, size, struct_name, var_name):
    bytes_per_row = (size + 7) // 8
    msk_size = size * bytes_per_row
    lines = []
    lines.append(f"/* 中文字符 {size}x{size} */")
    lines.append("typedef struct")
    lines.append("{")
    lines.append("    unsigned char Index[3];")
    lines.append(f"    unsigned char Msk[{msk_size}];")
    lines.append(f"}} {struct_name};")
    lines.append("")
    lines.append(f"const {struct_name} {var_name}[] =")
    lines.append("    {")

    for ch in CHARS:
        rows = render_char(font, ch, size)
        hex_vals = [f"\"{ch}\""]
        for row in rows:
            for b in row:
                hex_vals.append(f"0x{b:02X}")
        lines.append("        " + ", ".join(hex_vals) + f", /*\"{ch}\"*/")
        lines.append("")

    lines.append("    };")
    return "\n".join(lines)

def main():
    print(f"字符: {CHARS}")
    print(f"共 {len(CHARS)} 字")

    # 16x16: 用18pt渲染+LANCZOS缩放
    font16 = ImageFont.truetype(FONT_PATH, 18)
    # 24x24: 用26pt
    font24 = ImageFont.truetype(FONT_PATH, 26)
    # 32x32: 用34pt
    font32 = ImageFont.truetype(FONT_PATH, 34)
    # 12x12: 用14pt
    font12 = ImageFont.truetype(FONT_PATH, 14)

    tfont12 = gen_table(font12, 12, "typFNT_GB12", "tfont12")
    tfont16 = gen_table(font16, 16, "typFNT_GB16", "tfont16")
    tfont24 = gen_table(font24, 24, "typFNT_GB24", "tfont24")
    tfont32 = gen_table(font32, 32, "typFNT_GB32", "tfont32")

    # 读原文件保留 ASCII 部分
    with open(LCD_FONT_H, "r", encoding="utf-8") as f:
        original = f.read()

    marker = "/* 定义中文字符 12*12 */"
    idx = original.find(marker)
    if idx >= 0:
        head = original[:idx]
    else:
        # 保留到 ascii 表结束
        ascii_start = original.find("/* utf8格式汉字转化为ascii格式 */")
        ascii_end = original.find("/* 12*6的ASCII码显示 */")
        if ascii_start >= 0 and ascii_end >= 0:
            head = original[:original.find("/* 定义中文字符 12*12 */")] if original.find("/* 定义中文字符 12*12 */") > 0 else original[:ascii_end]
        else:
            head = "#ifndef _LCD_FONT_H_\n#define _LCD_FONT_H_\n\n#include <stdint.h>\n\n"

    new_content = head + "\n" + tfont12 + "\n\n" + tfont16 + "\n\n" + tfont24 + "\n\n" + tfont32 + "\n\n#endif /* _LCD_FONT_H_ */\n"

    with open(LCD_FONT_H, "w", encoding="utf-8") as f:
        f.write(new_content)

    print(f"✅ 已写入 {LCD_FONT_H}")

if __name__ == "__main__":
    main()
