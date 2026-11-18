#!/bin/bash
# fx34_off_bitexact.sh [BASE_REF]
#
# With the 4 FX project switch off, a project with FX in slots 0 and 1 plays bit for bit what the tree before the 4 FX work played:
# the same script runs on the ui_shot host of this tree and of BASE_REF (default release-1.4.0-int, checked out in a temporary worktree
# under build/), the master output of both (wav) must have the same sha256. The script uses sample tracks (filter, panning, delay and
# reverb send, tune, glide, slice free FX in both slots, a roll, a volume FX, Random value, Chance FX), a MIDI Chord with Strum and Arp, and a
# synth part with Panning and Delay send FX. The set of FX3 and FX4 is written too (setfx) while the switch is off: it must stay out.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED". Host only.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
REF="${1:-release-1.4.0-int}"
WT="$ROOT/build/fx34-bitexact-base"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/fx34_off_bitexact.XXXXXX")"
trap 'rm -rf "$WORK"; git -C "$ROOT" worktree remove --force "$WT" >/dev/null 2>&1' EXIT
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >"$WORK/make-new.log" 2>&1 || { tail -20 "$WORK/make-new.log"; exit 2; }
git -C "$ROOT" worktree remove --force "$WT" >/dev/null 2>&1; rm -rf "$WT"
git -C "$ROOT" worktree add -q --detach "$WT" "$REF" || exit 2
make -s -C "$WT/synth-open/tools/ui_shot" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >"$WORK/make-base.log" 2>&1 || { tail -20 "$WORK/make-base.log"; exit 2; }

PAN=31; DLY=30; REV=32; LP=27; HP=28; TUNE=40; GLIDE=19; VOL=18; ROLL=3; RV=16; COND=48; CHORD=14; STRUM=47; ARP=21
S="sampletone 0; sampletone 1; instr 0; audio 1; seed 5"
S="$S; setstep 0 0 60 0 $LP 40 $PAN 20; setstep 0 2 64 0 $DLY 70 $REV 30; setstep 0 4 -1 0 $HP 60 0 0; setstep 0 5 67 0 $TUNE 70 $GLIDE 60"
S="$S; setstep 0 8 60 0 $RV 20 $PAN 50; setstep 0 10 72 0 $COND 60 $LP 90; setstep 0 12 55 0 $VOL 50 $ROLL 5; setstep 0 14 -1 0 1 0 1 0"
S="$S; setstep 1 0 48 1 $PAN 70 0 0; setstep 1 4 52 1 $LP 30 $DLY 40; setstep 1 8 55 1 0 0 $REV 60; setstep 1 12 48 1 $TUNE 30 0 0"
S="$S; setstep 2 0 48 48 $CHORD 24 $STRUM 10; setstep 2 8 49 48 $CHORD 5 $ARP 3"
S="$S; setstep 3 0 60 64 $PAN 20 $DLY 50; setstep 3 6 64 64 $REV 40 0 0"
X="setfx 0 0 2 $PAN 99; setfx 0 0 3 $LP 5; setfx 0 4 3 $DLY 99; setfx 1 0 2 $PAN 5; setfx 2 0 3 $CHORD 3; setfx 0 8 2 $COND 0; setfx 3 0 2 $PAN 99"
run() {   # run BIN NAME EXTRA: the wav of two passes of 16 steps
    local d="$WORK/sd-$2"; rm -rf "$d"; mkdir -p "$d"
    "$1" "$d" "wait 500; $S; $3; patlen 16; wav $WORK/$2.wav; tap Play; wait 2400; tap Play; wait 300; wav off" >"$WORK/$2.out" 2>"$WORK/$2.err"
}
run "$ROOT/build/ui-shot/ui_shot" new "${NEWX:-fx34on 0; $X}"
run "$WT/build/ui-shot/ui_shot" base "wait 320"   # the 8 host commands of the other run (fx34on and 7 setfx) take 40 ms each
a=$(shasum -a 256 "$WORK/new.wav" | cut -c1-16); b=$(shasum -a 256 "$WORK/base.wav" | cut -c1-16)
sz=$(stat -f %z "$WORK/new.wav")
echo "new  $a ($sz bytes)"; echo "base $b ($(stat -f %z "$WORK/base.wav") bytes)"
if [ "$a" = "$b" ] && [ "$sz" -gt 100000 ]; then echo "RESULT: GREEN (switch off output == $REF, $sz bytes of audio)"; exit 0; fi
echo "RESULT: RED"; exit 1
