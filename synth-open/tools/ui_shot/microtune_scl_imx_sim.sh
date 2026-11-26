#!/bin/bash
# microtune_scl_imx_sim.sh PLUS|MINI [EVIDENCE_DIR]
#
# Microtuning phase 2 (Scala .scl import) on the Tracker+ and the Mini simulator: the same scenario file as the original Tracker (microtune_scl_sim.sh: the five
# example files through the Project Settings picker, every reject with its popup text, the pitch of sample tracks per key against the ratios on the voice tap,
# a non octave scale, project.mtff with the embedded table, reload with the .scl removed, Save As and Open on a card without /Tunings, a held looping voice)
# on a ui_shot built with PLATFORM=IMX PRODUCT=PLUS or MINI (build/ui-shot-plus, build/ui-shot-mini, made here when missing: only the host simulator, never the
# firmware targets). The IMX build has sample tracks only (no open synth stack in the plus and mini pins), so the synth and CHIP sims do not apply there.
# The Mini has no encoder in its lists: LISTKEYS=tap moves a list by Down and Up.
# SCRIPT=tuning_label_sim.sh runs that scenario file on the same builds (the Project Settings value column).
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
P="${1:?usage: microtune_scl_imx_sim.sh PLUS|MINI [EVIDENCE_DIR]}"
case "$P" in
    PLUS) OUT="$ROOT/build/ui-shot-plus"; LK=enc ;;
    MINI) OUT="$ROOT/build/ui-shot-mini"; LK=tap ;;
    *) echo "microtune_scl_imx_sim.sh: PLUS or MINI" >&2; exit 99 ;;
esac
BIN="${UI_SHOT_BIN:-$OUT/ui_shot}"
if [ -z "${UI_SHOT_BIN:-}" ]; then
    make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" PLATFORM=IMX PRODUCT="$P" >"$OUT.make.log" 2>&1 || { tail -30 "$OUT.make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "microtune_scl_imx_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
LISTKEYS="$LK" BIN="$BIN" PLAT="$P" exec bash "$HERE/${SCRIPT:-microtune_scl_sim.sh}" "${2:-}"
