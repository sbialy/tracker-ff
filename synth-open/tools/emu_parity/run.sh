#!/bin/bash
# emu-parity measurements (tools only, never in the image): builds the libunicorn bridge prototype and runs every bench of
# ref/emu-parity-plan.md against the release card of the pinned emu.
#   TOOLCHAIN=.../arm-none-eabi- [CARD=~/prj/poly/build/emu/current/card] [ENGINES_DIR=../poly-engines]
#   [AXF=platforms/mk66/build-trim/PolyendTracker.axf] synth-open/tools/emu_parity/run.sh OUTDIR
# PY defaults to the pinned build/pyenv (unicorn 2.1.4: its wheel ships include/ and lib/libunicorn.a, the prototype links that), capstone and numpy.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="$(mkdir -p "${1:?OUTDIR}" && cd "$1" && pwd)"
: "${TOOLCHAIN:?}"
PY="${PY:-$(sh "$ROOT/synth-open/tools/pyenv.sh" python)}" || exit 2
CARD="${CARD:-$HOME/prj/poly/build/emu/current/card}"
export ENGINES_DIR="${ENGINES_DIR:-$ROOT/../poly-engines}"
UC="$("$PY" -c 'import os, unicorn; print(os.path.dirname(unicorn.__file__))')"
clang++ -O2 -std=c++17 -I"$UC/include" "$HERE/ucbridge_proto.cpp" "$UC/lib/libunicorn.a" -lz -lpthread -o "$OUT/ucbridge_proto"
( cd "$ENGINES_DIR" && "$PY" -c "import sys; sys.path.insert(0, 'tools'); import emu_render as R; R.build_harness('$TOOLCHAIN', '$OUT/render_harness.plg')" )
cd "$OUT"
"$PY" "$HERE/bench_synth.py" "$CARD/Plugins/Synth" render_harness.plg synth_bench.json | tee synth_bench.txt
SET=max BLOCKS=60 "$PY" "$HERE/bench_synth.py" "$CARD/Plugins/Synth" render_harness.plg synth_bench_max.json | tee synth_bench_max.txt
"$PY" "$HERE/bench_insert.py" "$CARD/Plugins/Master" insert_bench.json | tee insert_bench.txt
for p in "$CARD"/Plugins/Synth/*.plg; do e=$(basename "$p" .plg)
  ./ucbridge_proto "$p" render_harness.plg "uc_$e.f32" 100 4 | head -1
  "$PY" "$HERE/ref_out.py" "$p" render_harness.plg "py_$e.f32" 100 4
  cmp -s "uc_$e.f32" "py_$e.f32" && echo "  $e BIT IDENTICAL (bridge at 0x10000000 vs emu_render.py at 0x1FFF6000)" || echo "  $e DIFFERS"
done | tee bridge_vs_python.txt
python3 "$HERE/worst.py" | tee worst.txt
AXF="${AXF:-$ROOT/platforms/mk66/build-trim/PolyendTracker.axf}"
[ -f "$AXF" ] && "$PY" "$HERE/libm_check.py" "$AXF" 20000 | tee libm_check.txt || echo "no $AXF: make original-release first for the libm check"
