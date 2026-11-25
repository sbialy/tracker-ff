#!/bin/bash
# microtune_chip_sim.sh [EVIDENCE_DIR]
#
# Microtuning phase 1 (ref/microtuning-scope.md), CHIP: the one engine that takes the tuning through the voice bend (v.bend, open_synth.cpp
# Voice::tuneSt) and not through n.pitch, because CHIP is chip.plg (Thumb code, its pitch comes from the note and the bend the shim passes).
# Runs the real chip.plg under Unicorn on the BRIDGE=1 ui_shot (bridge_env.sh), like model_rest_sim.sh. Pitch = zero crossings of the master
# blocks (lrdump), judged as a ratio to the key the 1/1 sits on (the tap reads about 0.8 % low):
#   C1  a project without a tuning: key 72 sounds an octave above key 60 (12 EDO, no bend added: tune off 60 = 0)
#   C2  31 EDO: key 91 (one period above the 1/1) sounds an octave above key 60, key 76 a 2^(16/31) above, key 44 a 2^(-16/31) below
#   C3  13 notes over 1901.955 cents (Bohlen-Pierce): key 73 sounds a 3:1 above key 60
#   C4  1/1 Hz 264 and 1/1 key 48 (key 48 plays what key 60 played)
#   C5  Reset puts the default pitch back (key 72 an octave above key 60 again)
# Variables: PLG_DIR (folder with chip.plg and chip.plgi, default build/chip-plugin, build/tests/modules), BRIDGE_AXF / UNICORN_DIR (bridge_env.sh).
# Without the bridge environment or chip.plg the script says SKIP and exits 0 (no plugin bytes can run then).
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
EVID="${1:-}"
. "$HERE/bridge_env.sh"
bridge_env "$ROOT" || { echo "microtune_chip_sim.sh: SKIP (no bridge environment)"; exit 0; }
plg="${PLG_DIR:-}"
if [ -z "$plg" ]; then for d in "$ROOT/build/chip-plugin" "$ROOT/build/tests/modules" "$HOME/prj/poly/build/chip-plugin"; do [ -f "$d/chip.plg" ] && { plg="$d"; break; }; done; fi
[ -f "$plg/chip.plg" ] || { echo "microtune_chip_sim.sh: SKIP (no chip.plg, set PLG_DIR)"; exit 0; }
BUILD="$ROOT/build/ui-shot-bridge"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/microtune_chip_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" "${BRIDGE_ARGS[@]}" OUT="$BUILD" >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }
[ -x "$BIN" ] || { echo "microtune_chip_sim.sh: no bridge ui_shot at $BIN" >&2; exit 99; }
[ -n "$EVID" ] && mkdir -p "$EVID"
fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
near() { awk -v a="$1" -v b="$2" -v tol="${3:-0.01}" 'BEGIN {d = a - b; if (d < 0) d = -d; exit !(b > 0 && d <= b * tol)}'; }

# the fine Tuning rows through the real UI (File F7, the Tuning group): ed ROW DELTA opens the list of a row, moves DELTA positions (enc moves at most 3 a call)
# and applies. Rows: 0 Notes, 4..7 Period 100c / 1c / .01c / .001c, 8..11 Hz 100 / 1 / .01 / .0001. 13 notes over 1900.000 cents (not a preset) and 264.0000 Hz
ed() {
    local r=$1 d=$2 t="tap Up 20; tap Down $1; tap F8; wait 200;"
    while [ "$d" -ge 3 ]; do t="$t enc 3;"; d=$((d-3)); done
    while [ "$d" -le -3 ]; do t="$t enc -3;"; d=$((d+3)); done
    [ "$d" -ne 0 ] && t="$t enc $d;"
    echo "$t tap F8; wait 200;"
}
UITUNE="tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300; $(ed 0 1) $(ed 4 7) $(ed 9 3) $(ed 10 -62) $(ed 11 -56) tap F7; wait 300; tap F7; wait 800;"

stage() { local sd="$1"; rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"; cp "$plg/chip.plg" "$sd/Plugins/Synth/"; [ -f "$plg/chip.plgi" ] && cp "$plg/chip.plgi" "$sd/Plugins/Synth/"; }
# CHIP is picked the way a player does it: the Synth screen of part 1 (WTFM), Down onto PERC, Down onto CHIP, the 1 s rest installs the plugin
PICK='tap Params; wait 1500; tap Down; wait 3300; tap Down; wait 3300'
sd0="$WORK/probe"; stage "$sd0"
rm -f "$WORK/poke" "$WORK/poke.out"
UISHOT_POKE="$WORK/poke" "$BIN" "$sd0" "wait 800; instr 64; $PICK; poke picker" >/dev/null 2>&1 || true
check "C0 chip.plg installs and the Model list picks CHIP (the real plugin bytes run under Unicorn)" "$(grep -q '^part 0 .*engine CHIP' "$WORK/poke.out" 2>/dev/null && echo ok || echo "part 0 is not CHIP")"

play() {   # NAME "setup" KEY -> $WORK/NAME.lr
    local sd="$WORK/$1"; stage "$sd"
    UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "wait 800; instr 64; $PICK; poke voicecount 0 1; $2 setstep 0 0 $3 64 0 0 0 0; audio 1; audiostat 1; tap Play; wait 700; tap Play; audiostat 0; audiostat lrdump" \
        2>"$WORK/$1.err" >"$WORK/$1.raw"
    grep -E '^lr ' "$WORK/$1.raw" >"$WORK/$1.lr"
}
hz() { awk '$4 > 300 {v++; if (v > 5) {z += $8; n++}} END {printf "%.2f", n ? z / 2 / (n * 128 / 44117.647) : 0}' "$WORK/$1.lr"; }
ratio_check() {   # LABEL SETUP KEYREF KEY WANT
    play "r-$1-ref" "$2" "$3"; play "r-$1-k" "$2" "$4"
    local a b r; a=$(hz "r-$1-ref"); b=$(hz "r-$1-k"); r=$(awk -v a="$a" -v b="$b" 'BEGIN {printf "%.4f", (a > 0) ? b / a : 0}')
    [ -n "$EVID" ] && printf '%-6s ref key %3s %9s Hz key %3s %9s Hz ratio %s want %s\n' "$1" "$3" "$a" "$4" "$b" "$r" "$5" >> "$EVID/chip-table.txt"
    if near "$r" "$5" 0.012; then check "$1 key $4 over key $3 = $5 ($a Hz -> $b Hz)" ok; else check "$1 key $4 over key $3" "want $5 got $r ($a Hz -> $b Hz)"; fi
}
play c1 "" 60; check "C1 CHIP sounds at key 60 ($(hz c1) Hz)" "$([ "$(awk -v h="$(hz c1)" 'BEGIN {print (h > 200) && (h < 330)}')" = 1 ] && echo ok || echo "$(hz c1) Hz")"
ratio_check C1 "" 60 72 2.0000
ratio_check C2a "tune 31;" 60 91 2.0000
ratio_check C2b "tune 31;" 60 76 "$(awk 'BEGIN {printf "%.4f", 2 ^ (16 / 31)}')"
ratio_check C2c "tune 31;" 60 44 "$(awk 'BEGIN {printf "%.4f", 2 ^ (-16 / 31)}')"
ratio_check C3 "tune 13 1901955;" 60 73 3.0000
ratio_check C4a "tune 12 1200000 48;" 48 60 2.0000
play c4-def "" 60; play c4-hz "tune 12 1200000 60 2640000;" 60
r=$(awk -v a="$(hz c4-def)" -v b="$(hz c4-hz)" 'BEGIN {printf "%.4f", (a > 0) ? b / a : 0}')
if near "$r" 1.0091 0.004; then check "C4b 1/1 Hz 264: key 60 is x1.0091 of the default ($r)" ok; else check "C4b 1/1 Hz 264" "want 1.0091 got $r"; fi
ratio_check C5 "tune 31; tune reset;" 60 72 2.0000
# C6: the fine rows through the UI (13 notes over 1900.000 cents, a period no preset has): key 73 over key 60 is 2^(1900/1200)
ratio_check C6 "$UITUNE" 60 73 "$(awk 'BEGIN {printf "%.4f", 2 ^ (1900 / 1200)}')"
errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
check "no sanitizer report in any run" "$([ "$errs" = 0 ] && echo ok || echo "$errs reports")"
echo
echo "$checks checks"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
