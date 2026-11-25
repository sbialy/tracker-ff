"""Smallest raw deflate (RFC 1951) stream of a byte string, shared by the boot asset generators
(fonts_deflate.py, icons_deflate.py, randname_deflate.py, playorder_deflate.py).

Candidates: zlib level 9 over every memLevel and the default and filtered strategies, plus libdeflate level 12
(`libdeflate-gzip -12`, found on PATH, brew install libdeflate) when it is there. The shortest one wins and is checked by
inflating it again with zlib. libdeflate usually gives 3 to 6 percent less than zlib on the font bitmaps. The decoder in
core/libs/NewDisplay/inflate.cpp reads any valid stream (stored, fixed and dynamic blocks), so the encoder does not matter
to the firmware. The bytes of a stream depend on the tool versions: the committed generated files are what counts.
"""
import shutil
import subprocess
import zlib


def _zlib_best(raw):
    best = None
    for mem in range(1, 10):
        for strat in (zlib.Z_DEFAULT_STRATEGY, zlib.Z_FILTERED):
            co = zlib.compressobj(9, zlib.DEFLATED, -15, mem, strat)
            z = co.compress(raw) + co.flush()
            if best is None or len(z) < len(best):
                best = z
    return best


def _libdeflate(raw):
    exe = shutil.which("libdeflate-gzip")
    if not exe:
        return None
    out = subprocess.run([exe, "-12", "-c"], input=raw, capture_output=True, check=True).stdout
    # gzip member written without a name: 10 byte header, deflate stream, 8 byte trailer
    if len(out) < 18 or out[:3] != b"\x1f\x8b\x08" or out[3] != 0:
        return None
    return out[10:-8]


def best_deflate(raw):
    best = _zlib_best(raw)
    alt = _libdeflate(raw)
    if alt is not None and len(alt) < len(best):
        best = alt
    assert zlib.decompress(best, -15) == raw
    return best
