#!/bin/bash
# plus_fx4_sim.sh [plus|mini]   (ref/ff-imx-port-plan.md P4 leftover b)
# The FX3/FX4 flip on the Tracker+ and Mini sims with a state readout, not a screen text: flows/plus_only_4fx.flow only sees the selected row of the page, so
# it cannot tell 2 slots from 4. `poke fxslots` prints the slots a step has (fx34::g_on, 4 with the project switch, else 2).
#   (the list moves by tap Down and tap Up on both, the Mini has no encoder)
#   a new project has 2 slots, Project Settings > 4 FX Slots > On gives 4, it holds over a Pattern screen round trip, Off gives 2 again
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
M="${1:-plus}"
BIN="${UI_SHOT_BIN:-$ROOT/build/ui-shot-$M-console-bridge/ui_shot}"
[ -x "$BIN" ] || { echo "plus_fx4_sim: no $BIN (build it with plus_smoke.sh or mini_smoke.sh)" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/plus_fx4_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
fails=0
expect() { if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
mkdir -p "$WORK/sd"; rm -f "$WORK/poke" "$WORK/poke.out"
FLIP='tap File; wait 800; tap F7; wait 1000; tap F8; wait 600; tap Down; wait 600; tap F8; wait 3000'
UISHOT_POKE="$WORK/poke" "$BIN" "$WORK/sd" "wait 500; tap Pattern; wait 300; poke fxslots; $FLIP; poke fxslots; tap F7; wait 800; tap Pattern; wait 800; poke fxslots; tap File; wait 800; tap F7; wait 1000; tap F8; wait 600; tap Up; wait 600; tap F8; wait 3000; poke fxslots" >/dev/null 2>"$WORK/err" || { tail -3 "$WORK/err"; fails=$((fails+1)); }
got=$(grep '^fxslots ' "$WORK/poke.out" 2>/dev/null | awk '{printf "%s ", $2}')
expect "slots before, after On, after the Pattern round trip, after Off" "2 4 4 2 " "$got"
echo
[ "$fails" = 0 ] && { echo "RESULT: GREEN"; exit 0; }
echo "RESULT: RED ($fails)"; exit 1
