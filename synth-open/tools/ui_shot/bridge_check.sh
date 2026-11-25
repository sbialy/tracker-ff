#!/usr/bin/env bash
# Bridge proof of emu parity T7 (ref/emu-parity-plan.md): every card plugin, reference and bridge byte equal. Host tool, NOT firmware.
#   ENG=<poly-engines checkout> PY=<venv python> [UNICORN_DIR=<pinned libunicorn>] bash bridge_check.sh [--quick] [--only a,b] [OUT]
# Builds the BRIDGE=1 ui_shot and runs, in order:
#   1  make bridge-test          14 synth .plg through host_ucbridge, bit identical to the prototype renders (BRIDGE_REF)
#   2  make synth-start-test     the image's own start() against ucbs::start on every synth .plg and its corrupt / ABI variants
#   3  make bridge-fx-ref + bridge-fx-test   8 master inserts and 9 Sample Editor .ptfx, bridge against fx_ref.py (image libm)
#   4  emu_plug.py --image-libm  every insert again through the SDK harness with the image's libm (a second reference, prints checksums)
#   5  bridge_check.py           every synth .plg: poly-engines tools/emu_render.py --image-libm (reference) against the firmware's
#                                install, start and ISynthEngine in ui_shot (`plugrender`), run by run, plus the unit leg transcripts
# OUT (default ROOT/build/bridge-check) gets the logs, keep/ (transcripts and renders) and unit/ (the unit leg: <file>.tr with the
# reference's and the bridge's per block FNVs, compared on the unit by the console's plugrender). Exit 0 = all GREEN.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
quick=(); only=()
while [ $# -gt 0 ]; do
  case $1 in
    --quick) quick=(--quick); shift ;;
    --only) only=(--only "$2"); shift 2 ;;
    *) break ;;
  esac
done
OUT=${1:-$ROOT/build/bridge-check}
ENG=${ENG:-$HOME/prj/poly-engines}
PY=${PY:-$(sh "$ROOT/synth-open/tools/pyenv.sh" python)} || exit 2
TOOLCHAIN=${TOOLCHAIN:-$ROOT/agt/12.3.rel1/bin/arm-none-eabi-}
[ -x "${TOOLCHAIN}gcc" ] || TOOLCHAIN=$HOME/prj/poly/agt/12.3.rel1/bin/arm-none-eabi-
export TOOLCHAIN
. "$HERE/bridge_env.sh"   # UNICORN_DIR (the pinned wheel), BRIDGE_AXF (the release axf)
bridge_env "$ROOT" || exit 2
CARD=${CARD:-$HOME/prj/poly/build/emu/current/card}
AXF=${AXF:-$BRIDGE_AXF}
BIN_DIR=${BIN_DIR:-$ROOT/build/ui-shot-bridge}
JOBS=${JOBS:-6}
[ -f "$AXF" ] || { echo "no release axf $AXF (make original-release first)"; exit 2; }
"$PY" -c 'import unicorn, numpy, capstone' || { echo "PY=$PY needs unicorn 2.1.4, numpy and capstone"; exit 2; }
mkdir -p "$OUT"
mk=(make -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" BRIDGE=1 UNICORN_DIR="$UNICORN_DIR" OUT="$BIN_DIR" BRIDGE_AXF="$AXF" TOOLCHAIN="$TOOLCHAIN" PY="$PY")
fail=0
step() {   # $1 name, then the command: its log is OUT/<name>.log, a non zero exit marks the run RED
  local n=$1; shift
  echo "== $n"
  if "$@" > "$OUT/$n.log" 2>&1; then tail -3 "$OUT/$n.log"; else echo "$n FAILED (rc $?), log $OUT/$n.log"; tail -10 "$OUT/$n.log"; fail=1; fi
}
step build "${mk[@]}" "$BIN_DIR/ui_shot"
step bridge-test "${mk[@]}" bridge-test
step synth-start-test "${mk[@]}" synth-start-test
step bridge-fx-ref "${mk[@]}" BRIDGE_FX_CARD="$CARD" BRIDGE_FX_REF="$OUT/fx-ref" bridge-fx-ref
step bridge-fx-test "${mk[@]}" BRIDGE_FX_CARD="$CARD" BRIDGE_FX_REF="$OUT/fx-ref" bridge-fx-test
inserts() {
  local f
  for f in "$CARD"/Plugins/Master/*.plg; do
    "$PY" -I "$ROOT/synth-open/plug/emu_plug.py" "$f" --image-libm "$AXF" || return 1
  done
}
step emu-plug-inserts inserts
step bridge-check "$PY" -I "$HERE/bridge_check.py" --engines-tree "$ENG" --card "$CARD" --axf "$AXF" --ui-shot "$BIN_DIR/ui_shot" \
  --jobs "$JOBS" --keep "$OUT/keep" --unit-dir "$OUT/unit" ${quick[@]+"${quick[@]}"} ${only[@]+"${only[@]}"}
if [ $fail -eq 0 ]; then echo "RESULT: GREEN"; else echo "RESULT: RED"; fi
exit $fail
