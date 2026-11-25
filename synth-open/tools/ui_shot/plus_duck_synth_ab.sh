#!/bin/bash
# plus_duck_synth_ab.sh [SIM_A [SIM_B]]   (ref/ff-imx-port-plan.md P11)  SIM FIGURES ONLY, no device
# plus_duck_ab.sh with the duck source Synth 1 (part 0, instrument 64, a note on steps 0 4 8 12) instead of a sample track. The plus sim plays the
# part on the closed libPolySynth (UISHOT_PLUS_SYNTH names its harness), the OG sim on the open engines: another sound and envelope, so the two
# figures agree less tightly than with the sample source (band 0.12, not 0.05). The threshold is set to its minimum so both parts are above it, the
# depth read is then the part's envelope. Same detector tap (the post fader left output of the part). Default SIM_A is this tree's plus sim, SIM_B
# the OG sim (build/ui-shot/ui_shot). The method below is plus_duck_ab.sh's:
# (build/ui-shot-plus-console-bridge/ui_shot), SIM_B the OG sim (build/ui-shot/ui_shot).
# Project: instrument 1 a 330 Hz sine with the delay and reverb send at 100 on track 2 step 0 (the returns), instrument 2 a decaying 60 Hz kick on
# track 1 every 4th step (the duck source), 120 BPM, 16 steps, live capture of the master (the `wav` command) for 6 s from Play.
# Three renders per sim, the same project: DRY (no sends), WET (sends, ducking off), DUCK (sends, ducking on: source track 1, depth 80, release 50,
# threshold 50 by the Master > Limiter > ducking page). The dry part is the same in all three, so
#     g = <DUCK - DRY, WET - DRY> / <WET - DRY, WET - DRY>      (least squares, both channels, the window after the first 0.3 s)
# is the mean gain the ducking put on the returns, and the depth read off the sim is 1 - g. Error band: (a) the repeat of WET (determinism, the
# residual of two identical renders), (b) g over four sub windows of the capture (the pumping makes g move inside the kick period, so this is the
# spread, not a noise figure). The two sims must agree within BAND (default 0.05 of g). The sims have different mixer chains (21 vs 13 inputs, stereo
# voice chains), so the WAVs are NOT bit identical and this is the check that is possible, a shared golden is not.
set -uo pipefail
DRYBAND=0
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
A="${1:-$ROOT/build/ui-shot-plus-console-bridge/ui_shot}"
B="${2:-$ROOT/build/ui-shot/ui_shot}"
BAND="${BAND:-0.12}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/plus_duck_ab.XXXXXX")"
trap '[ -n "${KEEP:-}" ] && { mkdir -p "$KEEP"; cp "$WORK"/*.wav "$WORK"/*.txt "$KEEP"/ 2>/dev/null; }; rm -rf "$WORK"' EXIT
for b in "$A" "$B"; do [ -x "$b" ] || { echo "plus_duck_ab: no $b" >&2; exit 2; }; done

PROJECT='sampletonehz 0 330 6000; tap Pattern; wait 300; tempo 120; plen 16; instr 64'
KICKS='setstep 0 0 60 64 -1 0 -1 0; setstep 0 4 60 64 -1 0 -1 0; setstep 0 8 60 64 -1 0 -1 0; setstep 0 12 60 64 -1 0 -1 0'
TONE_DRY='setstep 1 0 60 0 -1 0 -1 0'
TONE_WET='setstep 1 0 60 0 30 100 32 100'
DUCKSET='tap Master; wait 800; tap F4; wait 600; tap F6; wait 600; tap F5; tap F5; tap F5; tap F5; tap F5; tap F5; tap F5; tap F5; tap F5; tap F5; tap F5; tap F5; tap F1; enc 20; tap F3; enc -100; wait 300; poke duck; tap Pattern; wait 300'

render() {   # render SIM NAME BODY: one card, one capture
    local sim="$1" name="$2" body="$3"
    local sd="$WORK/sd-$name"
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" UI_SHOT_FATFS_REOPEN=1 "$sim" "$sd" "wait 500; $PROJECT; $KICKS; $body; audio 1; wav $WORK/$name.wav; tap Play; wait 6000; tap Play; wav off" > "$WORK/$name.log" 2>&1 || { echo "ui_shot failed ($name)"; tail -3 "$WORK/$name.log"; return 1; }
    cp "$WORK/poke.out" "$WORK/$name.poke.txt" 2>/dev/null
    [ -s "$WORK/$name.wav" ] || { echo "no WAV ($name)"; return 1; }
}

analyse() {   # analyse DRY WET WET2 DUCK -> "g_all g_w1 g_w2 g_w3 g_w4 repeat_residual"
    python3 -I - "$@" <<'PY'
import sys, wave, array
def load(p):
    w = wave.open(p); n = w.getnframes(); ch = w.getnchannels(); a = array.array('h'); a.frombytes(w.readframes(n))
    return ch, a
dry, wet, wet2, duck = [load(p) for p in sys.argv[1:5]]
ch = dry[0]
n = min(len(x[1]) for x in (dry, wet, wet2, duck)) // ch
start = int(0.3 * 44100)
def g(lo, hi):
    num = den = 0.0
    for i in range(lo * ch, hi * ch):
        r = wet[1][i] - dry[1][i]; d = duck[1][i] - dry[1][i]
        num += d * r; den += r * r
    return num / den if den else float('nan')
span = n - start
ws = [g(start + k * span // 4, start + (k + 1) * span // 4) for k in range(4)]
allg = g(start, n)
# determinism: the residual of two identical WET renders relative to the return
res = sum((wet[1][i] - wet2[1][i]) ** 2 for i in range(start * ch, n * ch))
ret = sum((wet[1][i] - dry[1][i]) ** 2 for i in range(start * ch, n * ch))
print("%.4f %.4f %.4f %.4f %.4f %.3e %.1f" % (allg, ws[0], ws[1], ws[2], ws[3], res / ret if ret else float('nan'), (ret / (n - start) / ch) ** 0.5))
PY
}

G_A=""; G_B=""
for sim in "$A" "$B"; do
    tag=$([ "$sim" = "$A" ] && echo a || echo b)
    render "$sim" "$tag-dry"  "$TONE_DRY"  || exit 2
    render "$sim" "$tag-wet"  "$TONE_WET"  || exit 2
    render "$sim" "$tag-wet2" "$TONE_WET"  || exit 2
    render "$sim" "$tag-duck" "$TONE_WET; $DUCKSET" || exit 2
    out=$(analyse "$WORK/$tag-dry.wav" "$WORK/$tag-wet.wav" "$WORK/$tag-wet2.wav" "$WORK/$tag-duck.wav")
    if [ "$tag" = a ]; then G_A="$out"; else G_B="$out"; fi
    echo "$tag $sim"
    echo "  g(all) g(w1..w4) repeat-residual return-rms: $out"
    echo "  $(grep '^duck ' "$WORK/$tag-duck.poke.txt" | head -1)"
done
echo "--- sample-only project (the DRY renders), banded compare, wav_band.py"
python3 -I "$HERE/wav_band.py" "$WORK/a-dry.wav" "$WORK/b-dry.wav" || DRYBAND=1
echo "--- the same compare on the WET renders (reverb and delay returns, informational, wider bands)"
python3 -I "$HERE/wav_band.py" "$WORK/a-wet.wav" "$WORK/b-wet.wav" --rms-db 1.0 --peak-db 2.0 --line-db 1.5 | tail -1
echo "--- send ducking depth"
python3 -I - "$G_A" "$G_B" "$BAND" "${DRYBAND:-0}" <<'PY'
import sys
a = [float(x) for x in sys.argv[1].split()]; b = [float(x) for x in sys.argv[2].split()]; band = float(sys.argv[3])
ga, gb = a[0], b[0]
spread = max(max(a[1:5]) - min(a[1:5]), max(b[1:5]) - min(b[1:5]))
print("depth plus %.3f  og %.3f  (1 - g)   difference %.3f  band %.3f  window spread %.3f  repeat residual plus %.1e og %.1e" % (1 - ga, 1 - gb, abs(ga - gb), band, spread, a[5], b[5]))
ok = sys.argv[4] == "0" and ga == ga and gb == gb and abs(ga - gb) <= band and (1 - ga) > 0.2 and (1 - gb) > 0.2
print("RESULT: GREEN" if ok else "RESULT: RED")
sys.exit(0 if ok else 1)
PY
