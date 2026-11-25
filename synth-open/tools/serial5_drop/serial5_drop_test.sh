#!/bin/bash
# Host test of the DIN MIDI out drop path (serial5_drop_test.c): the firmware's own serial5.c with its two interrupt mask asm lines turned into
# plain statements and the Kinetis registers as variables (stubs/). No Arm toolchain, no device.
#   serial5_drop_test.sh [FW_TREE]    default: this tree
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FW="${1:-$(cd "$HERE/../../.." && pwd)}"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/serial5_drop.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
sed -E 's/__asm__ volatile\("mrs[^;]*;/irq_state = 0;/; s/__asm__ volatile\("msr[^;]*;/(void)irq_state;/' \
    "$FW/platforms/mk66/3rdparty/teensy_core/serial5.c" >"$OUT/serial5.c"
if grep -q '__asm__' "$OUT/serial5.c"; then echo "serial5.c has an asm line the runner does not know"; exit 1; fi
CC="${CC:-cc}"
FLAGS=(-std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -I "$HERE/stubs")
"$CC" "${FLAGS[@]}" -Wno-unused-function -c "$OUT/serial5.c" -o "$OUT/serial5.o"
"$CC" "${FLAGS[@]}" -c "$HERE/serial5_drop_test.c" -o "$OUT/test.o"
"$CC" -fsanitize=address,undefined "$OUT"/*.o -o "$OUT/test"
"$OUT/test"
