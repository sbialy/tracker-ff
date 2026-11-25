#!/bin/bash
# vary_sim.sh [PNG_DIR]
#
# Patch Vary and Randomize of the synth patch editor (button 3 "Vary", Shift + button 3 undo, Shift + encoder the amount), run on the real UI
# code of this tree (core/ and synth-open/, the PLUG_HOST configuration, built by ui_shot) on the Mac. Host only: no device, every scenario has its
# own SD card folder. A scenario is a ui_shot script of key presses, the open patch is read back with "poke patchvals" (host_poke.cpp), the
# popup with "poke popup", the file the unit wrote with the card folder. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
# The per engine loop (every engine, ranges, volume, enums, undo, a plugin engine stand-in) is in host_smoke_test.cpp (run_smoke.sh).
#
#   synth-open/tools/ui_shot/vary_sim.sh [PNG_DIR]
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="$ROOT/build/ui-shot"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/vary_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
expect() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }

# the editor of part 1 (instr 64), engine ACD by default
EDIT='instr 64; tap Params; wait 1500; tap F8; wait 2000'
run() {
    local sd="$1" script="$2"
    rm -rf "$sd"; mkdir -p "$sd"
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "$script" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] || : >"$WORK/poke.out"
}
vals() { grep '^patchvals' "$WORK/poke.out" | sed -n "${1}p" | sed 's/.* v //'; }     # the values of the Nth patchvals line
popup() { grep '^popup' "$WORK/poke.out" | sed -n "${1}p"; }
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 10"; }
nchanged() { python3 -I -c 'import sys; a=sys.argv[1].split(","); b=sys.argv[2].split(","); print(sum(x!=y for x,y in zip(a,b)) if len(a)==len(b) else -1)' "$1" "$2"; }
maxmove() {   # the largest |a-b| over every value, 0 when equal
    python3 -I -c 'import sys; a=[float(x) for x in sys.argv[1].split(",")]; b=[float(x) for x in sys.argv[2].split(",")]; print(max(abs(x-y) for x,y in zip(a,b)))' "$1" "$2"; }

PV='poke patchvals 0'; PP='poke popup'
echo "### scenario 1: Vary 10 %, undo, undo again"
sd="$WORK/s1"
run "$sd" "$EDIT; $PV; $(shot vary-editor); tap F4; wait 300; $PV; $PP; $(shot vary-after-10); hold Shift; tap F4; unhold Shift; wait 300; $PV; $PP; hold Shift; tap F4; unhold Shift; wait 300; $PP"
A0="$(vals 1)"; A1="$(vals 2)"; A2="$(vals 3)"
echo "     $(echo "$A0" | tr ',' '\n' | wc -l | tr -d ' ') parameters, $(nchanged "$A0" "$A1") changed by Vary"
[ -n "$A0" ] && pass "the editor opens and patchvals reads the patch" || fail "patchvals reads the patch" "no output"
[ "$(nchanged "$A0" "$A1")" -gt 0 ] && pass "button 3 changes the patch" || fail "button 3 changes the patch" "$A0 / $A1"
expect "Vary popup" "popup 'Vary 10%'" "$(popup 1)"
expect "Shift + button 3 gives back every value exactly" "$A0" "$A2"
expect "Undo popup" "popup 'Vary undone'" "$(popup 2)"
expect "a second undo has nothing to undo" "popup 'Nothing to undo'" "$(popup 3)"

echo "### scenario 2: amount with Shift + encoder, Randomize at 100 %"
sd="$WORK/s2"
run "$sd" "$EDIT; $PV; hold Shift; enc 2; unhold Shift; wait 100; $(shot vary-amount-15); tap F4; wait 300; $PV; $PP; hold Shift; tap F4; unhold Shift; wait 200; hold Shift; enc 40; enc 40; enc 40; enc 40; enc 40; unhold Shift; wait 100; tap F4; wait 300; $PV; $PP; $(shot vary-random)"
B0="$(vals 1)"; B1="$(vals 2)"; B2="$(vals 3)"
expect "Shift + encoder 2 steps the amount to 15 %" "popup 'Vary 15%'" "$(popup 1)"
expect "100 % is Randomize" "popup 'Randomized'" "$(popup 2)"
[ "$(nchanged "$B0" "$B2")" -gt 10 ] && pass "Randomize changes most parameters ($(nchanged "$B0" "$B2") of $(echo "$B0" | tr ',' '\n' | wc -l | tr -d ' '))" || fail "Randomize changes most parameters" "$B0 / $B2"

echo "### scenario 3: the plain encoder still edits a parameter"
sd="$WORK/s3"
run "$sd" "$EDIT; $PV; enc 10; wait 200; $PV"
C0="$(vals 1)"; C1="$(vals 2)"
[ "$(nchanged "$C0" "$C1")" -eq 1 ] && pass "encoder without Shift changes one parameter" || fail "encoder without Shift changes one parameter" "$(nchanged "$C0" "$C1") changed"

echo "### scenario 4: Save after Vary and undo writes the same patch, Vary writes a different one"
sd="$WORK/s4"; rm -rf "$sd"; mkdir -p "$sd"
UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "$EDIT; tap F1; wait 600" >/dev/null 2>&1 || true
f="$(find "$sd" -name '*.patch' 2>/dev/null | sort | head -1)"
if [ -z "$f" ]; then fail "Save writes a .patch file on the host card" "none under the card"; else
    H0="$(shasum "$f" | cut -c1-12)"
    UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "$EDIT; tap F4; wait 300; hold Shift; tap F4; unhold Shift; wait 300; tap F1; wait 600" >/dev/null 2>&1 || true
    H1="$(shasum "$f" | cut -c1-12)"
    expect "Vary then undo then Save: the patch file is unchanged" "$H0" "$H1"
    UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "$EDIT; tap F4; wait 300; tap F1; wait 600" >/dev/null 2>&1 || true
    H2="$(shasum "$f" | cut -c1-12)"
    [ "$H0" != "$H2" ] && pass "Vary then Save writes a different patch" || fail "Vary then Save writes a different patch" "$H0 = $H2"
fi

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
