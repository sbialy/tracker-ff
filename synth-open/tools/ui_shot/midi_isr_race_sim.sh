#!/usr/bin/env bash
# MIDI input and the audio interrupt (core/source/mtMidi.cpp, arch move 5 step 1). The firmware reads the MIDI queues in the audio DMA
# interrupt (TrackerImpl::updateAudioBuffers -> midiUpdate), so the note handlers run there. A note on in the sequencer input mode used to
# redraw the pattern view from inside the interrupt, which can cut into a redraw the main loop is in the middle of. Now it sets a flag and
# the main loop redraws (midiUiUpdate).
# The row: the simulator's preempt point at the start of cPatternEditor::refreshPattern (core/include/ff_host_seam.h) fires the "interrupt"
# (a DIN note on and off through the real handlers) while the main loop redraws. A redraw started inside the interrupt is counted: none wanted.
# Usage: midi_isr_race_sim.sh [UI_SHOT_BINARY]   (default build/ui-shot/ui_shot: make -C synth-open/tools/ui_shot)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BIN="${1:-$ROOT/build/ui-shot/ui_shot}"
[ -x "$BIN" ] || { echo "no $BIN (make -C synth-open/tools/ui_shot)"; echo "RESULT: RED (no binary)"; exit 1; }
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
fails=0
expect() {   # expect TEXT WANT GOT
    if [ "$2" = "$3" ]; then echo "ok    $1"; else echo "FAIL  $1"; echo "      expected: $2"; echo "      got:      $3"; fails=$((fails + 1)); fi
}
out="$(ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd" "wait 500; midirace 3" 2>/dev/null | grep -a '^midirace' | tail -1)"
expect "three MIDI interrupts fired in the middle of a main loop redraw of the pattern view, none of them redrew it again (no UI work in the interrupt)" \
    "midirace fired=3 reentered=0" "$out"
if [ "$fails" = 0 ]; then echo "RESULT: GREEN"; else echo "RESULT: RED ($fails failing)"; exit 1; fi
