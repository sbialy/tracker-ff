#!/bin/bash
# Render compare of the synth engine plugins against the built-in engines (phase 3 gate: bit identical).
#
#   TOOLCHAIN=.../agt/10.3/bin/arm-none-eabi- PY=/tmp/phzvenv/bin/python synth-open/plug/synth_render_compare.sh OUT
#
# OUT holds ju6.plg and fm4.plg (build_plugs.sh). The harness (synth/synth_harness.cpp) is built here, it never ships. The reference is what the firmware itself compiled when
# the engine was built in: make -n of the 1.1.0 release engine set (TRIM=1, JU6 and FM4 in) gives the exact compile lines of
# engine_<e>.cpp and ot-synths/<e>/<e>.cpp, they are run as they are, and those objects behind the same plugin wrapper make
# <e>-fwobj.plg. Both render the harness script (scenario 0 at the defaults, 1 with every parameter moved and ramped) in
# emu_synth.py at the engine's FlexNVM home and in SRAM: every checksum must be equal.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SO="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$SO/.." && pwd)"
OUT="$(cd "${1:?output dir}" && pwd)"
PY="${PY:-python3}"
: "${TOOLCHAIN:?set TOOLCHAIN to the agt 10.3 prefix}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SET="ACD FAT VAP WTFM PERC CHIP JU6 PHZ FM4 DTYPE CSTYPE ATYPE TTYPE 8TYPE BTYPE ARCTYPE"
(cd "$ROOT" && make original TOOLCHAIN="$TOOLCHAIN" SYNTH_OPEN=1 TRIM=1 BUILD_TAG=x SYNTH_TAG=-rc SYNTH_ENGINES="$SET" -n 2>/dev/null) \
    > "$TMP/make-n.txt"
python3 "$HERE/build_plug.py" --id ff.test.synthharness --kind offline --opt "-O2 -fno-builtin" --out "$TMP/synth-harness.plg" \
    -I "$SO" -I "$SO/ot" -I "$ROOT/synth/include" "$HERE/synth/synth_harness.cpp" > /dev/null
fail=0
for e in ju6 fm4; do
    for src in "synth-open/engine_$e.cpp" "synth-open/ot-synths/$e/$e.cpp"; do
        line=$(grep -F "$ROOT/$src\"" "$TMP/make-n.txt" | grep -E '(c\+\+|g\+\+) ' | head -1)
        [ -n "$line" ] || { echo "no firmware compile line for $src"; exit 1; }
        obj="$TMP/fw-$(basename "$src" .cpp).o"
        line=$(printf '%s' "$line" | sed -E "s#-o \"[^\"]*\"#-o \"$obj\"#; s#-MMD -MP##")
        (cd "$ROOT/core" && eval "$line")
    done
    python3 "$HERE/build_plug.py" --id "org.opentracker.synth.$e" --kind synth --version 1.0.0 --opt "-Os -fno-builtin" --strip-ctors \
        --out "$TMP/$e-fwobj.plg" -I "$SO" -I "$SO/ot" -I "$ROOT/synth/include" "$HERE/synth/${e}_plug.cpp" \
        "$TMP/fw-engine_$e.o" "$TMP/fw-$e.o" > /dev/null
    for s in 0 1; do
        emu() { "$PY" -I "$HERE/emu_synth.py" "$1" "$TMP/synth-harness.plg" --scenario $s "${@:2}" 2>&1 | sed -n 's/^output checksum //p'; }
        p=$(emu "$OUT/$e.plg" --base 0x10000000 --rw-base 0x20027000)
        q=$(emu "$OUT/$e.plg" --base 0x1FFF6000)
        r=$(emu "$TMP/$e-fwobj.plg" --base 0x10000000 --rw-base 0x20027000)
        ok=ok
        [ -n "$p" ] && [ "$p" = "$q" ] && [ "$p" = "$r" ] || { ok=FAIL; fail=1; }
        printf "%-4s %s  scenario %d: plugin %s (FlexNVM) %s (SRAM), firmware objects %s\n" "$e" "$ok" "$s" "$p" "$q" "$r"
    done
done
[ "$fail" = 0 ] && echo "render compare PASSED" || { echo "render compare FAILED"; exit 1; }
