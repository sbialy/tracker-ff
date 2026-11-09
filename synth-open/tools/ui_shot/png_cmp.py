#!/usr/bin/env python3 -I
"""Host helper: compares the layout of two 800x480 PNG screenshots (no PIL on the Mac, so a small PNG reader).

  png_cmp.py --check FILE...      each file must be a readable 800x480 PNG with more than one colour (exit 1 otherwise)
  png_cmp.py A.png B.png          column/row positions of the bright separator lines in the bottom bar and the top bar underline,
                                  and the share of pixels that match exactly (only meaningful for the same screen and theme)
"""
import struct, sys, zlib


def read_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos, idat, w = 8, b'', None
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos + 8])
        c = d[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h, bd, ct, _, _, il = struct.unpack('>IIBBBBB', c)
            assert bd == 8 and il == 0 and ct in (2, 6), 'only 8 bit RGB or RGBA, not interlaced'
            bpp = 3 if ct == 2 else 4
        elif t == b'IDAT':
            idat += c
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def lum(rows, bpp, x, y):
    r = rows[y]
    return (r[x * bpp] + r[x * bpp + 1] + r[x * bpp + 2]) // 3


def separators(img, y0, y1):
    w, h, bpp, rows = img
    cols = []
    for x in range(w):
        # a vertical line: bright in most of the band, its neighbours are not
        n = sum(1 for y in range(y0, y1) if lum(rows, bpp, x, y) > 60)
        if n > (y1 - y0) * 0.6:
            cols.append(x)
    return cols


def underline(img, y0, y1):
    w, h, bpp, rows = img
    return [y for y in range(y0, y1) if sum(1 for x in range(w) if lum(rows, bpp, x, y) > 60) > w * 0.8]


if __name__ == '__main__':
    if sys.argv[1] == '--check':
        bad = 0
        for p in sys.argv[2:]:
            try:
                w, h, bpp, rows = read_png(p)
                colours = {bytes(r[x * bpp:x * bpp + 3]) for r in rows[::7] for x in range(0, w, 5)}
                if (w, h) != (800, 480) or len(colours) < 3:
                    raise ValueError('%dx%d, %d colours' % (w, h, len(colours)))
            except Exception as e:   # noqa: BLE001
                print('BAD  %s: %s' % (p, e))
                bad += 1
        sys.exit(1 if bad else 0)
    a, b = read_png(sys.argv[1]), read_png(sys.argv[2])
    for name, img in (('A', a), ('B', b)):
        print(name, '%dx%d' % (img[0], img[1]), 'top bar underline rows', underline(img, 20, 40), 'bottom bar separator columns', separators(img, 436, 476))
    if a[:2] == b[:2]:
        same = sum(1 for y in range(a[1]) if a[3][y] == b[3][y])
        print('identical rows: %d of %d' % (same, a[1]))
