#!/usr/bin/env bash
# Host proof of the deflated fx description texts (part of run_tests.sh). Needs c++ with ASan/UBSan, no Arm toolchain.
#   1. the texts of the header table under the release feature set (fxdesc_dump.cpp, no FW_TRIM) are the golden
#   2. interfaceFxDescZ.cpp + inflate.cpp + the generated blob, compiled with FW_TRIM and FEATURE_SYNTH_OPEN, must return the same text
#      for every id and nullptr outside the range
#      (so a stale blob after an edit of the table fails here: run fxdesc_deflate.py)
# Usage: fxdesc_test.sh [WORKTREE]
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="${1:-$HERE/../..}"
ROOT="$(cd "$ROOT" && pwd)"
C="$ROOT/core"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir "$T/stub"
printf '#pragma once\n#define __NOINIT(x)\n' > "$T/stub/cr_section_macros.h"
INC=(-I "$T/stub" -I "$C/include" -I "$C/config" -I "$C/source" -I "$C/libs/ElapsedMillis" -I "$C/libs" -I "$C/libs/NewDisplay" -I "$C/interface" -I "$C/interface/core"
     -I "$ROOT/util/include" -I "$ROOT/util/3rdparty" -I "$ROOT/synth/include")
# 1
c++ -std=gnu++20 -O0 -include cstdint -w "-Dstatic_assert(...)=" -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN "${INC[@]}" "$HERE/fxdesc_dump.cpp" -o "$T/dump" \
  || { echo "fxdesc_test: FAIL (compile dump)"; exit 1; }
"$T/dump" "$T/golden.bin" > /dev/null || { echo "fxdesc_test: FAIL (dump)"; exit 1; }
# 2
c++ -std=gnu++20 -O1 -g -include cstdint -w "-Dstatic_assert(...)=" -DFW_TRIM -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  "${INC[@]}" "$HERE/fxdesc_check.cpp" "$C/interface/core/interfaceFxDescZ.cpp" "$C/libs/NewDisplay/inflate.cpp" -o "$T/check" \
  || { echo "fxdesc_test: FAIL (compile check)"; exit 1; }
"$T/check" "$T/golden.bin" || { echo "fxdesc_test: FAIL"; exit 1; }
echo "fxdesc_test: PASS ($(wc -c < "$T/golden.bin" | tr -d ' ') B of texts)"
