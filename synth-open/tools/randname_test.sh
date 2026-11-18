#!/usr/bin/env bash
# Host proof that the deflated random name tables give the very same names as the stock tables: the firmware's own
# mtRandomNameGenerator.cpp (inflater and deflated tables included) names 6,000 fake timer seeds, the sha256 of the list must be
# the one of randname_golden.txt, made with the stock source of release-1.4.0-int 8d8a489 (plain C string tables). Also runs the
# generator output check: tables decode to the committed word lists. Usage: randname_test.sh [WORKTREE]
#   randname_test.sh --golden N     print the golden line for the tree (to remake the file from a stock tree)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="${1:-$HERE/../..}"
[ "${1:-}" = "--golden" ] && { ROOT="$HERE/../.."; MODE=golden; } || MODE=check
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
C="$ROOT/core"
sed 's/AUDIO_CORE_RAM//' "$C/source/util_math.cpp" > "$T/util_math.cpp"
cat > "$T/shared.cpp" <<'EOS'
#include "NewDisplay/inflate.h"
InflateScratch gInflateScratch;
EOS
SRCS=("$HERE/randname_check.cpp" "$C/source/mtRandomNameGenerator.cpp" "$T/util_math.cpp")
if [ -f "$C/source/mtRandomNameTabs.cpp" ]; then SRCS+=("$C/source/mtRandomNameTabs.cpp" "$C/libs/NewDisplay/inflate.cpp" "$T/shared.cpp"); fi
c++ -std=gnu++20 -O1 -include cstdint -w "-Dstatic_assert(...)=" -DCR_SECTION_MACROS_H_INCLUDED "-D__NOINIT(x)=" -DEXTERNAL_RAM=0 \
  -I "$C/include" -I "$C/config" -I "$C/source" -I "$C/libs/ElapsedMillis" -I "$C/libs" -I "$ROOT/util/include" -I "$ROOT/util/3rdparty" \
  "${SRCS[@]}" -o "$T/check" || { echo "randname_test: FAIL (compile)"; exit 1; }
"$T/check" 6000 > "$T/names.txt"
sum="$(shasum -a 256 "$T/names.txt" | cut -d' ' -f1)"
if [ "$MODE" = golden ]; then echo "6000 $sum"; exit 0; fi
want="$(grep -v '^#' "$HERE/randname_golden.txt" | head -1)"
if [ "$want" = "6000 $sum" ]; then echo "randname_test: PASS (6000 names, sha256 ${sum:0:12})"; else echo "randname_test: FAIL (got 6000 $sum, golden $want)"; exit 1; fi
