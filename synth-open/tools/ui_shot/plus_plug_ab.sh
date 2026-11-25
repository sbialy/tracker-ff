#!/bin/bash
# plus_plug_ab.sh [SIM_A [SIM_B]]   (ref/ff-imx-port-plan.md P7 H5)  SIM FIGURES ONLY, no device
# One plugin synth engine, the same .plg, the same default patch and the same note in two sims: SIM_A the plus sim (this tree's
# build/ui-shot-plus-console-bridge/ui_shot: the stock synth stack, the closed libPolySynth plays the engine through PlugSynthVoice in its
# guest), SIM_B the OG sim (default the pinned release emu ~/prj/poly/build/emu/current/ui_shot: the MK66 open shim plays it from the bank).
# The engine code is the same bytes in both. What differs is the host around it: the lib's voice allocator and envelope of the gain, the
# IMX rate 44,100 Hz vs 44,117.6 Hz, the velocity rule (PlugVoice: Volume x velocity, the MK66 shim: the patch's velocity amount), the
# part mixer chains (13 vs 21 inputs) and the master. So the WAVs are NOT bit identical: this compares the level and the envelope.
#   level  = 20 log10(rms_A / rms_B) over the capture (both channels)
#   env    = Pearson correlation of the 10 ms RMS envelopes, aligned at the onset (first sample above 1 % of the peak), first 1.5 s
# Band (stated before the first run): |level| <= LEVEL_DB (3 dB) and env >= ENV_MIN (0.90). ENGINES (default "PLK:pluck ALCO:alco"):
# NAME:FILE pairs, the .plg files come from PLUG_CARD. HARNESS: the plus_synth guest of SIM_A (default this tree's build/plus-synth-ab,
# built by synth-open/tools/plus_synth/build.sh when missing). KEEP=<dir> keeps the WAVs. Exit 0 when every engine is inside the band.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
A="${1:-$ROOT/build/ui-shot-plus-console-bridge/ui_shot}"
B="${2:-$HOME/prj/poly/build/emu/current/ui_shot}"
PLUG_CARD="${PLUG_CARD:-$HOME/prj/poly/build/emu/current/card/Plugins/Synth}"
ENGINES="${ENGINES:-PLK:pluck ALCO:alco}"
LEVEL_DB="${LEVEL_DB:-3}"
ENV_MIN="${ENV_MIN:-0.90}"
PY="${PY:-$HOME/prj/poly/build/pyenv/bin/python}"
HARNESS="${HARNESS:-$ROOT/build/plus-synth-ab/harness.elf}"
if [ ! -s "$HARNESS" ]; then
    bash "$ROOT/synth-open/tools/plus_synth/build.sh" "$(dirname "$HARNESS")" >/dev/null 2>&1 && [ -s "$HARNESS" ] || { echo "plus_plug_ab: no harness $HARNESS" >&2; exit 2; }
fi
WORK="$(mktemp -d "${TMPDIR:-/tmp}/plus_plug_ab.XXXXXX")"
trap '[ -n "${KEEP:-}" ] && { mkdir -p "$KEEP"; cp "$WORK"/*.wav "$WORK"/*.log "$KEEP"/ 2>/dev/null; }; rm -rf "$WORK"' EXIT
for b in "$A" "$B"; do [ -x "$b" ] || { echo "plus_plug_ab: no $b" >&2; exit 2; }; done

render() {   # render SIM TAG NAME FILE (tag a: the plus sim with its parts on the closed lib in the guest)
    local sd="$WORK/sd-$2-$3" env=()
    [ "$2" = a ] && env=(UISHOT_PLUS_SYNTH="$HARNESS")
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Synth"
    cp "$PLUG_CARD/$4.plg" "$sd/Plugins/Synth/" || return 1
    (cd "$WORK" && env ${env[@]+"${env[@]}"} ASAN_OPTIONS=detect_leaks=0 timeout 300 "$1" "$sd" "wait 500; instr 64; tap Params; wait 1500; pickmodel $3; wait 3000; tap Pattern; wait 300; setstep 0 0 60 64 0 0 0 0; audio 1; wav $WORK/$2-$3.wav; tap Play; wait 1500; tap Play; wait 1000; wav off") \
        >"$WORK/$2-$3.log" 2>&1
    grep -q "^pickmodel $3 ok" "$WORK/$2-$3.log" && [ -s "$WORK/$2-$3.wav" ]
}

fails=0
for spec in $ENGINES; do
    name="${spec%%:*}"; file="${spec#*:}"
    if ! render "$A" a "$name" "$file" || ! render "$B" b "$name" "$file"; then
        echo "FAIL  $name: a render failed ($(grep -h '^pickmodel' "$WORK"/?-"$name".log | tr '\n' ' '))"; fails=$((fails+1)); continue
    fi
    out=$("$PY" -I - "$WORK/a-$name.wav" "$WORK/b-$name.wav" "$LEVEL_DB" "$ENV_MIN" <<'PY'
import sys, wave
import numpy as np
def load(p):
    w = wave.open(p); a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64) / 32768.0
    return a.reshape(-1, w.getnchannels()), w.getframerate()
(a, ra), (b, rb) = load(sys.argv[1]), load(sys.argv[2])
lim, emin = float(sys.argv[3]), float(sys.argv[4])
def onset(x):
    m = np.abs(x).max(axis=1); pk = m.max()
    return int(np.argmax(m > 0.01 * pk)) if pk > 0 else 0
def env(x, r, o):
    h = int(0.010 * r); n = int(1.5 * r) // h
    seg = x[o:o + n * h]
    n = len(seg) // h
    return np.sqrt((seg[:n * h].reshape(n, h, -1) ** 2).mean(axis=(1, 2)))
pa, pb = np.abs(a).max(), np.abs(b).max()
rms = lambda x: np.sqrt((x ** 2).mean())
level = 20 * np.log10(rms(a) / rms(b)) if rms(a) > 0 and rms(b) > 0 else float('nan')
ea, eb = env(a, ra, onset(a)), env(b, rb, onset(b))
n = min(len(ea), len(eb))
c = float(np.corrcoef(ea[:n], eb[:n])[0, 1]) if n > 2 else float('nan')
ok = abs(level) <= lim and c >= emin
print('%s peak_a %.4f peak_b %.4f rms_a %.5f rms_b %.5f level %+.2f dB env %.3f (%d windows) band |level|<=%.1f env>=%.2f' %
      ('ok' if ok else 'OUT', pa, pb, rms(a), rms(b), level, c, n, lim, emin))
PY
)
    case "$out" in ok*) echo "ok    $name ${out#ok }";; *) echo "FAIL  $name ${out#OUT }"; fails=$((fails+1));; esac
done
[ "$fails" = 0 ] && { echo "plus_plug_ab: inside the band"; exit 0; }
echo "plus_plug_ab: $fails outside the band or failed"; exit 1
