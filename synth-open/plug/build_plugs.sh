#!/bin/bash
# Builds the shipped master insert plugins (dirt, tape, lofi, console, tube, bbd, phaser: .plg) and the synth engine plugins
# (the 12 of synth_engines.sh: .plg, render compare against the firmware's own objects in synth_render_compare.sh) with their card logos
# (<name>.plgi: tools/make_plgi.py text logos, previews/<name>.png, and the logo-test/ files of the device test) and checks each
# one on the Mac:
#   - no R_ARM_GOT* relocation in the linked ELF, the rebase table holds every R_ARM_ABS32 word (count equal, > 0)
#   - emu_plug.py runs it at the insert 1 home (SRAM_L 0x1FFF0BD0), the insert 2 home (text at 0x1001E000 in the FlexNVM,
#     rw at 0x20026C00 on the heap) and in SDRAM, and the output checksum is the same at all three
#   - Dirt: entering Asym leaves no DC step (dirt_asym_check.py)
#   - Dirt: zero in gives exact zero out for every Type and Bits (mid-tread quantiser, dirt_zero_check.py)
#   - the same three runs again with one non-default setting (every parameter moved, one --param-at change in block 5 to test
#     the ramps), the checksum again equal at the three homes
#   - every synth plugin passes the firmware's own attach check (tools/validate_synth.py runs the real checkSynth)
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
python3 plug/build_plug.py --id ff.console --kind master --version 1.0.0 --opt "-O2 -ffp-contract=off" \
    --keep "$OUT/console.elf" --out "$OUT/console.plg" plug/fx/ff_console_plug.c
python3 plug/build_plug.py --id ff.tube --kind master --version 1.0.0 --opt "-O2 -ffp-contract=off" \
    --keep "$OUT/tube.elf" --out "$OUT/tube.plg" plug/fx/ff_tube_plug.c
python3 plug/build_plug.py --id org.opentracker.fx.bbd --kind master --version 1.0.0 --opt "-O2 -ffp-contract=off" \
    --keep "$OUT/bbd.elf" --out "$OUT/bbd.plg" plug/fx/ot_bbd_plug.c
python3 plug/build_plug.py --id org.opentracker.fx.phaser --kind master --version 1.0.0 --opt "-O2 -ffp-contract=off" \
    --keep "$OUT/phaser.elf" --out "$OUT/phaser.plg" plug/fx/ot_phaser_plug.c
# Synth engines (phase 3): the engine sources exactly as the firmware compiled them when they were built in (the flags of
# make -n at TRIM=1, per engine in synth_engines.sh: -O2 (FM4 and B-Type -O3) and fp-contract left at its fused default for
# the OpenTracker modules, -O2 -ffp-contract=off for PHZ and PLK, see synth_render_compare.sh, which proves it)
. "$HERE/synth_engines.sh"
FWFLAGS="-std=gnu++20 -felide-constructors -include cstdint -fno-rtti -fno-exceptions -fno-common -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -O2 -DNDEBUG -mfp16-format=ieee"
build_synth() {   # build_synth <plugin> <out.plg> [extra build_plug.py args]
    local e="$1" out="$2"; shift 2
    read -r E def dsp dspopt <<< "$(synth_plug_info "$e")"
    python3 plug/build_plug.py --id org.opentracker.synth.$e --kind synth --version 1.0.0 --opt "-Os -fno-builtin" \
        --synth "$(synth_plug_numbers "$e")" --src-opt "$(basename "$dsp")=$FWFLAGS $dspopt -DSYNTH_ENGINE_$E" --src-opt "$def=$FWFLAGS -DSYNTH_ENGINE_$E" \
        "$@" --out "$out" -I . -I ot -I ../synth/include plug/synth/${e}_plug.cpp "$def" "$dsp"
}
for e in $SYNTH_PLUGS; do
    build_synth "$e" "$OUT/$e.plg" --keep "$OUT/$e.elf"
done
fail=0
emu() { "$PY" -I plug/emu_plug.py "$@" 2>&1 | sed -n 's/^output checksum \([0-9a-f]*\).*/\1/p'; }
# one non-default setting per plugin: every parameter moved, then one more change in block 5 (ramps and crossfades)
nd() {
    case "$1" in
        dirt) echo "--param 0=2 --param 1=60 --param 2=8 --param 3=4 --param 4=70 --param 5=80 --param-at 5:1=20" ;;
        tape) echo "--param 0=80 --param 1=60 --param 2=40 --param 3=70 --param-at 5:0=10" ;;
        lofi) echo "--param 0=8 --param 1=2 --param 2=40 --param 3=1 --param 4=70 --param-at 5:0=6" ;;
        console) echo "--param 0=70 --param 1=80 --param 2=-3 --param 3=60 --param-at 5:0=20" ;;
        tube) echo "--param 0=70 --param 1=80 --param 2=40 --param 3=60 --param-at 5:0=20" ;;
        bbd) echo "--param 0=1 --param 1=60 --param 2=40 --param 3=90 --param-at 5:1=100" ;;
        phaser) echo "--param 0=1 --param 1=70 --param 2=50 --param 3=60 --param 4=100 --param 5=70 --param-at 5:0=0" ;;
    esac
}
for p in dirt tape lofi console tube bbd phaser; do
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
for e in $SYNTH_PLUGS; do
    got=$("${TOOLCHAIN}objdump" -r "$OUT/$e.elf" | grep -c 'R_ARM_GOT' || true)
    reb=$(python3 -c "import struct,sys; print(struct.unpack_from('<I', open(sys.argv[1],'rb').read(), 32)[0])" "$OUT/$e.plg")
    abs=$("${TOOLCHAIN}objdump" -r "$OUT/$e.elf" | grep -c 'R_ARM_ABS32' || true)
    [ "$got" = 0 ] && [ "$reb" = "$abs" ] || { echo "$e FAIL GOT $got rebase $reb ABS32 $abs"; fail=1; }
done
PY="$PY" "$HERE/synth_render_compare.sh" "$OUT" || fail=1
# the firmware's own checkSynth (plug_synth.cpp, cross-compiled) on every synth plugin: a reject would be Missing on the device
"$PY" -I tools/validate_synth.py $(for e in $SYNTH_PLUGS; do echo "$OUT/$e.plg"; done) || fail=1
# Card logos (synth-open/plug_logo.h): a .plgi sidecar per synth engine, and the device test set in logo-test/: ju6.plg with a
# "JU6-T" logo trailer (the sidecar must win over it), a truncated fm4.plgi and a btype.plgi with a bad magic (both: text name)
mkdir -p "$OUT/previews"
for e in $SYNTH_PLUGS; do
    python3 -I tools/make_plgi.py --text "$(synth_plug_logo $e)" -o "$OUT/$e.plgi" --preview "$OUT/previews/$e.png" > /dev/null
done
python3 -I tools/plgi_sheet.py -o "$OUT/previews/sheet.png" $(for e in $SYNTH_PLUGS; do echo "$OUT/previews/$e.png"; done)
mkdir -p "$OUT/logo-test"
python3 -I tools/make_plgi.py --text JU6-T -o "$OUT/logo-test/ju6-trailer.plgi"
build_synth ju6 "$OUT/logo-test/ju6.plg" --logo "$OUT/logo-test/ju6-trailer.plgi" > /dev/null
rm -f "$OUT/logo-test/ju6-trailer.plgi"
head -c 1000 "$OUT/fm4.plgi" > "$OUT/logo-test/fm4.plgi"
{ printf 'PLGX'; tail -c +5 "$OUT/btype.plgi"; } > "$OUT/logo-test/btype.plgi"
# the trailer plugin is the shipped ju6.plg plus the trailer: same image bytes
cmp -s <(head -c "$(wc -c < "$OUT/ju6.plg")" "$OUT/logo-test/ju6.plg" | tail -c +161) <(tail -c +161 "$OUT/ju6.plg") ||
    { echo "logo FAIL logo-test/ju6.plg image differs from ju6.plg"; fail=1; }
# and it renders as ju6.plg (the emulators cut the trailer off, the firmware never reads past the rebase table)
python3 plug/build_plug.py --id ff.test.synthharness --kind offline --opt "-O2 -fno-builtin" --out "$OUT/logo-test/h.plg" \
    -I . -I ot -I ../synth/include plug/synth/synth_harness.cpp > /dev/null
esum() { "$PY" -I plug/emu_synth.py "$1" "$OUT/logo-test/h.plg" --scenario 0 --base 0x10000000 --rw-base 0x20027000 2>&1 |
    sed -n 's/^output checksum //p'; }
# every synth plugin carries its numbers in the manifest (the host lists it by them before it is installed), equal to the extension's
for e in $SYNTH_PLUGS; do
    m=$("$PY" -I plug/emu_synth.py "$OUT/$e.plg" "$OUT/logo-test/h.plg" --scenario 0 --blocks 1 2>&1 | grep -E "^manifest numbers|Error|differ" || true)
    case "$m" in
        "manifest numbers:"*"(equal to the extension)") echo "synth ok  $e.plg $m" ;;
        *) echo "synth FAIL $e.plg manifest numbers: '$m'"; fail=1 ;;
    esac
done
s1=$(esum "$OUT/ju6.plg")
s2=$(esum "$OUT/logo-test/ju6.plg")
rm -f "$OUT/logo-test/h.plg"
if [ -n "$s1" ] && [ "$s1" = "$s2" ]; then echo "logo ok  logo-test/ju6.plg (trailer) renders $s2 = ju6.plg"
else echo "logo FAIL logo-test/ju6.plg renders '$s2', ju6.plg '$s1'"; fail=1; fi
c++ -std=c++20 -O1 -Wall -I . tools/plug_logo_test.cpp -o "$OUT/plug_logo_test"
"$OUT/plug_logo_test" $(for e in $SYNTH_PLUGS; do echo "$OUT/$e.plgi"; done) "$OUT/logo-test/ju6.plg" | grep -E "FAIL|PASSED|ok  .*(plgi|plg):" || fail=1
for bad in fm4 btype; do
    if "$OUT/plug_logo_test" "$OUT/logo-test/$bad.plgi" > /dev/null; then echo "logo FAIL logo-test/$bad.plgi passes"; fail=1
    else echo "logo ok  logo-test/$bad.plgi is refused (text name)"; fi
done
rm -f "$OUT/plug_logo_test"
rm -f "$OUT"/*.elf
[ "$fail" = 0 ] && echo "PASSED" || { echo "FAILED"; exit 1; }
