#!/bin/bash
# export_bpm_sim.sh [UI_SHOT_BIN [OUT_DIR]]
#
# The tempo of the real export and of live playback on the real sequencer, measured from a 4-on-the-floor kick (host only, no device).
# For every tempo (default 120 125 128 174) it renders
#   song     Export Song Stems of a 64 bar Song (64 playlist slots of one 16 step pattern), the exporter's own render loop and WAV header (44100)
#   pattern  Export Pattern Stems of the longest pattern (128 steps, 8 bars, the UI clamps plen 255 to it)
#   live     the firmware's audio callback (Tracker::updateAudioBuffers, 128 frames at the MK66 rate 44,117.6470588 Hz) for 64 bars of the pattern loop
#   swing-song / swing-live   the song and live rows again with a Swing FX (value SWING_VALUE, default 66) on the first step
# and runs export_bpm_check.py on master.wav: the least squares tempo from the onsets and the exact model (the integer arithmetic of
# internalTickClock.h puts every beat's tick in a 128 frame block, every onset must sit there to the sample). Ends with
# "RESULT: GREEN (n checks)" or "RESULT: RED (n failing of m)".
#   TEMPOS="120 125 128 174"  BARS=64  KEEP=DIR (keep the rendered master.wav files there)
# The master-only exports (Export Song / Export Pattern) are not rendered here: the host card opens a file with kWriteCreateNew as "w+b" and
# truncates it at every flush of saveMasterBufferToSD, so only the last chunk of such a WAV survives on the host (the device FatFs
# reuses the freed clusters, see ref/export-bpm.md). The stems exports write the same master.wav from one raw file and share every line of
# the tempo path (Sequencer::tick, the audio block loop) with the master-only ones.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-$ROOT/build/ui-shot-bpm/ui_shot}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/export_bpm_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
[ -x "$BIN" ] || { echo "export_bpm_sim.sh: no ui_shot at $BIN (make -C synth-open/tools/ui_shot OUT=$ROOT/build/ui-shot-bpm)" >&2; exit 2; }
TEMPOS="${TEMPOS:-120 125 126 127 128 174}"
BARS="${BARS:-64}"
KEEP="${KEEP:-}"
checks=0; fails=0
CONTROL_TEMPO=125
PLAYLIST=$(python3 -I -c "import sys; print(' '.join(['1'] * int(sys.argv[1])))" "$BARS")
STEPS4='setstep 0 0 60 0 -1 0 -1 0; setstep 0 4 60 0 -1 0 -1 0; setstep 0 8 60 0 -1 0 -1 0; setstep 0 12 60 0 -1 0 -1 0'
# the same kick with a Swing FX (type 17, value SWING_VALUE) on step 0: the swing phase toggles at every step boundary, so a step boundary falls
# inside an audio block, and the beats (every 4th step, two pairs of steps) must still sit on the exact grid. With the period taken once per block
# (stock, up to ff 1.3.x) swing 66 gave x1.00098 at 125 BPM, x1.00038 at 126 and x1.00076 at 127 (ref/export-bpm.md section 11).
SWING_VALUE="${SWING_VALUE:-66}"
SWSTEPS="setstep 0 0 60 0 17 $SWING_VALUE -1 0; setstep 0 4 60 0 -1 0 -1 0; setstep 0 8 60 0 -1 0 -1 0; setstep 0 12 60 0 -1 0 -1 0"
# the longest pattern: a kick on every 4th step
LONG=""
for ((s = 0; s < 255; s += 4)); do LONG="$LONG; setstep 0 $s 60 0 -1 0 -1 0"; done

scene() {   # scene TEMPO BODY: a fresh card folder, the kick on instrument 1, then BODY
    local sd="$WORK/sd"
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    UI_SHOT_FATFS_REOPEN=1 "$BIN" "$sd" "wait 500; samplekick 0; tap Pattern; wait 300; tempo $1; $2" > "$WORK/log" 2>&1 || { echo "ui_shot failed"; tail -3 "$WORK/log"; }
}
check() {   # check NAME WAV TEMPO [RATE] [PER_ONSET]
    local name="$1" wav="$2" tempo="$3" rate="${4:-}" per="${5:-1}" out rc
    checks=$((checks + 1))
    if [ ! -s "$wav" ]; then echo "FAIL  $name: no WAV"; fails=$((fails + 1)); return; fi
    out=$(python3 -I "$HERE/export_bpm_check.py" "$wav" "$tempo" --beats-per-onset "$per" ${rate:+--rate "$rate"}); rc=$?
    if [ $rc -eq 0 ]; then echo "ok    $name: ${out#* onsets }"; else echo "FAIL  $name: ${out#* onsets }"; fails=$((fails + 1)); fi
    [ -n "$KEEP" ] && { mkdir -p "$KEEP"; cp "$wav" "$KEEP/$name.wav"; }
    [ "$name" = "song-$CONTROL_TEMPO" ] && cp "$wav" "$WORK/control_src.wav"
}

for t in $TEMPOS; do
    scene "$t" "plen 16; $STEPS4; playlist $PLAYLIST; export songstems"
    check "song-$t" "$(ls "$WORK"/sd/Export/*/*/master.wav | head -1)" "$t"
    scene "$t" "plen 255$LONG; export patternstems"
    check "pattern-$t" "$(ls "$WORK"/sd/Export/*/*/master.wav | head -1)" "$t"
    secs=$(python3 -I -c "import sys; print(int(float(sys.argv[2]) * 4 * 60.0 / float(sys.argv[1]) * 1000) + 400)" "$t" "$BARS")
    scene "$t" "plen 16; $STEPS4; audio 1; wav $WORK/live.wav; tap Play; wait $secs; tap Play; wav off"
    check "live-$t" "$WORK/live.wav" "$t" 44117.6470588
    rm -f "$WORK/live.wav"
    scene "$t" "plen 16; $SWSTEPS; playlist $PLAYLIST; export songstems"
    check "swing-song-$t" "$(ls "$WORK"/sd/Export/*/*/master.wav | head -1)" "$t"
    scene "$t" "plen 16; $SWSTEPS; audio 1; wav $WORK/live.wav; tap Play; wait $secs; tap Play; wav off"
    check "swing-live-$t" "$WORK/live.wav" "$t" 44117.6470588
    rm -f "$WORK/live.wav"
done
# Render Selection (the loop render of a bar into a sample, mtAudioExport exportPatternSelection): the WAV must be the bar rounded up to the
# next 128 frame block, no more (no tail) and no less. UI_SHOT_FATFS_REOPEN keeps the earlier flushes of the master WAV on the host card.
for t in $TEMPOS; do
    scene "$t" "plen 16; $STEPS4; export selection 0 15"
    wav=$(ls "$WORK"/sd/Export/*/Selection/sel.wav 2>/dev/null | head -1)
    checks=$((checks + 1))
    if [ -z "$wav" ]; then echo "FAIL  selection-$t: no WAV"; fails=$((fails + 1)); continue; fi
    got=$(python3 -I -c "import sys, wave; print(wave.open(sys.argv[1]).getnframes())" "$wav")
    want=$(python3 -I -c "import math, sys; print(math.ceil(4 * 60.0 / float(sys.argv[1]) * 44100 / 128) * 128)" "$t")
    exact=$(python3 -I -c "import sys; print('%.2f' % (4 * 60.0 / float(sys.argv[1]) * 44100))" "$t")
    if [ "$got" = "$want" ]; then echo "ok    selection-$t: $got frames (bar $exact, rounded up to the block)"; else echo "FAIL  selection-$t: $got frames, want $want"; fails=$((fails + 1)); fi
done
# negative control: the 125 BPM song WAV played 0.104 % faster (what 125.13 BPM for 125 would be) must fail the exact model, so the checks above can tell
src="$WORK/control_src.wav"
if [ -s "$src" ]; then
    python3 -I - "$src" "$WORK/fast.wav" <<'PY'
import struct, sys, wave
w = wave.open(sys.argv[1]); n = w.getnframes(); d = struct.unpack("<%dh" % (n * 2), w.readframes(n))
out = []
for i in range(int(n / 1.00104)):
    j = int(i * 1.00104) * 2
    out += [d[j], d[j + 1]]
o = wave.open(sys.argv[2], "wb"); o.setnchannels(2); o.setsampwidth(2); o.setframerate(44100); o.writeframes(struct.pack("<%dh" % len(out), *out)); o.close()
PY
    checks=$((checks + 1))
    if python3 -I "$HERE/export_bpm_check.py" "$WORK/fast.wav" "$CONTROL_TEMPO" > "$WORK/neg.txt"; then
        echo "FAIL  control: a 0.104 % faster WAV passed"; fails=$((fails + 1))
    else
        neg=$(cat "$WORK/neg.txt"); echo "ok    control (0.104 % faster WAV fails): ${neg#* onsets }"
    fi
fi
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
