#!/bin/bash
# wave_label_sim.sh [--update]
#
# The A File row of ALCO's wave mode on the real UI code of this tree (BRIDGE=1 ui_shot, the real alco.plg under Unicorn). Host only.
# Scenarios (every one has its own SD card folder), each shot byte equal to goldens/waves_curated/ (--update writes them):
#   none     no /Waves: the A File row is not listed at all (a1-no-waves)
#   zero     the flat curated set, A File 0: the word Sine (a2-zero-sine)
#   named    A File 37: the AKWF name, never a number (a3-named)
#   past     A File 1024, past the 143 files: Missing (a4-past-list)
#   missing  a patch whose stored path (field 101) is not on the card: "? name" (a5-stored-missing)
#   flatpath a patch that stores a subfolder path, on the flat card: the stored name, no mark (a6-stored-flat)
# Variables: PLG_DIR (folder with the .plg and .plgi, default build/tests/modules), UNICORN_DIR and BRIDGE_AXF (bridge_env.sh).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
. "$HERE/bridge_env.sh"
bridge_env "$ROOT" || exit 2
BIN="$ROOT/build/ui-shot-bridge/ui_shot"
G="$HERE/goldens/waves_curated"; mkdir -p "$G"
UPD=0; [ "${1:-}" = "--update" ] && UPD=1
plg="${PLG_DIR:-$ROOT/build/tests/modules}"
[ -f "$plg/alco.plg" ] || { echo "wave_label_sim.sh: no alco.plg in $plg (set PLG_DIR)" >&2; exit 2; }
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" "${BRIDGE_ARGS[@]}" OUT="$(dirname "$BIN")" >"$(dirname "$BIN").make.log" 2>&1 || { tail -30 "$(dirname "$BIN").make.log"; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/wave_label_sim.XXXXXX")"; trap 'rm -rf "$WORK"' EXIT
fails=0
chk() { # name shot: byte equal to the golden
    if [ "$UPD" = 1 ]; then cp "$2" "$G/$1.png"; echo "updated $1"
    elif cmp -s "$2" "$G/$1.png"; then echo "ok    $1"
    else echo "FAIL  $1 differs from goldens/waves_curated/$1.png"; fails=$((fails+1)); fi
}
stage() { # DIR waves(none|flat) [skipfile]
    rm -rf "$1"; mkdir -p "$1/Plugins/Synth"
    for id in alco arc atype btype chip cstype dtype eight fm4 hand ju6 phz pluck ttype; do cp "$plg/$id.plg" "$plg/$id.plgi" "$1/Plugins/Synth/" 2>/dev/null; done
    if [ "$2" = flat ]; then mkdir -p "$1/Waves"; cp "$ROOT"/releases/waves/Waves/*.wav "$1/Waves/"; [ -z "${3:-}" ] || rm -f "$1/Waves/$3"; fi
}
ALCO='instr 64; tap Params; wait 1200; tap Down 15; wait 6000; tap F8; wait 2500; enc 8; wait 600'   # ALCO on part 1, its editor, A Wave = Wave
run() { "$BIN" "$1" "$2" >"$WORK/out.txt" 2>&1 || { echo "ui_shot failed:"; tail -3 "$WORK/out.txt"; fails=$((fails+1)); }; }

stage "$WORK/none" none;  run "$WORK/none" "$ALCO; shot $WORK/a1.png";                                       chk a1-no-waves "$WORK/a1.png"
stage "$WORK/flat" flat;  run "$WORK/flat" "$ALCO; tap Down; wait 300; shot $WORK/a2.png; enc 37; wait 1500; shot $WORK/a3.png; enc 2000; wait 800; shot $WORK/a4.png"
chk a2-zero-sine "$WORK/a2.png"; chk a3-named "$WORK/a3.png"; chk a4-past-list "$WORK/a4.png"
# a patch with A File 51 saved from the UI, then field 101 appended by hand (the editor itself never writes a path): param 71 = A File, index 51
stage "$WORK/mk" flat;    run "$WORK/mk" "$ALCO; tap Down; wait 300; enc 51; wait 1500; tap F1; wait 1500"
base="$(ls "$WORK"/mk/Patches/*/*.patch 2>/dev/null | head -1)"
[ -n "$base" ] || { echo "FAIL  no patch was saved"; fails=$((fails+1)); }
python3 -I - "$base" "$WORK/Ref.patch" <<'PY'
import sys
def var(n):
    o = b''
    while True:
        b = n & 0x7f; n >>= 7
        if n: o += bytes([b | 0x80])
        else: return o + bytes([b])
p = b"AKWF_0001/AKWF_0051.wav"
sub = b'\x08' + var(71) + b'\x12' + var(len(p)) + p + b'\x18' + var(51)
open(sys.argv[2], 'wb').write(open(sys.argv[1], 'rb').read() + var((101 << 3) | 2) + var(len(sub)) + sub)
PY
LOAD='instr 64; tap Params; wait 1200; tap Down 15; wait 6000; tap F7; wait 2000; tap F8; wait 2500; tap F8; wait 2500; tap Down; wait 500'
stage "$WORK/miss" flat AKWF_0051.wav; mkdir -p "$WORK/miss/Patches/ALCO"; cp "$WORK/Ref.patch" "$WORK/miss/Patches/ALCO/Ref.patch"
run "$WORK/miss" "$LOAD; shot $WORK/a5.png"; chk a5-stored-missing "$WORK/a5.png"
stage "$WORK/fp" flat;    mkdir -p "$WORK/fp/Patches/ALCO"; cp "$WORK/Ref.patch" "$WORK/fp/Patches/ALCO/Ref.patch"
run "$WORK/fp" "$LOAD; shot $WORK/a6.png"; chk a6-stored-flat "$WORK/a6.png"
[ "$fails" = 0 ] && echo "RESULT: GREEN" || echo "RESULT: RED ($fails failing)"
[ "$fails" = 0 ]
