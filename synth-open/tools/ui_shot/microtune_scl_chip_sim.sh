#!/bin/bash
# microtune_scl_chip_sim.sh [EVIDENCE_DIR]
#
# Microtuning phase 2 on CHIP: a Scala file picked in Project Settings > Tuning > Scale file reaches the one engine that takes the tuning through the voice
# bend (v.bend, open_synth.cpp Voice::tuneSt), because CHIP is chip.plg (Thumb code, the pitch comes from the integer note and the shim bend).
# Runs the real chip.plg under Unicorn on the BRIDGE=1 ui_shot (bridge_env.sh). Pitch = zero crossings of the master blocks (lrdump), judged as a ratio to key 60:
#   H1  a septimal 5 note scale (8/7 6/5 3/2 7/4 2/1, written into /Tunings by this script): key 61 is x8/7, key 62 x6/5, key 65 x2/1, key 59 x7/8
#   H2  Bohlen-Pierce just (the release example): key 73 is x3, key 47 is x1/3
#   H3  EDO again from the row keeps the 5 notes as equal steps: key 62 is 2^(2/5) over key 60, not the 6/5 of the file
# Variables: PLG_DIR, BRIDGE_AXF / UNICORN_DIR (bridge_env.sh). Without the bridge environment or chip.plg: SKIP and exit 0.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
EVID="${1:-}"
. "$HERE/bridge_env.sh"
bridge_env "$ROOT" || { echo "microtune_scl_chip_sim.sh: SKIP (no bridge environment)"; exit 0; }
plg="${PLG_DIR:-}"
if [ -z "$plg" ]; then for d in "$ROOT/build/chip-plugin" "$ROOT/build/tests/modules" "$HOME/prj/poly/build/chip-plugin"; do [ -f "$d/chip.plg" ] && { plg="$d"; break; }; done; fi
[ -f "$plg/chip.plg" ] || { echo "microtune_scl_chip_sim.sh: SKIP (no chip.plg, set PLG_DIR)"; exit 0; }
BUILD="$ROOT/build/ui-shot-bridge"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/microtune_scl_chip_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" "${BRIDGE_ARGS[@]}" OUT="$BUILD" >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }
[ -x "$BIN" ] || { echo "microtune_scl_chip_sim.sh: no bridge ui_shot at $BIN" >&2; exit 99; }
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
# UITUNE="tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300; $(ed 0 1) $(ed 4 7) $(ed 9 3) $(ed 10 -62) $(ed 11 -56) tap F7; wait 300; tap F7; wait 800;"

stage() { local sd="$1"; rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"; cp "$plg/chip.plg" "$sd/Plugins/Synth/"; [ -f "$plg/chip.plgi" ] && cp "$plg/chip.plgi" "$sd/Plugins/Synth/"; }
# CHIP is picked the way a player does it: the Synth screen of part 1 (WTFM), Down onto PERC, Down onto CHIP, the 1 s rest installs the plugin
PICK='tap Params; wait 1500; tap Down; wait 3300; tap Down; wait 3300'
sd0="$WORK/probe"; stage "$sd0"
rm -f "$WORK/poke" "$WORK/poke.out"
UISHOT_POKE="$WORK/poke" "$BIN" "$sd0" "wait 800; instr 64; $PICK; poke picker" >/dev/null 2>&1 || true
check "C0 chip.plg installs and the Model list picks CHIP (the real plugin bytes run under Unicorn)" "$(grep -q '^part 0 .*engine CHIP' "$WORK/poke.out" 2>/dev/null && echo ok || echo "part 0 is not CHIP")"


TUN="$ROOT/releases/tunings"
printf 'septimal 5 notes\n5\n8/7\n6/5\n3/2\n7/4\n2/1\n' > "$WORK/septimal5.scl"
stage() { local sd="$1"; rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth" "$sd/Tunings"; cp "$plg/chip.plg" "$sd/Plugins/Synth/"; [ -f "$plg/chip.plgi" ] && cp "$plg/chip.plgi" "$sd/Plugins/Synth/"; cp "$SCL" "$sd/Tunings/"; }
PICK='tap Params; wait 1500; tap Down; wait 3300; tap Down; wait 3300'
# the Scale file row (12) through the real UI, list position 1, then back to the pattern editor, then CHIP is picked on the Synth screen
SCLUI="tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300; $(ed 12 ${POS:-1}) tap F7; wait 300; tap F7; wait 800;"
play() {   # NAME "setup" KEY -> $WORK/NAME.lr
    local sd="$WORK/$1"; stage "$sd"
    UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "wait 800; instr 64; $2 $PICK; poke voicecount 0 1; setstep 0 0 $3 64 0 0 0 0; audio 1; audiostat 1; tap Play; wait 700; tap Play; audiostat 0; audiostat lrdump" \
        2>"$WORK/$1.err" >"$WORK/$1.raw"
    grep -E '^lr ' "$WORK/$1.raw" >"$WORK/$1.lr"
}
hz() { awk '$4 > 300 {v++; if (v > 5) {z += $8; n++}} END {printf "%.2f", n ? z / 2 / (n * 128 / 44117.647) : 0}' "$WORK/$1.lr"; }
ratio_check() {   # LABEL SETUP KEYREF KEY WANT [TOL]
    play "r-$1-ref" "$2" "$3"; play "r-$1-k" "$2" "$4"
    local a b r; a=$(hz "r-$1-ref"); b=$(hz "r-$1-k"); r=$(awk -v a="$a" -v b="$b" 'BEGIN {printf "%.4f", (a > 0) ? b / a : 0}')
    [ -n "$EVID" ] && printf '%-6s ref key %3s %9s Hz key %3s %9s Hz ratio %s want %s\n' "$1" "$3" "$a" "$4" "$b" "$r" "$5" >> "$EVID/chip-scl-table.txt"
    if near "$r" "$5" "${6:-0.012}"; then check "$1 key $4 over key $3 = $5 ($a Hz -> $b Hz)" ok; else check "$1 key $4 over key $3" "want $5 got $r ($a Hz -> $b Hz)"; fi
}
rm -f "$WORK/poke" "$WORK/poke.out"
SCL="$WORK/septimal5.scl"
for e in "61 8/7" "62 6/5" "65 2/1" "59 7/8"; do k=${e%% *}; r=${e#* }; ratio_check "H1" "$SCLUI" 60 "$k" "$(awk -v r="$r" 'BEGIN {split(r, a, "/"); printf "%.4f", a[1] / a[2]}')"; done
SCL="$TUN/bohlen-pierce-just.scl"
ratio_check H2a "$SCLUI" 60 73 3.0000
ratio_check H2b "$SCLUI" 60 47 0.3333 0.02
SCL="$WORK/septimal5.scl"
ratio_check H3 "tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300; $(ed 12 1) $(ed 12 -1) tap F7; wait 300; tap F7; wait 800;" 60 62 "$(awk 'BEGIN {printf "%.4f", 2 ^ (2 / 5)}')"
errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
check "no sanitizer report in any run" "$([ "$errs" = 0 ] && echo ok || echo "$errs reports")"
echo
echo "$checks checks"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
