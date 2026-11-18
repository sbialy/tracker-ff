#!/usr/bin/env bash
# Host proof that the deflated custom play orders (core/source/playModeTabs.cpp) inflate to the very same table as the stock
# source table of playModeDefs.h: the firmware's own playModeOrder.cpp and inflate.cpp natively, sha256 of the 5,120 bytes
# (pairs stepNumberToPlay, fxOption of the 20 orders of 128) against playorder_golden.txt, made from the stock header of
# release-1.4.0-int d5420afd. Also checks that names/playorder.txt still makes the committed playModeTabs.cpp.
# Usage: playorder_test.sh [WORKTREE]
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="${1:-$HERE/../..}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
C="$ROOT/core"
cat > "$T/check.cpp" <<'EOS'
#include <stdio.h>
#include "playModeDefs.h"
#include "NewDisplay/inflate.h"
InflateScratch gInflateScratch;
int main()
{
    if (!playOrderInit()) { puts("inflate failed"); return 1; }
    fwrite(playOrderWithFx, sizeof(playOrderWithFx), 1, stdout);
    return 0;
}
EOS
c++ -std=gnu++20 -O1 -include cstdint -w -DCR_SECTION_MACROS_H_INCLUDED "-D__NOINIT(x)=" -DEXTERNAL_RAM=0 \
  -I "$C/source" -I "$C/libs" -I "$C/include" "$T/check.cpp" "$C/source/playModeOrder.cpp" "$C/source/playModeTabs.cpp" \
  "$C/libs/NewDisplay/inflate.cpp" -o "$T/check" || { echo "playorder_test: FAIL (compile)"; exit 1; }
"$T/check" > "$T/table.bin" || { echo "playorder_test: FAIL (run)"; exit 1; }
sum="$(shasum -a 256 "$T/table.bin" | cut -d' ' -f1)"
want="$(grep -v '^#' "$HERE/playorder_golden.txt" | head -1)"
[ "$want" = "5120 $sum" ] || { echo "playorder_test: FAIL (got 5120 $sum, golden $want)"; exit 1; }
cp "$C/source/playModeTabs.cpp" "$C/source/playModeTabs.h" "$T/"
python3 -I "$HERE/playorder_deflate.py" "$C/source" > /dev/null || { echo "playorder_test: FAIL (generator)"; exit 1; }
if ! cmp -s "$C/source/playModeTabs.cpp" "$T/playModeTabs.cpp" || ! cmp -s "$C/source/playModeTabs.h" "$T/playModeTabs.h"; then
  cp "$T/playModeTabs.cpp" "$T/playModeTabs.h" "$C/source/"; echo "playorder_test: FAIL (playModeTabs.* is not what playorder.txt makes)"; exit 1
fi
echo "playorder_test: PASS (5,120 B, sha256 ${sum:0:12})"
