#!/usr/bin/env bash
# Host proof of the deflated boot logo, part of run_tests.sh. Needs c++ with ASan/UBSan, no Arm toolchain.
# Inflates core/libs/images/poly_logo_160x172_L8.cpp with the firmware's own inflate.cpp into a 27,520 B buffer, and compares the result with
# the python expansion of the stock {value, count} pairs (rle/poly_logo_160x172_L8.cpp.rle) that decompress() used to expand.
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/chk.cpp" <<'CPP'
#include <stdio.h>
#include <stdint.h>
#include "NewDisplay/inflate.h"
#include "poly_logo_160x172_L8.h"
InflateScratch gInflateScratch;
int main(int, char** argv)
{
    static uint8_t buf[27520 + 64];
    for (auto& b : buf) b = 0xA5;
    int n = inflateRaw(poly_logo_160x172_l8_deflated, sizeof(poly_logo_160x172_l8_deflated), buf, 27520, &gInflateScratch);
    if (n != poly_logo_160x172_l8_rawSize) { fprintf(stderr, "inflate returned %d\n", n); return 1; }
    for (int i = 27520; i < 27520 + 64; i++) if (buf[i] != 0xA5) { fprintf(stderr, "write past the buffer\n"); return 1; }
    // a buffer one byte short must be refused, never overrun
    if (inflateRaw(poly_logo_160x172_l8_deflated, sizeof(poly_logo_160x172_l8_deflated), buf, 27519, &gInflateScratch) >= 0) { fprintf(stderr, "short buffer accepted\n"); return 1; }
    FILE* f = fopen(argv[1], "wb"); fwrite(buf, 1, 27520, f); fclose(f);
    return 0;
}
CPP
c++ -std=gnu++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I"$root/core/libs" -I"$root/core/libs/images" \
    "$tmp/chk.cpp" "$root/core/libs/NewDisplay/inflate.cpp" "$root/core/libs/images/poly_logo_160x172_L8.cpp" -o "$tmp/chk"
"$tmp/chk" "$tmp/logo.bin"
python3 -I - "$root" "$tmp/logo.bin" <<'PY'
import re, sys
root, out = sys.argv[1:3]
txt = open(root + "/core/libs/images/rle/poly_logo_160x172_L8.cpp.rle", encoding="utf8").read()
old = b"".join(bytes([int(v)]) * int(c) for v, c in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", txt))
new = open(out, "rb").read()
if new != old or len(old) != 27520:
    sys.exit("logo_inflate_test: FAIL (%d vs %d B, equal %s)" % (len(new), len(old), new == old))
print("logo_inflate_test: PASS (27,520 B equal to the old RLE decode)")
PY
