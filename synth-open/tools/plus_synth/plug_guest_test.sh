#!/bin/bash
# plug_guest_test.sh [OUT]   (ref/ff-imx-port-plan.md P7 H5) host tooling, a run_tests.sh leg
# Every synth .plg of the card (CARD, default the pinned OG emu card) except chip.plg (D10: the built-in Chiptune stays on IMX) is loaded into
# a code slot of the plus_synth harness guest (h2_plug_attach: plug_load.h loadImage, then PlugSynthVoice attach and the lib's registry) and
# plays one note on the closed libPolySynth through PlugVoice (run.py --plg, 2 voices, 0.5 s on, 0.4 s off). Green: every engine attaches
# under its manifest type and renders peak > 0 with no NaN. SKIP (exit 0) without a card. TOOLCHAIN and PY as for build.sh and run.py.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$HERE/../../../build/plug-guest-test}"
CARD="${CARD:-$HOME/prj/poly/build/emu/current/card}"
PY="${PY:-$HOME/prj/poly/build/pyenv/bin/python}"
if ! ls "$CARD"/Plugins/Synth/*.plg >/dev/null 2>&1; then
    echo "plug_guest_test: SKIP (no card with Plugins/Synth/*.plg at $CARD, set CARD)"; exit 0
fi
mkdir -p "$OUT"
bash "$HERE/build.sh" "$OUT/harness" > "$OUT/build.log" 2>&1 || { tail -20 "$OUT/build.log"; echo "plug_guest_test: harness build failed"; exit 1; }
n=0; bad=0
for f in "$CARD"/Plugins/Synth/*.plg; do
    e=$(basename "$f" .plg)
    [ "$e" = chip ] && continue
    n=$((n+1))
    line=$("$PY" -I "$HERE/run.py" "$OUT/harness/harness.elf" "$OUT/harness/harness.nm" x "$OUT/$e.wav" --plg "$f" --voices 2 2>&1 | tail -1)
    case "$line" in
        *" nan 0 "*) pk=$(sed -E 's/.* peak ([0-9.]+) .*/\1/' <<<"$line")
            if awk -v p="$pk" 'BEGIN { exit !(p > 0) }'; then echo "ok    $e: $line"; else echo "FAIL  $e silent: $line"; bad=$((bad+1)); fi;;
        *) echo "FAIL  $e: $line"; bad=$((bad+1));;
    esac
done
[ "$bad" = 0 ] && { echo "plug_guest_test: $n of $n synth .plg play in the guest"; exit 0; }
echo "plug_guest_test: $bad of $n failed"; exit 1
