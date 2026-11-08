#!/bin/bash
# Builds the shipped PUBLIC master insert plugins (dirt, tape, lofi, bbd, phaser: .plg, the OpenTracker ports; console and tube are built by poly-effects) and the two worked examples of
# docs/plugin_sdk.md, and checks each one on the Mac. The synth engine plugins are NOT built here: they have their own standalone
# build in engines/ (make -C engines, engines/README.md), and their firmware side checks are engines/crosscheck.sh.
#   - no R_ARM_GOT* relocation in the linked ELF, the rebase table holds every R_ARM_ABS32 word (count equal, > 0)
#   - emu_plug.py runs it at the insert 1 home (SRAM_L 0x1FFF0BD0), the insert 2 home (text at 0x1001E000 in the FlexNVM,
#     rw at 0x20026C00 on the heap) and in SDRAM, and the output checksum is the same at all three
#   - Dirt: entering Asym leaves no DC step (dirt_asym_check.py)
#   - Dirt: zero in gives exact zero out for every Type and Bits (mid-tread quantiser, dirt_zero_check.py)
#   - the same three runs again with one non-default setting (every parameter moved, one --param-at change in block 5 to test
#     the ramps), the checksum again equal at the three homes
#   - the two worked examples (examples/tilt.c, examples/saw_synth.cpp) are built into OUT/examples and get the same checks: tilt like
#     the inserts above, sawf through the firmware's own attach check (tools/validate_synth.py runs the real checkSynth) and the
#     emulator at two homes
#   - Dirt: the checksum is 3b1f3dc5, the device rule of section 11 (runs 2 and 3: the in-image Dirt gives 3b1f3dc5), and the
#     firmware's own ff_dirt.o and ff_dirt_ctl.o (FW_OBJ, from the PLUG_HOST image build) linked as a plugin give it too
#
#   TOOLCHAIN=.../agt/10.3/bin/arm-none-eabi- PY=/tmp/phzvenv/bin/python synth-open/plug/build_plugs.sh OUT [FW_OBJ]
# PY needs the unicorn module. Ports of firmware code build those objects with the firmware's flags (ff_dirt_ctl.cpp at -Os):
# at -O2 -ffp-contract=off GCC does not fuse 1 + 15 d^2 into a vfma and Coef::pre differs by one ulp (phase 1b).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SO="$(cd "$HERE/.." && pwd)"
mkdir -p "${1:?output dir}"
OUT="$(cd "$1" && pwd)"
FW_OBJ="${2:-}"
[ -z "$FW_OBJ" ] || FW_OBJ="$(cd "$FW_OBJ" && pwd)"
PY="${PY:-python3}"
: "${TOOLCHAIN:?set TOOLCHAIN to the agt 10.3 prefix}"
mkdir -p "$OUT"
cd "$SO"
python3 plug/build_plug.py --id ff.dirt --kind master --version 1.0.0 --opt "-O2 -ffp-contract=off -fno-builtin" \
    --src-opt "ff_dirt_ctl.cpp=-Os -fno-builtin" --keep "$OUT/dirt.elf" --out "$OUT/dirt.plg" -I . \
    plug/fx_dirt_plug.cpp ff_dirt.cpp ff_dirt_ctl.cpp
python3 plug/build_plug.py --id org.opentracker.fx.tape --kind master --version 1.0.0 --opt "-O2 -ffp-contract=off" \
    --keep "$OUT/tape.elf" --out "$OUT/tape.plg" plug/fx/ot_tape_plug.c
python3 plug/build_plug.py --id org.opentracker.fx.lofi --kind master --version 1.0.0 --opt "-O2 -ffp-contract=off" \
    --keep "$OUT/lofi.elf" --out "$OUT/lofi.plg" plug/fx/ot_lofi_plug.c
python3 plug/build_plug.py --id org.opentracker.fx.bbd --kind master --version 1.0.0 --opt "-O2 -ffp-contract=off" \
    --keep "$OUT/bbd.elf" --out "$OUT/bbd.plg" plug/fx/ot_bbd_plug.c
python3 plug/build_plug.py --id org.opentracker.fx.phaser --kind master --version 1.0.0 --opt "-O2 -ffp-contract=off" \
    --keep "$OUT/phaser.elf" --out "$OUT/phaser.plg" plug/fx/ot_phaser_plug.c
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
        dirt) echo "--param 0=2 --param 1=60 --param 2=8 --param 3=4 --param 4=70 --param 5=80 --param-at 5:1=20" ;;
        tape) echo "--param 0=80 --param 1=60 --param 2=40 --param 3=70 --param-at 5:0=10" ;;
        lofi) echo "--param 0=8 --param 1=2 --param 2=40 --param 3=1 --param 4=70 --param-at 5:0=6" ;;
        bbd) echo "--param 0=1 --param 1=60 --param 2=40 --param 3=90 --param-at 5:1=100" ;;
        phaser) echo "--param 0=1 --param 1=70 --param 2=50 --param 3=60 --param 4=100 --param 5=70 --param-at 5:0=0" ;;
        examples/tilt) echo "--param 0=6 --param 1=4 --param 2=1200 --param-at 5:1=-5" ;;
    esac
}
for p in dirt tape lofi bbd phaser examples/tilt; do
    got=$("${TOOLCHAIN}objdump" -r "$OUT/$p.elf" | grep -c 'R_ARM_GOT' || true)
    abs=$("${TOOLCHAIN}objdump" -r "$OUT/$p.elf" | grep -c 'R_ARM_ABS32' || true)
    reb=$(python3 -c "import struct,sys; print(struct.unpack_from('<I', open(sys.argv[1],'rb').read(), 32)[0])" "$OUT/$p.plg")
    l=$(emu "$OUT/$p.plg" --base 0x1FFF0BD0)
    n=$(emu "$OUT/$p.plg" --base 0x1001E000 --rw-base 0x20026C00)
    d=$(emu "$OUT/$p.plg" --base 0x70000000)
    nl=$(emu "$OUT/$p.plg" --base 0x1FFF0BD0 $(nd $p))
    nn=$(emu "$OUT/$p.plg" --base 0x1001E000 --rw-base 0x20026C00 $(nd $p))
    nd_=$(emu "$OUT/$p.plg" --base 0x70000000 $(nd $p))
    ok=ok
    [ -n "$nl" ] && [ "$nl" = "$nn" ] && [ "$nl" = "$nd_" ] && [ "$nl" != "$l" ] || ok=FAIL
    [ "$got" = 0 ] && [ "$reb" -gt 0 ] && [ "$reb" = "$abs" ] && [ -n "$l" ] && [ "$l" = "$n" ] && [ "$l" = "$d" ] || ok=FAIL
    [ "$ok" = ok ] || fail=1
    printf "%-5s %s  GOT relocs %s, ABS32 relocs %s, rebase words %s, checksum SRAM_L %s FlexNVM+heap %s SDRAM %s, non-default %s %s %s, sha256 %s\n" \
        "$p" "$ok" "$got" "$abs" "$reb" "$l" "$n" "$d" "$nl" "$nn" "$nd_" "$(shasum -a 256 "$OUT/$p.plg" | cut -c1-16)"
    if [ "$p" = dirt ]; then
        if [ "$l" = 3b1f3dc5 ]; then echo "dirt  ok  checksum 3b1f3dc5 = the device's in-image Dirt (section 11)"; else echo "dirt  FAIL checksum $l, want 3b1f3dc5"; fail=1; fi
    fi
done
# entering Asym from Soft, Hard and Fold: no DC step (the follower of opentracker-fx cbedea9)
python3 plug/dirt_asym_check.py "$PY" plug/emu_plug.py "$OUT/dirt.plg" "$OUT" || fail=1
rm -f "$OUT"/asym_*.wav
python3 plug/dirt_zero_check.py "$PY" plug/emu_plug.py "$OUT/dirt.plg" "$OUT" || fail=1
rm -f "$OUT"/zero_*.wav "$OUT"/near_*.wav
if [ -n "$FW_OBJ" ]; then
    # the image's own Dirt kernel and coefficient objects behind the plugin wrapper: must give the same output as dirt.plg
    python3 plug/build_plug.py --id ff.dirt --kind master --opt "-O2 -ffp-contract=off -fno-builtin" --out "$OUT/dirt-fwobj.plg" \
        -I . plug/fx_dirt_plug.cpp "$FW_OBJ/ff_dirt.o" "$FW_OBJ/ff_dirt_ctl.o" > /dev/null
    f=$(emu "$OUT/dirt-fwobj.plg" --base 0x1FFF0BD0)
    if [ "$f" = 3b1f3dc5 ]; then echo "dirt  ok  the firmware's ff_dirt.o + ff_dirt_ctl.o give 3b1f3dc5 too"; else echo "dirt  FAIL firmware objects give $f"; fail=1; fi
    rm -f "$OUT/dirt-fwobj.plg"
fi
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
