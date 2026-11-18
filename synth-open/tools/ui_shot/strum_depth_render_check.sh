#!/bin/bash
# strum_depth_render_check.sh NEW_BIN FLAT_BIN OLD_BIN [WAV_DIR]
#
# The Strum ramp depth on a render (host only, the real sequencer and the built in ACD of ui_shot, the strum_vel_render.py scene: a maj7 chord
# C4 E4 G4 B4 on a synth part, Poly (4 voices, the sequencer's velocity ramp) and Para (one voice, ACD's own ramp), Strum down 40, up 40, tight 4):
#   NEW_BIN   this tree's ui_shot (strum depth in the project)
#   FLAT_BIN  this tree built with STRUM_RAMP=0 (make -C synth-open/tools/ui_shot STRUM_RAMP=0 OUT=...): the plain Strum velocity
#   OLD_BIN   the ui_shot of the tree before the depth (rel140-strumvel b1a46f2): the ramp as it was, always 100 %
# Checks, every case a byte for byte compare of the 16 bit WAV:
#   1. NEW at depth 0   == FLAT                 (depth 0 is bit identical to -DSTRUM_RAMP=0)
#   2. NEW at depth 100 == OLD                  (depth 100 is today's ramp)
#   3. NEW at depth 50 differs from both, and a second render of NEW at 50 is identical (the check can tell, and the render is deterministic)
#   4. NEW with the depth never set (a new project) == OLD   (a new project starts at 100)
# Ends with "RESULT: GREEN" or "RESULT: RED (n failing of m)". WAVs are kept in WAV_DIR when given.
set -uo pipefail
NEW="${1:?NEW_BIN}"; FLAT="${2:?FLAT_BIN}"; OLD="${3:?OLD_BIN}"; KEEP="${4:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/sdrc.XXXXXX")"; trap 'rm -rf "$WORK"' EXIT
fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n' "$1"; }
same() { cmp -s "$1" "$2"; }

render() {   # render BIN OUT.wav PARA(0|1) VALUE [DEPTH]
    local bin="$1" out="$2" para="$3" value="$4" depth="${5:-}" pre sd
    pre="wait 500; instr 64; tap Params; wait 1500; tap Up 3; wait 1800; tap Pattern; wait 300; poke synthparam 0 Sustain 1"
    if [ "$para" = 1 ]; then pre="$pre; poke synthparam 0 Chord_Mode 1"; else pre="$pre; poke synthvoices 0 4"; fi
    [ -n "$depth" ] && pre="$pre; poke strumdepth $depth"
    sd="$WORK/sd"; rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    "$bin" "$sd" "$pre; setstep 0 0 60 64 14 24 47 $value; wait 200; audio 1; wav $out; tap Play; wait 1800; tap Play; wait 1200; wav off" >/dev/null 2>&1 \
        || { echo "ui_shot failed: $bin"; fails=$((fails+1)); }
    [ -s "$out" ] || { echo "no WAV from $bin"; fails=$((fails+1)); }
}

for eng in poly para; do
    para=0; [ "$eng" = para ] && para=1
    for c in down40:40 up104:104 tight4:4; do
        name="${c%%:*}"; val="${c##*:}"; id="$eng $name"
        d="$WORK/$eng-$name"; mkdir -p "$d"
        render "$NEW"  "$d/new0.wav"   $para "$val" 0
        render "$NEW"  "$d/new100.wav" $para "$val" 100
        render "$NEW"  "$d/new50.wav"  $para "$val" 50
        render "$NEW"  "$d/new50b.wav" $para "$val" 50
        render "$NEW"  "$d/newdef.wav" $para "$val"
        render "$FLAT" "$d/flat.wav"   $para "$val"
        render "$OLD"  "$d/old.wav"    $para "$val"
        same "$d/new0.wav" "$d/flat.wav"     && pass "$id: depth 0 == STRUM_RAMP=0 build, byte for byte"   || fail "$id: depth 0 differs from the STRUM_RAMP=0 build"
        same "$d/new100.wav" "$d/old.wav"    && pass "$id: depth 100 == the ramp before the depth, byte for byte" || fail "$id: depth 100 differs from the old ramp"
        same "$d/newdef.wav" "$d/old.wav"    && pass "$id: a new project (depth never set) == the old ramp" || fail "$id: the new project default differs from the old ramp"
        if ! same "$d/new50.wav" "$d/flat.wav" && ! same "$d/new50.wav" "$d/old.wav"; then pass "$id: depth 50 differs from both (the check can tell)"; else fail "$id: depth 50 equals flat or old"; fi
        same "$d/new50.wav" "$d/new50b.wav"  && pass "$id: depth 50 renders the same twice" || fail "$id: depth 50 is not deterministic"
        if [ -n "$KEEP" ]; then mkdir -p "$KEEP"; for k in new0 new50 new100 flat old; do cp "$d/$k.wav" "$KEEP/${eng}_${name}_$k.wav"; done; fi
    done
done
# a strum 0 step is untouched by any depth (Strum 0 never calls the ramp)
render "$NEW" "$WORK/z100.wav" 0 0 100; render "$NEW" "$WORK/z0.wav" 0 0 0; render "$OLD" "$WORK/zold.wav" 0 0
same "$WORK/z100.wav" "$WORK/z0.wav" && same "$WORK/z0.wav" "$WORK/zold.wav" && pass "poly strum 0: depth 100 == depth 0 == the old build" || fail "poly strum 0 differs with the depth"
echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
