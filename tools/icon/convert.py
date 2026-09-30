#!/usr/bin/env python3
"""
Turns the PPM renders of render.swift into the BMPs pilrc needs, one per colour
depth: 1 bpp (black/white), 2 and 4 bpp (grey), 8 bpp (256 colours, own
palette), 16 bpp (RGB 5-6-5 from a 24-bit BMP).

    python3 tools/icon/convert.py build/icon src/icon
"""
import struct
import sys


def read_ppm(path):
    data = open(path, "rb").read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    pix = parts[3]
    return w, h, [tuple(pix[i:i + 3]) for i in range(0, len(pix), 3)]


def lum(c):
    return (0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]) / 255


def write_bmp(path, w, h, bpp, index_rows=None, palette=None, rgb_rows=None):
    """Bottom-up BMP: indexed (palette) or 24-bit."""
    if bpp == 24:
        stride = (w * 3 + 3) // 4 * 4
        body = bytearray()
        for row in reversed(rgb_rows):
            line = bytearray()
            for r, g, b in row:
                line += bytes((b, g, r))
            body += line + bytes(stride - len(line))
        pal = b""
    else:
        stride = ((w * bpp + 7) // 8 + 3) // 4 * 4
        body = bytearray()
        for row in reversed(index_rows):
            line = bytearray((w * bpp + 7) // 8)
            for x, v in enumerate(row):
                bit = x * bpp
                line[bit // 8] |= v << (8 - bpp - bit % 8)
            body += line + bytes(stride - len(line))
        pal = b"".join(bytes((b, g, r, 0)) for r, g, b in palette)
    off = 14 + 40 + len(pal)
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", off + len(body), 0, 0, off))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, h, 1, bpp, 0, len(body), 2835, 2835,
                            len(palette) if palette else 0, len(palette) if palette else 0))
        f.write(pal)
        f.write(body)


def grey_levels(pixels, w, h, bpp, name_path):
    n = 1 << bpp
    palette = [(round(255 * i / (n - 1)),) * 3 for i in range(n)]
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            row.append(min(n - 1, int(lum(pixels[y * w + x]) * (n - 1) + 0.5)))
        rows.append(row)
    write_bmp(name_path, w, h, bpp, index_rows=rows, palette=palette)


def mono(pixels, w, h, path):
    rows = [[0 if lum(pixels[y * w + x]) > 0.62 else 1 for x in range(w)] for y in range(h)]
    # palette index 1 = black, 0 = white
    write_bmp(path, w, h, 1, index_rows=rows, palette=[(255, 255, 255), (0, 0, 0)])
    return rows


def palette256(pixels, w, h, path):
    """<= 256 colours: exact if the image has that few, else popularity."""
    counts = {}
    for p in pixels:
        counts[p] = counts.get(p, 0) + 1
    colors = sorted(counts, key=lambda c: -counts[c])[:256]
    if (255, 255, 255) not in colors:
        colors[-1] = (255, 255, 255)
    colors.sort(key=lambda c: (-(c == (255, 255, 255)), lum(c)), reverse=False)
    index = {c: i for i, c in enumerate(colors)}

    def nearest(c):
        if c in index:
            return index[c]
        return min(range(len(colors)),
                   key=lambda i: sum((a - b) ** 2 for a, b in zip(c, colors[i])))

    rows = [[nearest(pixels[y * w + x]) for x in range(w)] for y in range(h)]
    write_bmp(path, w, h, 8, index_rows=rows, palette=colors + [(0, 0, 0)] * (256 - len(colors)))
    return len(counts)


def color16(pixels, w, h, path):
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b = pixels[y * w + x]
            row.append((r & 0xF8 | r >> 5, g & 0xFC | g >> 6, b & 0xF8 | b >> 5))
        rows.append(row)
    write_bmp(path, w, h, 24, rgb_rows=rows)


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    for name in ("large-72", "large-144", "small-72", "small-144"):
        w, h, px = read_ppm(f"{src}/{name}.ppm")
        density = name.split("-")[1]
        base = f"{dst}-{name}"
        if density == "72":                  # 1 bpp is drawn by hand (tools/make_icon.py)
            grey_levels(px, w, h, 4, base + "-4.bmp")      # pilrc takes no 2 bpp
        n = palette256(px, w, h, base + "-8.bmp")
        if density == "144":                 # only double density uses 16 bit
            color16(px, w, h, base + "-16.bmp")
        print(f"{name}: {w}x{h}, {n} distinct colours")
