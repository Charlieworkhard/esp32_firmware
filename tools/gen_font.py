"""生成 OLED 字库点阵 C 文件。

用法：
    python tools/gen_font.py [字体路径]

默认字体 C:\\Windows\\Fonts\\simhei.ttf，输出到 main/display/font_zh16.c。
字符集为 GB2312 全集，外加常用中文标点。
需要 Pillow：pip install pillow
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

DEFAULT_FONT = r"C:\Windows\Fonts\simhei.ttf"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "main", "display", "font_zh16.c")

ASCII_W, ASCII_H = 8, 16
HANZI_W, HANZI_H = 16, 16


def gb2312_chars():
    chars = set()
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                continue
    return chars


def glyph(ch, font, w, h):
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((0, 0), ch, font=font, fill=255)
    cols = []
    for x in range(w):
        for page in range(h // 8):
            byte = 0
            for bit in range(8):
                if img.getpixel((x, page * 8 + bit)) > 100:
                    byte |= 1 << bit
            cols.append(byte)
    return cols


def main():
    font_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_FONT
    font = ImageFont.truetype(font_path, 16)

    chars = gb2312_chars()
    chars.update("，。！？、：；（）—…“”‘’")
    ordered = sorted(chars, key=ord)
    print(f"glyphs: {len(ordered)}")

    ascii_rows = [
        "    {" + ",".join(f"0x{b:02x}" for b in glyph(chr(c), font, ASCII_W, ASCII_H)) + "},"
        for c in range(32, 127)
    ]
    hanzi_rows = [
        "    {" + ",".join(f"0x{b:02x}" for b in glyph(ch, font, HANZI_W, HANZI_H)) + "},"
        for ch in ordered
    ]

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write('#include "font_glyphs.h"\n\n')
        f.write("const uint8_t kAsciiFont[95][16] = {\n" + "\n".join(ascii_rows) + "\n};\n\n")
        f.write(f"const int kHanziCount = {len(ordered)};\n")
        f.write(
            "const uint32_t kHanziCodepoints[] = {\n    "
            + ",".join(f"0x{ord(c):04x}" for c in ordered)
            + ",\n};\n\n"
        )
        f.write("const uint8_t kHanziFont[][32] = {\n" + "\n".join(hanzi_rows) + "\n};\n")

    print(f"written {OUT}")


if __name__ == "__main__":
    main()
