#!/bin/bash
# part_auto_sim.sh
#
# The part destinations of the synth automation (Part Pan, Gain, Reverb send, Delay send, open_mod.h ModKind::kPart) on the real audio engine and
# synth code of this tree (ui_shot) on the Mac. Host only, no device. The slot is set the way the editor sets it ("poke partslot", host_poke.cpp)
# and the part is read back with "poke fx34play partauto" (host_poke_fx34.cpp): every source of the 4 part params (the last one is the automation,
# audioEngine::SynthValuesSource::kAuto), the value the engine applies and the multipliers of the part's two panning amplifiers.
#   1  an LFO on Part Pan moves the panning amplifiers (square 50 % = -100 .. +100 points: left only, then right only)
#   2  an Envelope on Part Gain holds the gain offset while the note is held (-25 % of 108 dB = -27 dB)
#   3  an LFO on Part Reverb send and Delay send moves the sends above their base
#   4  Off restores the base value: the Auto source is empty, the amplifiers are back
#   5  a project load clears the automation values of the old project
# Ends with "RESULT: GREEN (n checks)" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
WORK="$(mktemp -d "${TMPDIR:-/tmp}/part_auto_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

PAN=264; GAIN=265; REV=266; DLY=267   # picker codes: kModPickPart + n
fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
expect() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }
run() {   # run NAME "script": the partslot and partauto lines
    local sd="$WORK/sd-$1"; rm -rf "$sd"; mkdir -p "$sd"
    rm -f "$WORK/poke" "$WORK/poke.out"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "wait 500; $2" >"$WORK/$1.out" 2>"$WORK/$1.err" ||
        { echo "ui_shot failed ($1):"; tail -5 "$WORK/$1.err"; }
    if grep -qE 'runtime error|AddressSanitizer' "$WORK/$1.err" 2>/dev/null; then fail "sanitizer report in $1" "$(grep -E 'runtime error|AddressSanitizer' "$WORK/$1.err" | head -2)"; fi
    [ -f "$WORK/poke.out" ] && cat "$WORK/poke.out"
    grep -E '^(partslot|partauto|fmsave|fmopen) ' "$WORK/$1.out"
}
polls() { local n=$1 gap=$2 i s=""; for ((i = 0; i < n; i++)); do s="$s; wait $gap; poke fx34play partauto 0"; done; printf '%s' "$s"; }
# field NAME OUTPUT: the values "= V" of param NAME (pan dly rev gain) of every partauto line, one per line
field() { sed -n "s/^partauto 0.* $1 \[[^]]*\] = \([-0-9.]*\).*/\1/p" <<<"$2"; }
src() { sed -n "s/^partauto 0.* $1 \[\([^]]*\)\].*/\1/p" <<<"$2"; }   # the source list of param NAME
amps() { sed -n 's/^partauto 0.* amp \([-0-9]*\),\([-0-9]*\) .*/\1,\2/p' <<<"$1"; }
lastcol() { awk '{ print $NF }' <<<"$1"; }                              # the Auto source (last of 7) of each source list

NOTE="audio 1; instr 64; setstep 0 0 60 64 0 0 0 0"

echo "-- 1: LFO on Part Pan"
out=$(run p1 "$NOTE; poke partslot 0 0 1 $PAN 50 3 14; tap Play$(polls 14 30); tap Play")
vals=$(field pan "$out"); mn=$(sort -n <<<"$vals" | head -1); mx=$(sort -n <<<"$vals" | tail -1)
expect "1a a square LFO at 50 % on Part Pan sweeps the applied pan from -100 to +100" "-100.00 100.00" "$mn $mx"
a=$(amps "$out"); left_only=$(grep -c '^0,65536$\|^[0-9]*,0$' <<<"$a" || true); lmax=$(cut -d, -f1 <<<"$a" | sort -n | tail -1); lmin=$(cut -d, -f1 <<<"$a" | sort -n | head -1)
rmax=$(cut -d, -f2 <<<"$a" | sort -n | tail -1); rmin=$(cut -d, -f2 <<<"$a" | sort -n | head -1)
expect "1b the part's two panning amplifiers follow: left 0 .. 65536 and right 0 .. 65536 (full left, then full right)" "0 65536 0 65536" "$lmin $lmax $rmin $rmax"
empty=$(lastcol "$(src pan "$out")" | grep -c '^-$' || true)
expect "1c the Auto source (last of 7 sources) of the pan holds a value on every poll (0 empty)" "0" "$empty"

echo "-- 2: Envelope on Part Gain"
out=$(run p2 "$NOTE; poke partslot 0 0 2 $GAIN -25 2 0 0 0 100 500; tap Play$(polls 3 40); tap Play")
base=$(awk '{ print $1 }' <<<"$(src gain "$out" | head -1)"); got=$(field gain "$out" | tail -1); auto=$(lastcol "$(src gain "$out" | tail -1)")
want=$(awk -v b="$base" 'BEGIN { printf "%.2f", b - 27.0 }')
expect "2a the envelope at sustain 100 % and -25 % holds the part gain 27 dB below its base ($base dB)" "$want -27.00" "$got $auto"

echo "-- 3: LFO on the sends"
out=$(run p3 "$NOTE; poke partslot 0 0 1 $REV 30 3 14; poke partslot 0 1 1 $DLY 20 3 14; tap Play$(polls 12 30); tap Play")
rv=$(field rev "$out"); dv=$(field dly "$out")
expect "3a Part Reverb send: square 30 % on a base of 0 gives 0 and 30 points" "0.00 30.00" "$(sort -n <<<"$rv" | head -1) $(sort -n <<<"$rv" | tail -1)"
expect "3b Part Delay send: square 20 % on a base of 0 gives 0 and 20 points" "0.00 20.00" "$(sort -n <<<"$dv" | head -1) $(sort -n <<<"$dv" | tail -1)"

echo "-- 4: Off restores the base value"
out=$(run p4 "$NOTE; poke partslot 0 0 1 $PAN 50 3 14; poke partslot 0 1 2 $GAIN -25 2 0 0 0 100 500; tap Play; wait 100; poke fx34play partauto 0; poke partslot 0 0 0 0; poke partslot 0 1 0 0; wait 100; poke fx34play partauto 0; tap Play")
on=$(grep '^partauto' <<<"$out" | head -1); off=$(grep '^partauto' <<<"$out" | tail -1)
expect "4a with the Pan and the Gain slot on, the engine holds both destinations (mask bits 0 and 1)" "mask 3" "$(sed -n 's/.* \(mask [0-9]*\)$/\1/p' <<<"$on")"
expect "4b both slots Off: mask 0, Auto sources empty, amplifiers at the base (centre)" "mask 0 65536,65536 -" "$(sed -n 's/.* mask \([0-9]*\)$/mask \1/p' <<<"$off") $(amps "$off") $(lastcol "$(src pan "$off")")"
expect "4c the gain is its base value again" "$(awk '{ print $1 }' <<<"$(src gain "$off")")" "$(field gain "$off")"

echo "-- 5: a project load clears the automation values"
out=$(run p5 "$NOTE; poke fmsave - PA1; wait 4000; poke partslot 0 0 1 $PAN 50 3 14; tap Play; wait 200; tap Play; poke fx34play partauto 0; poke fmopen - PA1; wait 6000; poke fx34play partauto 0")
before=$(grep '^partauto' <<<"$out" | head -1); after=$(grep '^partauto' <<<"$out" | tail -1)
expect "5a before the load the automation is live (mask 1)" "mask 1" "$(sed -n 's/.* mask \([0-9]*\)$/mask \1/p' <<<"$before")"
expect "5b after the load of the project saved without the slot: mask 0 and an empty Auto source" "mask 0 -" "$(sed -n 's/.* mask \([0-9]*\)$/mask \1/p' <<<"$after") $(lastcol "$(src pan "$after")")"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
