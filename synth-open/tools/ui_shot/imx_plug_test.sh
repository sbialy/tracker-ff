#!/usr/bin/env bash
# IMX port P5 H8 as a gate leg (run_tests.sh): make PLATFORM=IMX BRIDGE=1 imx-plug-test (host_imxplug_test.cc). Every synth .plg of the card is
# loaded by plug::imx::load into an IMX SDRAM slot that Unicorn maps at 0x80000000 and must render byte equal to the MK66 placement.
# Host only, NOT firmware.
#   imx_plug_test.sh [LOGDIR]
# Env: CARD (default ~/prj/poly/build/emu/current/card), BRIDGE_REF (default the makefile's), UNICORN_DIR BRIDGE_AXF (bridge_env.sh).
# Known over the 32 KiB synth slot: IMX_TOOBIG_OK (default "alco"). ALCO is image 31,940 + bss 1,276 = 33,216 B: on the MK66 its rw goes to
# s_pool beside the FlexNVM text, on IMX plug_load.h puts image + bss in one slot. The slot size (36/40 KiB) or the MK66 rw split is P7.
# Any other engine reported TOO BIG fails the leg, so a plugin that grows past the slot shows up here.
# Exit 0 green, 1 red, 0 with SKIP when there is no card.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
LOG=${1:-$ROOT/build/tests/logs}
mkdir -p "$LOG"
card=${CARD:-$HOME/prj/poly/build/emu/current/card}
ok_list=" ${IMX_TOOBIG_OK:-alco} "
if ! ls "$card"/Plugins/Synth/*.plg >/dev/null 2>&1; then
    echo "imx_plug_test: SKIP (no card with Plugins/Synth/*.plg at $card, set CARD)"; exit 0
fi
. "$HERE/bridge_env.sh"
bridge_env "$ROOT" || exit 1
make -s -C "$HERE" PLATFORM=IMX "${BRIDGE_ARGS[@]}" BRIDGE_CARD="$card/Plugins/Synth" ${BRIDGE_REF:+BRIDGE_REF="$BRIDGE_REF"} \
    -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" imx-plug-test > "$LOG/imx_plug_test.log" 2>&1
rc=$?
grep -E 'TOO BIG|FAIL|DIFFERS|of [0-9]+ synth \.plg' "$LOG/imx_plug_test.log"
bad=0
while read -r name; do
    case "$ok_list" in *" $name "*) ;; *) echo "imx_plug_test: $name is over the IMX synth slot and not in IMX_TOOBIG_OK"; bad=1 ;; esac
done < <(awk '/TOO BIG for the IMX synth slot/ {print $1}' "$LOG/imx_plug_test.log")
[ "$rc" = 0 ] || { echo "imx_plug_test: make imx-plug-test rc=$rc (log $LOG/imx_plug_test.log)"; tail -5 "$LOG/imx_plug_test.log"; exit 1; }
[ "$bad" = 0 ] || exit 1
echo "imx_plug_test: PASS"
