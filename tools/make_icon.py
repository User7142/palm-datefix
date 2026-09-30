#!/usr/bin/env python3
"""
Draws the black-and-white launcher icons of DateFix by hand (pixel by pixel:
a threshold on the colour drawing loses the binder rings):
a calendar page showing 2032, the first year after the Palm OS limit.

    python3 tools/make_icon.py -> src/icon/icon-large-72-1.bmp (32x22),
                                  src/icon/icon-small-72-1.bmp (15x9)

The grey and colour versions come from tools/icon (render.swift, convert.py).
"""
import struct
import sys

DIGITS = {          # 3x5 pixel font
    "0": ["###", "#.#", "#.#", "#.#", "###"],
    "2": ["###", "..#", "###", "#..", "###"],
    "3": ["###", "..#", "###", "..#", "###"],
}


def text(canvas, s, x, y):
    for ch in s:
        for r, row in enumerate(DIGITS[ch]):
            for c, px in enumerate(row):
                if px == "#":
                    canvas[y + r][x + c] = 1
        x += 4


def blank(w, h):
    return [[0] * w for _ in range(h)]


def rect(canvas, x0, y0, x1, y1, fill=False):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if fill or y in (y0, y1) or x in (x0, x1):
                canvas[y][x] = 1


def badge(canvas, x, y):
    """7x7 black disc with a white check mark, a white halo clears the page."""
    disc = ["..###..", ".#####.", "#######", "#######", "#######", ".#####.", "..###.."]
    check = {(3, 1), (4, 2), (3, 3), (2, 4), (1, 5)}
    for r in range(-1, 8):
        for c in range(-1, 8):
            if (r - 3) ** 2 + (c - 3) ** 2 <= 4.6 ** 2 - 0.3 and 0 <= y + r < len(canvas):
                if 0 <= x + c < len(canvas[0]):
                    canvas[y + r][x + c] = 0
    for r, row in enumerate(disc):
        for c, ch in enumerate(row):
            if ch == "#" and (r, c) not in check and 0 <= y + r < len(canvas):
                canvas[y + r][x + c] = 1


def large():
    c = blank(32, 22)
    rect(c, 3, 3, 28, 21)                 # page
    rect(c, 3, 3, 28, 7, fill=True)       # header band
    rect(c, 8, 0, 9, 4, fill=True)        # binder rings
    rect(c, 22, 0, 23, 4, fill=True)
    c[4][8] = c[4][9] = c[4][22] = c[4][23] = 1
    text(c, "2032", 8, 12)
    badge(c, 24, 15)
    return c


def small():
    c = blank(15, 9)
    rect(c, 1, 0, 13, 8)
    rect(c, 1, 0, 13, 2, fill=True)
    text(c, "32", 4, 3)
    return c


def write_bmp(path, canvas):
    h, w = len(canvas), len(canvas[0])
    row_bytes = (((w + 7) // 8) + 3) // 4 * 4
    pixels = bytearray()
    for row in reversed(canvas):          # BMP rows run bottom-up
        bits = bytearray(row_bytes)
        for x, v in enumerate(row):
            if v:
                bits[x // 8] |= 0x80 >> (x % 8)
        pixels += bits
    palette = bytes([0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0])      # 0 = white, 1 = black
    offset = 14 + 40 + len(palette)
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", offset + len(pixels), 0, 0, offset))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, h, 1, 1, 0, len(pixels), 2835, 2835, 2, 2))
        f.write(palette)
        f.write(pixels)


if __name__ == "__main__":
    write_bmp("src/icon/icon-large-72-1.bmp", large())
    write_bmp("src/icon/icon-small-72-1.bmp", small())
    for name, canvas in (("large", large()), ("small", small())):
        print(name)
        for row in canvas:
            print("".join("#" if v else "." for v in row))
    sys.exit(0)
