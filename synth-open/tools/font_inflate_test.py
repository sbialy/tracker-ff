#!/usr/bin/env python3
"""Host proof that the deflated fonts (core/libs/fonts/*_L4.cpp) decode to the old fonts, byte for byte.

  1. Expands the stock run length encoded originals (core/libs/fonts/rle/*.cpp.rle), the arrays decompress() used to expand, in python.
  2. Compiles the firmware's own core/libs/NewDisplay/inflate.cpp with the 10 deflated arrays and decodes them natively.
  3. Compares: every font must be identical to the old bitmap and equal its rawSize (and fit the 22,396 B buffer of display.cpp).
  4. Stored, fixed and dynamic blocks of the decoder (streams made with zlib), and a damaged stream fuzz under ASan/UBSan.
Usage: font_inflate_test.py [WORKTREE]        Prints "font_inflate_test: PASS" (exit 0) or FAIL.
"""
import os
import re
import subprocess
import sys
import tempfile
import zlib

root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
here = os.path.join(root, "synth-open", "tools")
fonts = os.path.join(root, "core", "libs", "fonts")
PAIR = re.compile(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}")
bad = 0


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def build(tmp, san):
    exe = os.path.join(tmp, "check_san" if san else "check")
    cmd = ["c++", "-std=gnu++20", "-O1", "-g", "-I" + os.path.join(root, "core", "libs"), "-I" + fonts,
           os.path.join(here, "font_inflate_check.cpp"), os.path.join(root, "core", "libs", "NewDisplay", "inflate.cpp"), "-o", exe]
    cmd += [os.path.join(fonts, f) for f in sorted(os.listdir(fonts)) if f.endswith(".cpp")]
    if san:
        cmd[2:2] = ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"]
    r = run(cmd)
    if r.returncode:
        sys.exit("compile failed:\n" + r.stderr[-3000:])
    return exe


with tempfile.TemporaryDirectory() as tmp:
    exe, exe_san = build(tmp, False), build(tmp, True)
    # 1+2+3: the 10 fonts against the old bitmaps
    os.makedirs(os.path.join(tmp, "out"))
    for e in (exe, exe_san):
        r = run([e, "fonts", os.path.join(tmp, "out")])
        print(r.stdout, end="")
        if r.returncode:
            bad += 1
            print("FAIL: decode of the fonts failed\n" + r.stderr[-800:])
    n_ok = 0
    for fn in sorted(os.listdir(os.path.join(fonts, "rle"))):
        if not fn.endswith(".cpp.rle"):
            continue
        old = b"".join(bytes([int(v)]) * int(c) for v, c in PAIR.findall(open(os.path.join(fonts, "rle", fn), encoding="utf8").read()))
        binf = os.path.join(tmp, "out", fn[:-len(".cpp.rle")] + ".bin")
        new = open(binf, "rb").read() if os.path.exists(binf) else b""
        if new != old or len(old) > 22396:
            bad += 1
            print("FAIL: %s differs from the old bitmap (%d vs %d B)" % (fn, len(new), len(old)))
        else:
            n_ok += 1
    print("fonts byte identical to the old RLE decode: %d of 10" % n_ok)
    if n_ok != 10:
        bad += 1
    # 4: the block types
    cases = os.path.join(tmp, "cases")
    os.makedirs(cases)
    sample = open(os.path.join(tmp, "out", "RobotoMono-Regular_12_L4.bin"), "rb").read()
    rnd = bytes((i * 2654435761 >> 7) & 255 for i in range(5000))
    streams = []
    for raw in (sample, rnd, b"", b"a", b"abc" * 3000):
        for level, strat in ((0, 0), (1, 4), (6, 1), (9, 0), (9, 2)):   # stored, fixed (Z_FIXED), filtered, dynamic, huffman only
            co = zlib.compressobj(level, zlib.DEFLATED, -15, 8, strat)
            streams.append((co.compress(raw) + co.flush(), raw))
    k = 0
    for z, raw in streams:
        if not raw:
            continue
        open(os.path.join(cases, "%d.z" % k), "wb").write(z)
        open(os.path.join(cases, "%d.raw" % k), "wb").write(raw)
        k += 1
    r = run([exe_san, "cases", cases])
    fails = [l for l in r.stdout.splitlines() if "FAIL" in l]
    print("block type cases: %d streams, %d failed" % (k, len(fails)))
    if r.returncode or fails:
        bad += 1
        print(r.stdout[-1500:] + r.stderr[-1500:])
    r = run([exe_san, "fuzz"])
    print(r.stdout.strip())
    if r.returncode:
        bad += 1
        print(r.stderr[-2000:])

print("font_inflate_test: %s" % ("PASS" if not bad else "FAIL"))
sys.exit(1 if bad else 0)
