#!/bin/bash
# macro_mode_compat_check.sh - the macro depth mode (patch field 100) through the patch codec of this tree (1.3.0) and of the old
# firmware (FW12_REF, default release-1.2.0-pub6): bytes written, what each codec reads back and what it plays, for
#   old   a 1.2.x patch (Add, macro amount 0.5), old0 the same without a macro amount
#   new0  a new 1.3.0 patch (Scale, no macro amount)
#   newN  a 1.3.0 patch (Scale, macro amount 0.5)
# Exit 0 when every expectation holds, 1 otherwise.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REF="${FW12_REF:-release-1.2.0-pub6}"
W="$(mktemp -d "${TMPDIR:-/tmp}/macro_mode_compat.XXXXXX")"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/t12" && git -C "$ROOT" archive "$REF" synth-open | tar -x -C "$W/t12" || { echo "no $REF" >&2; exit 2; }
T=$ROOT/synth-open/tools/macro_mode_compat_test.cpp
build() { c++ -std=c++20 -O1 $3 -I "$1/synth-open" -I "$1/synth-open/ot" -I "$ROOT/synth/include" -I "$ROOT/core/synth" -I "$ROOT/util/include" "$T" "$1/synth-open/open_patchio.cpp" -o "$2" 2>"$W/build.log" || { cat "$W/build.log" >&2; exit 2; }; }
build "$ROOT" "$W/c13" -DTREE13
build "$W/t12" "$W/c12" ""
fail=0
expect() { if [ "$2" = "$3" ]; then echo "PASS  $1"; else echo "FAIL  $1"; echo "      expected: $2"; echo "      got:      $3"; fail=$((fail + 1)); fi; }
"$W/c12" enc old "$W/old12.patch"; "$W/c12" enc old0 "$W/old012.patch"
"$W/c13" enc old "$W/old13.patch"; "$W/c13" enc new0 "$W/new0.patch"; "$W/c13" enc newN "$W/newN.patch"
sz() { wc -c <"$1" | tr -d ' '; }
echo "bytes: 1.2 old $(sz "$W/old12.patch"), 1.3 old $(sz "$W/old13.patch"), new0 $(sz "$W/new0.patch"), newN $(sz "$W/newN.patch")"
expect "old 1.2 patch: the 1.3.0 codec writes the same bytes as the 1.2 codec" "$(shasum "$W/old12.patch" | cut -c1-16)" "$(shasum "$W/old13.patch" | cut -c1-16)"
expect "old 1.2 patch without a macro amount, read by 1.3.0: Scale (no amount tells the modes apart), re-saved unchanged" "bytes $(sz "$W/old012.patch") amount 0 unknown-raw 8 plays Scale resave same" "$("$W/c13" dec "$W/old012.patch")"
expect "old 1.2 patch, read by 1.3.0: Add, amount kept, re-saved unchanged" "bytes $(sz "$W/old12.patch") amount 0.5 unknown-raw 8 plays Add resave same" "$("$W/c13" dec "$W/old12.patch")"
expect "old 1.2 patch, read by 1.2: Add" "bytes $(sz "$W/old12.patch") amount 0.5 unknown-raw 8 plays Add resave same" "$("$W/c12" dec "$W/old12.patch")"
expect "new patch with no macro amount: the same bytes as the 1.2 codec writes (no field 100)" "$(shasum "$W/old012.patch" | cut -c1-16)" "$(shasum "$W/new0.patch" | cut -c1-16)"
"$W/c12" enc old "$W/zero12.patch" && sed 's/^/ /' /dev/null
expect "new patch with no macro amount, read by 1.3.0: Scale (the default), re-saved unchanged" "bytes $(sz "$W/new0.patch") amount 0 unknown-raw 8 plays Scale resave same" "$("$W/c13" dec "$W/new0.patch")"
expect "new patch with no macro amount, read by 1.2: no field 100 for it to miss, so the sound is the same" "bytes $(sz "$W/new0.patch") amount 0 unknown-raw 8 plays Add resave same" "$("$W/c12" dec "$W/new0.patch")"
expect "Scale patch with a macro amount: 1.3.0 writes field 100 (a0 06 01 at the end), 3 bytes more" "$(( $(sz "$W/old12.patch") + 3 )) a00601" "$(sz "$W/newN.patch") $(tail -c 3 "$W/newN.patch" | xxd -p)"
expect "Scale patch with a macro amount, read by 1.3.0: Scale, re-saved unchanged" "bytes $(sz "$W/newN.patch") amount 0.5 unknown-raw 8 plays Scale resave same" "$("$W/c13" dec "$W/newN.patch")"
expect "Scale patch with a macro amount, read by 1.2: plays Add (the limit), field 100 kept as 3 unknown bytes, re-saved unchanged" "bytes $(sz "$W/newN.patch") amount 0.5 unknown-raw 11 plays Add resave same" "$("$W/c12" dec "$W/newN.patch")"
[ "$fail" = 0 ] && { echo "RESULT: GREEN"; exit 0; } || { echo "RESULT: RED ($fail failing)"; exit 1; }
