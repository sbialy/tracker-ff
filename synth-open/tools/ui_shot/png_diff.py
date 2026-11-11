#!/usr/bin/env python3
"""png_diff.py A.png B.png: bounding box of the pixels that differ between two ui_shot frames as "x0 y0 x1 y1" (inclusive), "none" when equal.
Plain zlib decode of an 8 bit non-interlaced PNG (what ui_shot writes), no imaging library."""
import struct
import sys
import zlib


def load(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
    w, h, depth, ctype, _, _, interlace = ihdr
    assert depth == 8 and interlace == 0 and ctype in (0, 2, 4, 6), "unsupported PNG"
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        ft = raw[y * (stride + 1)]
        cur = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                cur[i] = (cur[i] + a) & 255
            elif ft == 2:
                cur[i] = (cur[i] + b) & 255
            elif ft == 3:
                cur[i] = (cur[i] + ((a + b) >> 1)) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[i] = (cur[i] + pred) & 255
        rows.append(cur)
        prev = cur
    return w, h, bpp, rows


def main():
    wa, ha, bpp, ra = load(sys.argv[1])
    wb, hb, _, rb = load(sys.argv[2])
    assert (wa, ha) == (wb, hb), "sizes differ"
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    for y in range(ha):
        if ra[y] == rb[y]:
            continue
        for x in range(wa):
            if ra[y][x * bpp:(x + 1) * bpp] != rb[y][x * bpp:(x + 1) * bpp]:
                x0, x1 = min(x0, x), max(x1, x)
                y0, y1 = min(y0, y), max(y1, y)
    print("none" if x1 < 0 else "%d %d %d %d" % (x0, y0, x1, y1))


main()
