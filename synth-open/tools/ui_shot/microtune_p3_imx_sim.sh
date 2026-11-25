#!/bin/bash
# microtune_p3_imx_sim.sh PLUS|MINI SIM [EVIDENCE_DIR]
#
# A microtuning phase 3 sim (microtune_names_sim.sh, microtune_snap_sim.sh, ...) on the Tracker+ and the Mini simulator: the same scenario file on a ui_shot built with
# PLATFORM=IMX PRODUCT=PLUS or MINI (build/ui-shot-plus, build/ui-shot-mini, made here when missing: only the host simulator, never the firmware targets).
# The Mini has no encoder in its lists: LISTKEYS=tap moves a list by Down and Up. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
P="${1:?usage: microtune_p3_imx_sim.sh PLUS|MINI SIM [EVIDENCE_DIR]}"
S="${2:?usage: microtune_p3_imx_sim.sh PLUS|MINI SIM [EVIDENCE_DIR]}"
case "$P" in
    PLUS) OUT="$ROOT/build/ui-shot-plus"; LK=enc ;;
    MINI) OUT="$ROOT/build/ui-shot-mini"; LK=tap ;;
    *) echo "microtune_p3_imx_sim.sh: PLUS or MINI" >&2; exit 99 ;;
esac
BIN="${UI_SHOT_BIN:-$OUT/ui_shot}"
if [ -z "${UI_SHOT_BIN:-}" ]; then
    make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" PLATFORM=IMX PRODUCT="$P" >"$OUT.make.log" 2>&1 || { tail -30 "$OUT.make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "microtune_p3_imx_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
LISTKEYS="$LK" BIN="$BIN" PLAT="$P" exec bash "$HERE/$S" "${3:-}"
