#!/bin/bash
# Builds the two worked examples of docs/plugin_sdk.md and checks each one on the Mac. No shipped plugin is built here: every synth
# engine and master insert plugin is built in its module repository (tracker-engines-os, tracker-engines, tracker-effects-os, tracker-effects),
# this repository pins their releases (engines/external.lock.json) and checks them (engines/crosscheck.sh), see engines/README.md.
#   - tilt (examples/tilt.c, a master insert): no R_ARM_GOT* relocation in the linked ELF, the rebase table holds every R_ARM_ABS32
#     word (count equal, > 0), emu_plug.py gives the same output checksum at the insert 1 home (SRAM_L 0x1FFF0BD0), the insert 2
#     home (the top of the same slot, 0x1FFF1BA0: both inserts share it) and in SDRAM, and again with one non-default setting
#   - sawf (examples/saw_synth.cpp, a synth): the firmware's own attach check (tools/validate_synth.py runs the real checkSynth) and
#     the emulator at two homes
#
#   TOOLCHAIN=.../arm-none-eabi- PY=/path/to/venv/bin/python synth-open/plug/build_plugs.sh OUT
# PY needs the unicorn module. Output in OUT/examples (do not copy them to a card by accident).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SO="$(cd "$HERE/.." && pwd)"
mkdir -p "${1:?output dir}"
OUT="$(cd "$1" && pwd)"
PY="${PY:-python3}"
: "${TOOLCHAIN:?set TOOLCHAIN to the agt 10.3 prefix}"
mkdir -p "$OUT"
cd "$SO"
# Worked examples of docs/plugin_sdk.md, in OUT/examples (kept apart from the shipped set: do not copy them to a card by accident)
mkdir -p "$OUT/examples"
python3 plug/build_plug.py --id org.example.fx.tilt --kind master --version 1.0.0 --opt "-O2 -ffp-contract=off" \
    --keep "$OUT/examples/tilt.elf" --out "$OUT/examples/tilt.plg" plug/examples/tilt.c
python3 plug/build_plug.py --id org.example.synth.sawf --kind synth --version 1.0.0 --opt "-O2 -ffp-contract=off" \
    --synth 0x64880b9c,0x64880b9c,0x64880b9c --keep "$OUT/examples/sawf.elf" --out "$OUT/examples/sawf.plg" \
    -I . -I ot -I ../synth/include plug/examples/saw_synth.cpp
fail=0
emu() { "$PY" -I plug/emu_plug.py "$@" 2>&1 | sed -n 's/^output checksum \([0-9a-f]*\).*/\1/p'; }
# one non-default setting per plugin: every parameter moved, then one more change in block 5 (ramps and crossfades)
nd() {
    case "$1" in
        examples/tilt) echo "--param 0=6 --param 1=4 --param 2=1200 --param-at 5:1=-5" ;;
    esac
}
for p in examples/tilt; do
    got=$("${TOOLCHAIN}objdump" -r "$OUT/$p.elf" | grep -c 'R_ARM_GOT' || true)
    abs=$("${TOOLCHAIN}objdump" -r "$OUT/$p.elf" | grep -c 'R_ARM_ABS32' || true)
    reb=$(python3 -c "import struct,sys; print(struct.unpack_from('<I', open(sys.argv[1],'rb').read(), 32)[0])" "$OUT/$p.plg")
    l=$(emu "$OUT/$p.plg" --base 0x1FFF0BD0)
    n=$(emu "$OUT/$p.plg" --base 0x1FFF1BA0)
    d=$(emu "$OUT/$p.plg" --base 0x70000000)
    nl=$(emu "$OUT/$p.plg" --base 0x1FFF0BD0 $(nd $p))
    nn=$(emu "$OUT/$p.plg" --base 0x1FFF1BA0 $(nd $p))
    nd_=$(emu "$OUT/$p.plg" --base 0x70000000 $(nd $p))
    ok=ok
    [ -n "$nl" ] && [ "$nl" = "$nn" ] && [ "$nl" = "$nd_" ] && [ "$nl" != "$l" ] || ok=FAIL
    [ "$got" = 0 ] && [ "$reb" -gt 0 ] && [ "$reb" = "$abs" ] && [ -n "$l" ] && [ "$l" = "$n" ] && [ "$l" = "$d" ] || ok=FAIL
    [ "$ok" = ok ] || fail=1
    printf "%-13s %s  GOT relocs %s, ABS32 relocs %s, rebase words %s, checksum SRAM_L %s SRAM_L top %s SDRAM %s, non-default %s %s %s, sha256 %s\n" \
        "$p" "$ok" "$got" "$abs" "$reb" "$l" "$n" "$d" "$nl" "$nn" "$nd_" "$(shasum -a 256 "$OUT/$p.plg" | cut -c1-16)"
done
# the worked synth example: the same attach check, no GOT, a full rebase table, and the same render at two homes
"$PY" -I tools/validate_synth.py "$OUT/examples/sawf.plg" || fail=1
python3 plug/build_plug.py --id ff.test.synthharness --kind offline --opt "-O2 -fno-builtin" --out "$OUT/examples/h.plg" \
    -I . -I ot -I ../synth/include plug/synth/synth_harness.cpp > /dev/null
got=$("${TOOLCHAIN}objdump" -r "$OUT/examples/sawf.elf" | grep -c 'R_ARM_GOT' || true)
abs=$("${TOOLCHAIN}objdump" -r "$OUT/examples/sawf.elf" | grep -c 'R_ARM_ABS32' || true)
reb=$(python3 -c "import struct,sys; print(struct.unpack_from('<I', open(sys.argv[1],'rb').read(), 32)[0])" "$OUT/examples/sawf.plg")
ex_ok=ok
[ "$got" = 0 ] && [ "$reb" = "$abs" ] && [ "$reb" -gt 0 ] || ex_ok=FAIL
for s in 0 1; do
    a=$("$PY" -I plug/emu_synth.py "$OUT/examples/sawf.plg" "$OUT/examples/h.plg" --scenario $s --base 0x10000000 --rw-base 0x20027000 2>&1 | sed -n 's/^output checksum //p')
    b=$("$PY" -I plug/emu_synth.py "$OUT/examples/sawf.plg" "$OUT/examples/h.plg" --scenario $s --base 0x1FFF6000 2>&1 | sed -n 's/^output checksum //p')
    [ -n "$a" ] && [ "$a" = "$b" ] || ex_ok=FAIL
    echo "sawf  $ex_ok  scenario $s checksum FlexNVM $a SRAM $b, GOT relocs $got, rebase words $reb = ABS32 $abs"
done
[ "$ex_ok" = ok ] || fail=1
rm -f "$OUT/examples/h.plg"
rm -f "$OUT"/*.elf "$OUT"/examples/*.elf
[ "$fail" = 0 ] && echo "PASSED" || { echo "FAILED"; exit 1; }
