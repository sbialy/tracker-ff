#!/bin/bash
# plug_tempo_sim.sh
#
# PLUG_EXT_TEMPO and PLUG_EXT_TAIL (plug_abi.h, ABI 1.3) on the real code of this tree, on the Mac: ui_shot with "audio 1" runs the
# firmware's own audio callback (Tracker::updateAudioBuffers: the sequencer ticks, the insert node AudioEffectPlug::update calls the
# plugins), the plugins are the native twins of Sweep Gate and Dust Bed (tracker-effects-os) wrapped by the tap of host_tap.cpp, which
# writes down every call: the tempo and beat the host delivered, the input and output level of process(). The scenarios:
#   1  stopped, then Play, then stop. Sweep Gate (Noise 60 %, so it makes sound on a silent bus) gates its noise on the Offbeat pattern:
#      the host's beat advances by bpm/60 per second of audio (the real sequencer clock, 2304 ticks a beat), starts at 0 on Play, the
#      pattern's closed and open slots sit where the beat says, the host reports playing 0 and beat 0 when stopped.
#   2  Dust Bed (a bed, PLUG_EXT_TAIL) runs on a silent bus, Play Only fades it out while stopped, and while every plugin says quiet() the
#      host skips the silent bus as before (no call at all). Once one insert is not quiet the whole chain of the block runs.
# Host only, no device. Ends with "RESULT: GREEN (n checks)" (exit 0) or "RESULT: RED (n failing of m)" (exit 1).
#   FX_SET   the folder with sweepgate.plg and dustbed.plg (a tracker-effects-os build/set, built with the same sources as the twins)
#   OUT      the ui_shot build (default build/ui-shot-tempo of this tree, make -C synth-open/tools/ui_shot OUT=...)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${OUT:-$ROOT/build/ui-shot-tempo}"
BIN="${UI_SHOT_BIN:-$OUT/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (it never made one)
FX_SET="${FX_SET:?set FX_SET to the folder with sweepgate.plg and dustbed.plg}"
[ -x "$BIN" ] || { echo "plug_tempo_sim.sh: build $BIN first (make -C synth-open/tools/ui_shot OUT=$OUT, sources of the twins in $OUT/external)" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/plug_tempo_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
uid() { python3 -I -c 'import sys
h=2166136261
for c in sys.argv[1].encode(): h=((h^c)*16777619)&0xFFFFFFFF
print("%08x"%h)' "$1"; }
SG="$(uid ff.sweepgate)"; DB="$(uid ff.dustbed)"
# the project's tempo is whatever the default project has: the tap reports it, the checks read it from there
INS_ON='tap Master; wait 800; tap F6; tap F6 5; wait 600'

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }

# scenario SIDECAR_BODY SCRIPT -> $WORK/poke.out
scenario() {
    local sd="$WORK/sd"
    rm -rf "$sd" "$WORK/poke" "$WORK/poke.out"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    cp "$FX_SET/sweepgate.plg" "$FX_SET/dustbed.plg" "$sd/Plugins/Master/"
    "$BIN" "$sd" "wait 100" >/dev/null 2>&1 || { echo "priming run failed"; exit 2; }   # a first power up writes the Workspace project
    printf 'tffplug 1\nformat 2\nmixer 10\n%s\n' "$1" >"$sd/Workspace/plugins.txt"
    UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "$2" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; exit 2; }
    [ -f "$WORK/poke.out" ] || { echo "no poke output"; tail -5 "$WORK/err.txt"; exit 2; }
}
# analyse FILE MODE: python checks on the tap lines, prints "ok|FAIL text" lines
analyse() { python3 -I "$HERE/plug_tempo_check.py" "$WORK/poke.out" "$@"; }
verdict() { while IFS= read -r l; do checks=$((checks+1)); case "$l" in ok*) printf '%s\n' "$l" ;; *) fails=$((fails+1)); printf '%s\n' "$l" ;; esac; done; }

echo "### scenario 1: Sweep Gate follows the sequencer (Offbeat gate on the beat, Noise 60 % as the sound source)"
S1="slot 1 $SG 00010000 Sweep Gate
p Noise=60
p Gate=2
p Rate=1
p Depth=100
p Smooth=20"
scenario "$S1" "audio 1; wait 6500; $INS_ON; poke inserts; wait 400; poke tap reset; wait 1500; poke tap dump; poke tap reset; tap Play; wait 4000; poke tap dump; tap Play; wait 800; poke tap reset; wait 1200; poke tap dump"
grep '^inserts' "$WORK/poke.out" | head -1 | sed 's/^/      /'
verdict < <(analyse sweepgate)

echo "### scenario 2: Dust Bed on a silent bus, Play Only, a quiet plugin is not called"
S2="slot 1 $SG 00010000 Sweep Gate
p Noise=0
slot 2 $DB 00010000 Dust Bed
p Play Only=1"
scenario "$S2" "audio 1; wait 6500; $INS_ON; poke inserts; wait 400; poke tap reset; wait 1000; poke tap dump; tap Play; wait 600; poke tap reset; wait 1500; poke tap dump; tap Play; wait 1500; poke tap reset; wait 1500; poke tap dump"
grep '^inserts' "$WORK/poke.out" | head -1 | sed 's/^/      /'
verdict < <(analyse dustbed)

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
