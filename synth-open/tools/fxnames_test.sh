#!/usr/bin/env bash
# Host proof that the fx name pointer tables show the very same strings as the stock char tables: core/interface/core/interfaceDefs.h
# is compiled natively with four feature sets (none, FEATURE_SYNTH, FEATURE_SYNTH + FEATURE_SYNTH_OPEN, the one of the release image, and the
# Tracker+ / Mini set PLATFORM_IMX + FEATURE_SYNTH_PATCHFX of the IMX port P12, Patch and Morph at list positions 46 and 47),
# fxnames_check.cpp prints every table entry, and the text must equal fxnames_golden.txt, made from the stock header of release-1.4.0-int
# 8d8a489 (char[FX_COUNT][FX_NAME_SIZE + 4] tables) plus the FX of features-140 (Morph, Chance FX, Tape) and rel140-snapfx (Snap), rel141-tables (Y - Table), Tape Stop and Tape Start merged into one entry W - Tape (rel140-tape-onefx), added with --golden. Usage: fxnames_test.sh [WORKTREE]      fxnames_test.sh --golden   (a stock tree only)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MODE=check
if [ "${1:-}" = "--golden" ]; then MODE=golden; ROOT="$HERE/../.."; else ROOT="${1:-$HERE/../..}"; fi
C="$ROOT/core"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
# the stock header has the same member names ptrAllFxNames, ptrAllFxNamesShort and ptrFxNames the driver reads
out="$T/out.txt"; : > "$out"
for flags in "" "-DFEATURE_SYNTH" "-DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN" "-DFEATURE_SYNTH -DPLATFORM_IMX -DFEATURE_SYNTH_PATCHFX"; do
  echo "### flags: ${flags:-none}" >> "$out"
  # shellcheck disable=SC2086
  c++ -std=gnu++20 -O0 -include cstdint -w "-Dstatic_assert(...)=" $flags -I "$C/include" -I "$C/config" -I "$C/source" -I "$C/libs/ElapsedMillis" \
    -I "$C/libs" -I "$C/libs/NewDisplay" -I "$C/interface" -I "$C/interface/core" -I "$ROOT/util/include" -I "$ROOT/util/3rdparty" -I "$ROOT/synth/include" \
    "$HERE/fxnames_check.cpp" -o "$T/check" || { echo "fxnames_test: FAIL (compile, flags: ${flags:-none})"; exit 1; }
  "$T/check" >> "$out"
done
if [ "$MODE" = golden ]; then cat "$out"; exit 0; fi
if diff -u "$HERE/fxnames_golden.txt" "$out" > "$T/diff.txt"; then
  echo "fxnames_test: PASS ($(grep -c '^[0-9t]' "$out") entries, 4 feature sets)"
else
  head -20 "$T/diff.txt"; echo "fxnames_test: FAIL"; exit 1
fi
