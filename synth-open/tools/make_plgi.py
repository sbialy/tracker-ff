#!/usr/bin/env python3
"""Make a synth plugin engine logo (.plgi, PLGI 1, synth-open/plug_logo.h). Pure Python, no Pillow.

    make_plgi.py --text JU6 -o ju6.plgi [--preview ju6.png]          a text logo in the stock style (pill, letters cut out)
    make_plgi.py --png logo.png -o x.plgi                             one PNG, scaled to both sizes (area average)
    make_plgi.py --mini m.png --med d.png -o x.plgi                   one PNG per size (scaled when not 80x29 / 108x40)
    make_plgi.py --from-h mini.h med.h -o x.plgi                      the C arrays of a stock logo (core/libs/images)
    make_plgi.py --check x.plgi [--preview x.png]                     check a record (or a .plg trailer), write a preview
    make_plgi.py --selftest                                           golden round trip of every stock logo pair

The logo is an alpha mask: white (or opaque, for a PNG with alpha) is drawn in the theme colour, black (or transparent) is
not drawn. Four levels (FT812 L2), pixel 0 in the top two bits of a byte, rows padded to whole bytes. The record is a 16 byte
header and the 80x29 list image then the 108x40 editor image, 1,676 bytes. Put it next to the engine as
/Plugins/Synth/<name>.plgi (it wins over a trailer), or into the .plg with build_plug.py --logo.
"""
import argparse
import os
import re
import struct
import sys
import zlib

MINI = (80, 29)
MED = (108, 40)
MAGIC = 0x49474C50   # "PLGI"
HEADER = 16


def stride(w):
    return (w + 3) // 4


PIXELS = stride(MINI[0]) * MINI[1] + stride(MED[0]) * MED[1]   # 1,660
RECORD = HEADER + PIXELS                                         # 1,676


# ------------------------------------------------------------------------------------------------ L2 packing
def pack_l2(levels, w, h):
    """levels: h rows of w values 0..3 -> bytes"""
    out = bytearray(stride(w) * h)
    for y in range(h):
        for x in range(w):
            out[y * stride(w) + x // 4] |= (levels[y][x] & 3) << (6 - 2 * (x % 4))
    return bytes(out)


def unpack_l2(data, w, h):
    return [[(data[y * stride(w) + x // 4] >> (6 - 2 * (x % 4))) & 3 for x in range(w)] for y in range(h)]


def quantise(alpha):
    """rows of 0..255 (or floats 0..1 when given as floats) -> rows of 0..3"""
    return [[min(3, max(0, int(round((a * 255.0 if isinstance(a, float) else a) / 85.0)))) for a in row] for row in alpha]


def record(mini_l2, med_l2):
    assert len(mini_l2) == stride(MINI[0]) * MINI[1] and len(med_l2) == stride(MED[0]) * MED[1]
    px = mini_l2 + med_l2
    hdr = struct.pack("<IBBBBBBHI", MAGIC, 1, 2, MINI[0], MINI[1], MED[0], MED[1], 0, zlib.crc32(px) & 0xFFFFFFFF)
    assert len(hdr) == HEADER
    return hdr + px


def check_record(rec):
    """the firmware's logoCheck: returns (error string or None, mini bytes, med bytes)"""
    if len(rec) < RECORD:
        return "short (%d B)" % len(rec), None, None
    magic, ver, fmt, mw, mh, dw, dh, _flags, crc = struct.unpack_from("<IBBBBBBHI", rec, 0)
    if magic != MAGIC or ver != 1:
        return "bad magic or version", None, None
    if fmt != 2 or (mw, mh) != MINI or (dw, dh) != MED:
        return "bad format or size", None, None
    px = rec[HEADER:RECORD]
    if zlib.crc32(px) & 0xFFFFFFFF != crc:
        return "bad crc", None, None
    n = stride(MINI[0]) * MINI[1]
    return None, px[:n], px[n:]


# ------------------------------------------------------------------------------------------------ PNG
def read_png(path):
    """-> (w, h, rows of alpha 0..255). Alpha channel when there is one, else the grey level (white = opaque)."""
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("%s: not a PNG" % path)
    pos, idat, w = 8, b"", None
    while pos < len(d):
        n, typ = struct.unpack_from(">I4s", d, pos)
        body = d[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            w, h, depth, ctype, _c, _f, inter = struct.unpack(">IIBBBBB", body)
            if depth != 8 or inter != 0 or ctype not in (0, 2, 4, 6):
                sys.exit("%s: only 8 bit non-interlaced grey, RGB, grey+alpha or RGBA PNGs" % path)
        elif typ == b"PLTE":
            sys.exit("%s: palette PNGs are not supported, save as grey or RGBA" % path)
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
    if w is None:
        sys.exit("%s: no IHDR" % path)
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    rows, prev, i = [], bytearray(w * bpp), 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + w * bpp])
        i += 1 + w * bpp
        for x in range(len(line)):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        prev = line
        out = []
        for x in range(w):
            px = line[x * bpp:(x + 1) * bpp]
            if ctype == 0:
                out.append(px[0])
            elif ctype == 2:
                out.append((px[0] * 299 + px[1] * 587 + px[2] * 114) // 1000)
            else:
                out.append(px[-1])
        rows.append(out)
    return w, h, rows


def write_png(path, rows):
    """rows of grey 0..255"""
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def resample(rows, w, h):
    """area average of rows (any size) to w x h"""
    sh, sw = len(rows), len(rows[0])
    if (sw, sh) == (w, h):
        return rows
    out = []
    for y in range(h):
        y0, y1 = y * sh / h, (y + 1) * sh / h
        line = []
        for x in range(w):
            x0, x1 = x * sw / w, (x + 1) * sw / w
            acc = area = 0.0
            for sy in range(int(y0), min(sh, int(y1) + 1)):
                fy = min(y1, sy + 1) - max(y0, sy)
                if fy <= 0:
                    continue
                for sx in range(int(x0), min(sw, int(x1) + 1)):
                    fx = min(x1, sx + 1) - max(x0, sx)
                    if fx > 0:
                        acc += rows[sy][sx] * fx * fy
                        area += fx * fy
            line.append(int(round(acc / area)) if area else 0)
        out.append(line)
    return out


def preview(path, mini_l2, med_l2, zoom=4):
    """both images side by side, zoomed, white on black"""
    a, b = unpack_l2(mini_l2, *MINI), unpack_l2(med_l2, *MED)
    h = max(MINI[1], MED[1])
    rows = []
    for y in range(h):
        r = [a[y][x] * 85 if y < MINI[1] else 0 for x in range(MINI[0])] + [40] * 4 + [b[y][x] * 85 for x in range(MED[0])]
        rows.append(r)
    big = []
    for r in rows:
        line = [v for v in r for _ in range(zoom)]
        big += [line] * zoom
    write_png(path, big)


# ------------------------------------------------------------------------------------------------ stock C arrays
def read_h(path):
    t = open(path).read()
    t = t[t.index("{") + 1:t.rindex("}")]
    return bytes(int(x) for x in re.findall(r"\d+", t))


# ------------------------------------------------------------------------------------------------ text logos
# Stroke font on a 4 x 6 grid (y down), polylines, chamfered corners: the geometric look of the stock lettered logos
GLYPHS = {
    "A": [[(0, 6), (0, 1), (1, 0), (3, 0), (4, 1), (4, 6)], [(0, 3.5), (4, 3.5)]],
    "B": [[(0, 3), (0, 0), (3, 0), (4, 1), (4, 2), (3, 3), (0, 3), (0, 6), (3, 6), (4, 5), (4, 4), (3, 3)]],
    "C": [[(4, 0), (1, 0), (0, 1), (0, 5), (1, 6), (4, 6)]],
    "D": [[(0, 0), (3, 0), (4, 1), (4, 5), (3, 6), (0, 6), (0, 0)]],
    "E": [[(4, 0), (0, 0), (0, 6), (4, 6)], [(0, 3), (3, 3)]],
    "F": [[(4, 0), (0, 0), (0, 6)], [(0, 3), (3, 3)]],
    "G": [[(4, 1), (3, 0), (1, 0), (0, 1), (0, 5), (1, 6), (3, 6), (4, 5), (4, 3), (2, 3)]],
    "H": [[(0, 0), (0, 6)], [(4, 0), (4, 6)], [(0, 3), (4, 3)]],
    "I": [[(2, 0), (2, 6)], [(1, 0), (3, 0)], [(1, 6), (3, 6)]],
    "J": [[(4, 0), (4, 5), (3, 6), (1, 6), (0, 5)]],
    "K": [[(0, 0), (0, 6)], [(4, 0), (1, 3), (0, 3)], [(1, 3), (4, 6)]],
    "L": [[(0, 0), (0, 6), (4, 6)]],
    "M": [[(0, 6), (0, 0), (2, 2), (4, 0), (4, 6)]],
    "N": [[(0, 6), (0, 0), (4, 6), (4, 0)]],
    "O": [[(1, 0), (3, 0), (4, 1), (4, 5), (3, 6), (1, 6), (0, 5), (0, 1), (1, 0)]],
    "P": [[(0, 6), (0, 0), (3, 0), (4, 1), (4, 2), (3, 3), (0, 3)]],
    "Q": [[(1, 0), (3, 0), (4, 1), (4, 5), (3, 6), (1, 6), (0, 5), (0, 1), (1, 0)], [(2.5, 4.5), (4, 6)]],
    "R": [[(0, 6), (0, 0), (3, 0), (4, 1), (4, 2), (3, 3), (0, 3)], [(2, 3), (4, 6)]],
    "S": [[(4, 0), (1, 0), (0, 1), (0, 2), (1, 3), (3, 3), (4, 4), (4, 5), (3, 6), (0, 6)]],
    "T": [[(0, 0), (4, 0)], [(2, 0), (2, 6)]],
    "U": [[(0, 0), (0, 5), (1, 6), (3, 6), (4, 5), (4, 0)]],
    "V": [[(0, 0), (2, 6), (4, 0)]],
    "W": [[(0, 0), (0, 6), (2, 4), (4, 6), (4, 0)]],
    "X": [[(0, 0), (4, 6)], [(4, 0), (0, 6)]],
    "Y": [[(0, 0), (2, 3), (4, 0)], [(2, 3), (2, 6)]],
    "Z": [[(0, 0), (4, 0), (0, 6), (4, 6)]],
    "0": [[(1, 0), (3, 0), (4, 1), (4, 5), (3, 6), (1, 6), (0, 5), (0, 1), (1, 0)]],
    "1": [[(1, 1), (2, 0), (2, 6)], [(1, 6), (3, 6)]],
    "2": [[(0, 1), (1, 0), (3, 0), (4, 1), (4, 2), (0, 6), (4, 6)]],
    "3": [[(0, 0), (3, 0), (4, 1), (4, 2), (3, 3), (1, 3)], [(3, 3), (4, 4), (4, 5), (3, 6), (0, 6)]],
    "4": [[(3, 6), (3, 0), (0, 4), (4, 4)]],
    "5": [[(4, 0), (0, 0), (0, 3), (3, 3), (4, 4), (4, 5), (3, 6), (0, 6)]],
    "6": [[(4, 0), (1, 0), (0, 1), (0, 5), (1, 6), (3, 6), (4, 5), (4, 4), (3, 3), (0, 3)]],
    "7": [[(0, 0), (4, 0), (1, 6)]],
    "8": [[(1, 0), (3, 0), (4, 1), (4, 2), (3, 3), (1, 3), (0, 2), (0, 1), (1, 0)],
          [(1, 3), (0, 4), (0, 5), (1, 6), (3, 6), (4, 5), (4, 4), (3, 3)]],
    "9": [[(0, 6), (3, 6), (4, 5), (4, 1), (3, 0), (1, 0), (0, 1), (0, 2), (1, 3), (4, 3)]],
    "-": [[(0.5, 3), (3.5, 3)]],
    " ": [],
}


def seg_dist2(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    qx, qy = ax + t * dx - px, ay + t * dy - py
    return qx * qx + qy * qy


def text_logo(text, w, h, pill_x, radius, cap_h, stroke, ss=4):
    """alpha rows (floats 0..1): a rounded pill from pill_x to w - pill_x, the text cut out of it, centred"""
    text = text.upper()
    for c in text:
        if c not in GLYPHS:
            sys.exit("no glyph for %r (have %s)" % (c, "".join(sorted(GLYPHS))))
    u = (cap_h - stroke) / 6.0
    u_x = u * 1.3                   # the stock letters are wide
    gw = 4 * u_x + stroke           # glyph cell width in pixels
    gap = stroke * 0.9              # space between glyph cells
    total = len(text) * gw + (len(text) - 1) * gap
    room = (w - 2 * pill_x) - 2 * radius * 0.55
    if total > room:   # long names: narrower letters, same height
        k = room / total
        u_x, gw, gap = u_x * k, gw * k, gap * k
        total = room
    x0 = (w - total) / 2.0
    y0 = (h - cap_h) / 2.0
    segs = []
    for i, c in enumerate(text):
        ox = x0 + i * (gw + gap) + stroke / 2.0
        oy = y0 + stroke / 2.0
        for line in GLYPHS[c]:
            pts = [(ox + gx * u_x, oy + gy * u) for gx, gy in line]
            segs += [(pts[j][0], pts[j][1], pts[j + 1][0], pts[j + 1][1]) for j in range(len(pts) - 1)]
    r2 = (stroke / 2.0) ** 2
    l, r = pill_x, w - pill_x
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            pill = ink = 0
            for sy in range(ss):
                py = y + (sy + 0.5) / ss
                for sx in range(ss):
                    px = x + (sx + 0.5) / ss
                    # rounded rectangle [l, r] x [0, h] with corner radius
                    cx = min(max(px, l + radius), r - radius)
                    cy = min(max(py, radius), h - radius)
                    if (px - cx) ** 2 + (py - cy) ** 2 > radius * radius or px < l or px > r:
                        continue
                    pill += 1
                    if any(seg_dist2(px, py, *s) <= r2 for s in segs):
                        ink += 1
            row.append((pill - ink) / float(ss * ss))
        rows.append(row)
    return rows


def text_record(text):
    # sizes measured on the stock logos (core/libs/images): med pill x 4..104 with corner radius 19, letters 16 px high
    # with 3 px strokes, mini pill x 2..78 radius 13, letters 11 px with 2 px strokes
    mini = text_logo(text, MINI[0], MINI[1], 2, 13.0, 11.0, 2.2)
    med = text_logo(text, MED[0], MED[1], 4, 19.0, 16.0, 3.2)
    return record(pack_l2(quantise(mini), *MINI), pack_l2(quantise(med), *MED))


def png_l2(rows, size):
    return pack_l2(quantise(resample(rows, *size)), *size)


# ------------------------------------------------------------------------------------------------ main
def plg_trailer(data):
    """the logo record a .plg names in plug_manifest.reserved[0], or None"""
    if len(data) < 160 or struct.unpack_from("<I", data, 0)[0] != 0x31474C50:
        return None
    hsize, = struct.unpack_from("<H", data, 4)
    off, = struct.unpack_from("<I", data, 64 + 72)
    return data[off:off + RECORD] if off >= hsize and off >= 160 else None


def selftest(images):
    fails = 0
    pairs = [("acd", 29), ("fat", 29), ("vap", 29), ("wtfm", 29), ("perc", 29), ("phz", 29)]
    for name, _ in pairs:
        mini = read_h(os.path.join(images, "%s_mini_nomark_80x29_L2.h" % name))
        med = read_h(os.path.join(images, "%s_med_108x40_L2.h" % name))
        # stock -> PNG (4 grey levels) -> make_plgi -> the same bytes
        tmp = "/tmp/make_plgi_selftest_%s" % name
        write_png(tmp + "_mini.png", [[v * 85 for v in r] for r in unpack_l2(mini, *MINI)])
        write_png(tmp + "_med.png", [[v * 85 for v in r] for r in unpack_l2(med, *MED)])
        rec = record(png_l2(read_png(tmp + "_mini.png")[2], MINI), png_l2(read_png(tmp + "_med.png")[2], MED))
        err, a, b = check_record(rec)
        ok = err is None and a == mini and b == med and len(rec) == RECORD
        fails += not ok
        print("%s  %s round trip (PNG -> .plgi equals the stock bytes, %d B)" % ("ok  " if ok else "FAIL", name.upper(), len(rec)))
        os.remove(tmp + "_mini.png")
        os.remove(tmp + "_med.png")
    rec = text_record("JU6")
    ok = check_record(rec)[0] is None and len(rec) == RECORD
    fails += not ok
    print("%s  text logo record checks" % ("ok  " if ok else "FAIL"))
    print("PASSED" if not fails else "FAILED")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--text")
    ap.add_argument("--png")
    ap.add_argument("--mini")
    ap.add_argument("--med")
    ap.add_argument("--from-h", nargs=2, metavar=("MINI_H", "MED_H"))
    ap.add_argument("--check")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--images", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "core", "libs", "images"))
    ap.add_argument("-o", "--out")
    ap.add_argument("--preview")
    a = ap.parse_args()
    if a.selftest:
        return selftest(a.images)
    if a.check:
        data = open(a.check, "rb").read()
        rec = data if a.check.lower().endswith(".plgi") else plg_trailer(data)
        if rec is None:
            print("%s: no logo trailer" % a.check)
            return 1
        if a.check.lower().endswith(".plgi") and len(data) != RECORD:
            print("%s: %d B, a .plgi is exactly %d B" % (a.check, len(data), RECORD))
            return 1
        err, mini, med = check_record(rec)
        if err:
            print("%s: %s" % (a.check, err))
            return 1
        if a.preview:
            preview(a.preview, mini, med)
        print("%s: PLGI 1 ok, %d B" % (a.check, RECORD))
        return 0
    if a.text:
        rec = text_record(a.text)
    elif a.from_h:
        rec = record(read_h(a.from_h[0]), read_h(a.from_h[1]))
    elif a.png or (a.mini and a.med):
        mini_rows = read_png(a.mini or a.png)[2]
        med_rows = read_png(a.med or a.png)[2]
        rec = record(png_l2(mini_rows, MINI), png_l2(med_rows, MED))
    else:
        ap.error("give --text, --png, --mini and --med, --from-h, --check or --selftest")
    if not a.out:
        ap.error("-o is needed")
    open(a.out, "wb").write(rec)
    err, mini, med = check_record(rec)
    assert err is None
    if a.preview:
        preview(a.preview, mini, med)
    print("%s: PLGI 1, %d B" % (a.out, len(rec)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
