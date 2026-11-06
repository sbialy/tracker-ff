#!/bin/sh
# Renders PLK demo WAV files on the host (see pluck_render.cpp for the list), so the engine can be listened to without
# the device. Needs clang++ (macOS: x86_64 build via Rosetta, like run_smoke.sh).
#   synth-open/tools/pluck_render.sh [outdir]      default build/pluck-wav
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$ROOT/build/pluck-wav}
mkdir -p "$OUT"
BIN=${TMPDIR:-/tmp}/pluck_render_$$
clang++ -arch x86_64 -std=c++20 -O2 -ffp-contract=off "-D_GLIBCXX_THROW_OR_ABORT(x)=__builtin_abort()" \
  -I "$ROOT/synth/include" -I "$ROOT/util/include" -I "$ROOT/util/3rdparty" -I "$ROOT/synth-open" -I "$ROOT/synth-open/ot" \
  -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DSYNTH_ENGINE_PLK \
  "$ROOT/synth-open/tools/pluck_render.cpp" "$ROOT/synth-open/pluck/pluck.cpp" "$ROOT/synth-open/engine_pluck.cpp" -o "$BIN"
rc=0
"$BIN" "$OUT" || rc=$?
rm -f "$BIN"
echo "WAV files in $OUT"
exit $rc
