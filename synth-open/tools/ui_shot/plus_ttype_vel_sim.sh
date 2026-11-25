#!/bin/bash
# plus_ttype_vel_sim.sh [FLAVOUR...]   (default "PLUS MINI")   SIM ONLY, host tooling, no device
# T-Type (ttype.plg) has no Volume parameter and divides the voice gain by the velocity of the last hit, so the host gain MUST be the note
# velocity. The MK66 shim does that (open_synth.cpp), the IMX adapter (core/synth/engines/PlugSynthVoice.cpp) once did not for an engine
# without Volume and played T-Type 1 / velocity louder (+2 dB at the default velocity, +4 dB at velocity 0.1). This test renders the same
# plugin, the same default patch and the same step in the original sim and in the Tracker+ and Mini sims (the stock synth stack over the
# closed libPolySynth, the harness built from THIS tree) and compares the WAVs byte for byte:
#   T-Type with the step Volume FX (id 18) at 10, 50 and 100 (the Synth play page ignores midion velocity, so the step FX sets it)
#   FM4 (has a Volume parameter) as the control at the same values
#   Mini equals Plus for T-Type
# TTV_MUTANT=1: the harness is built from a copy of PlugSynthVoice.cpp without the no-Volume gain (the fix reverted), the test must then say RED
# (run_tests.sh plus_ttype_vel_sim_mutant asserts it, the proof that this test can fail).
# Exit 0 green, 1 red, 0 with SKIP when there is no card with ttype.plg and fm4.plg or no devtools checkout with the UISHOT_PLUS guards.
# Env: CARD (default ~/prj/poly/build/emu/current/card), DEVTOOLS_DIR (a console checkout, devtools rel-2.0.0-int or main), OG_BIN (default this
# tree's BRIDGE=1 original sim build/ui-shot-bridge/ui_shot, built when missing), PLUS_BIN MINI_BIN (prebuilt ui_shot, else built into
# build/ui-shot-{plus,mini}-console-bridge), KEEP=<dir> keeps the WAVs, TOOLCHAIN (12.3, bridge_env.sh and plus_synth/build.sh).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
FLAVOURS="${*:-PLUS MINI}"
CARD="${CARD:-$HOME/prj/poly/build/emu/current/card}"
PLG="$CARD/Plugins/Synth"
if [ ! -s "$PLG/ttype.plg" ] || [ ! -s "$PLG/fm4.plg" ]; then echo "plus_ttype_vel_sim: SKIP (no ttype.plg and fm4.plg in $PLG, set CARD)"; exit 0; fi
if [ -z "${DEVTOOLS_DIR:-}" ]; then
    for d in "/Volumes/StoreCI/prj/poly-devtools/.wt/rel-200-int" "$HOME/prj/poly-devtools/.wt/rel-200-int" "$HOME/prj/poly-devtools/.wt/main" "$HOME/prj/poly-devtools"; do
        grep -q UISHOT_PLUS "$d/firmware/include/UsbTest.h" 2>/dev/null && { DEVTOOLS_DIR="$d"; break; }
    done
fi
if [ -z "${DEVTOOLS_DIR:-}" ] || ! grep -q UISHOT_PLUS "$DEVTOOLS_DIR/firmware/include/UsbTest.h" 2>/dev/null; then
    echo "plus_ttype_vel_sim: SKIP (no devtools checkout with the UISHOT_PLUS guards, set DEVTOOLS_DIR)"; exit 0; fi
export DEVTOOLS_DIR
. "$HERE/bridge_env.sh"
bridge_env "$ROOT" || { echo "plus_ttype_vel_sim: no bridge environment" >&2; exit 1; }
JOBS="$(sh "$HERE/../jobs.sh" n)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/plus_ttype_vel.XXXXXX")"
trap '[ -n "${KEEP:-}" ] && { mkdir -p "$KEEP"; cp "$WORK"/*.wav "$WORK"/*.log "$KEEP"/ 2>/dev/null; }; rm -rf "$WORK"' EXIT
fails=0
ok()  { printf 'ok    %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails+1)); }

# the sims: the original (BRIDGE=1, runs the .plg Thumb code under Unicorn) and each IMX flavour, the guest harness of this tree
OG_BIN="${OG_BIN:-$ROOT/build/ui-shot-bridge/ui_shot}"
if [ ! -x "$OG_BIN" ]; then
    make -s -C "$HERE" -j"$JOBS" "${BRIDGE_ARGS[@]}" TOOLCHAIN="${TOOLCHAIN:-}" >"$WORK/make-og.log" 2>&1 || { tail -20 "$WORK/make-og.log"; echo "plus_ttype_vel_sim: original sim build failed" >&2; exit 1; }
fi
HARN="$WORK/harness"
if [ -n "${TTV_MUTANT:-}" ]; then
    mkdir -p "$WORK/mutant"
    python3 -I - "$ROOT/core/synth/engines/PlugSynthVoice.cpp" "$WORK/mutant/PlugSynthVoice.cpp" <<'PY' || { echo "plus_ttype_vel_sim: mutant not made (the fix line moved)" >&2; exit 1; }
import sys
s = open(sys.argv[1]).read()
a = "target = std::clamp(1.0f * (1.0f + 1.0f * (vel - 1.0f)), 0.0f, 1.0f);"
assert s.count(a) == 1
open(sys.argv[2], "w").write(s.replace(a, "target = 1.0f;"))
PY
    export PLUGVOICE_SRC="$WORK/mutant/PlugSynthVoice.cpp"
fi
bash "$HERE/../plus_synth/build.sh" "$HARN" >"$WORK/harness.log" 2>&1 && [ -s "$HARN/harness.elf" ] || { tail -20 "$WORK/harness.log"; echo "plus_ttype_vel_sim: harness build failed" >&2; exit 1; }
declare -A BINS
BINS[OG]="$OG_BIN"
for f in $FLAVOURS; do
    case "$f" in
        PLUS) b="${PLUS_BIN:-}"; dflt="$ROOT/build/ui-shot-plus-console-bridge/ui_shot"; prod="";;
        MINI) b="${MINI_BIN:-}"; dflt="$ROOT/build/ui-shot-mini-console-bridge/ui_shot"; prod="PRODUCT=MINI";;
        *) echo "plus_ttype_vel_sim: flavour PLUS or MINI" >&2; exit 2;;
    esac
    if [ -z "$b" ]; then
        b="$dflt"
        make -s -C "$HERE" -j"$JOBS" PLATFORM=IMX $prod CONSOLE=1 "${BRIDGE_ARGS[@]}" DEVTOOLS_DIR="$DEVTOOLS_DIR" >"$WORK/make-$f.log" 2>&1 \
            || { tail -20 "$WORK/make-$f.log"; echo "plus_ttype_vel_sim: $f sim build failed" >&2; exit 1; }
    fi
    BINS[$f]="$b"
done

# render FLAVOUR NAME FILE V: one note (60) on synth part 0 with the step Volume FX V (velocity V/100), the WAV in $WORK/<flavour>-<name>-<V>.wav
render() {
    local fl=$1 name=$2 file=$3 v=$4 sd="$WORK/sd-$1-$2-$4" envv=()
    [ "$fl" = OG ] || envv=(UISHOT_PLUS_SYNTH="$HARN/harness.elf")
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Synth"; cp "$PLG/$file.plg" "$sd/Plugins/Synth/"
    (cd "$WORK" && env ${envv[@]+"${envv[@]}"} UISHOT_POKE="$WORK/poke-$fl-$name-$v" ASAN_OPTIONS=detect_leaks=0 timeout 400 "${BINS[$fl]}" "$sd" \
        "wait 500; instr 64; tap Params; wait 1500; pickmodel $name; wait 3000; poke modelrow 0; tap Pattern; wait 300; setstep 0 0 60 64 18 $v 0 0; audio 1; wav $WORK/$fl-$name-$v.wav; tap Play; wait 2500; tap Play; wait 1500; wav off") \
        >"$WORK/$fl-$name-$v.log" 2>&1
    grep -q "^pickmodel $name ok" "$WORK/$fl-$name-$v.log" && [ -s "$WORK/$fl-$name-$v.wav" ]
}
peak() { python3 -I -c 'import sys,wave,array
w=wave.open(sys.argv[1]); a=array.array("h"); a.frombytes(w.readframes(w.getnframes())); print(max((abs(x) for x in a), default=0))' "$1"; }
hashf() { md5 -q "$1" 2>/dev/null || md5sum "$1" | cut -d' ' -f1; }

jobs_list=()
for spec in "TTYPE ttype" "FM4 fm4"; do
    set -- $spec
    for v in 10 50 100; do
        for fl in OG $FLAVOURS; do jobs_list+=("$fl $1 $2 $v"); done
    done
done
printf '%s\n' "${jobs_list[@]}" | xargs -P "$JOBS" -L1 bash -c "$(declare -f render); WORK='$WORK' HARN='$HARN' PLG='$PLG'; $(declare -p BINS); render \$0 \$1 \$2 \$3 || echo \"render failed: \$0 \$1 \$3\" >> '$WORK/render-fail.txt'"
[ -s "$WORK/render-fail.txt" ] && { cat "$WORK/render-fail.txt"; bad "renders failed (logs in $WORK, KEEP=<dir> keeps them)"; }

for spec in "TTYPE" "FM4"; do
    for v in 10 50 100; do
        og="$WORK/OG-$spec-$v.wav"; [ -s "$og" ] || { bad "$spec V $v: no original render"; continue; }
        pk=$(peak "$og"); [ "$v" = 0 ] || [ "${pk:-0}" -gt 0 ] || bad "$spec V $v: the original render is silent"
        for fl in $FLAVOURS; do
            w="$WORK/$fl-$spec-$v.wav"
            if [ ! -s "$w" ]; then bad "$spec V $v $fl: no render"
            elif [ "$(hashf "$og")" = "$(hashf "$w")" ]; then ok "$spec V $v: $fl byte identical to the original (peak $pk)"
            else bad "$spec V $v: $fl differs from the original (peaks $pk vs $(peak "$w"))"; fi
        done
    done
done
if [[ " $FLAVOURS " == *" PLUS "* && " $FLAVOURS " == *" MINI "* ]]; then
    for v in 10 50 100; do
        [ -s "$WORK/PLUS-TTYPE-$v.wav" ] && [ "$(hashf "$WORK/PLUS-TTYPE-$v.wav")" = "$(hashf "$WORK/MINI-TTYPE-$v.wav")" ] \
            && ok "TTYPE V $v: Mini byte identical to Plus" || bad "TTYPE V $v: Mini differs from Plus"
    done
fi
[ "$fails" = 0 ] && { echo "plus_ttype_vel_sim: GREEN"; exit 0; }
echo "plus_ttype_vel_sim: RED ($fails)"; exit 1
