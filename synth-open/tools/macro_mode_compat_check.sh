#!/bin/bash
# macro_mode_compat_check.sh - the macro depth mode (patch field 100) through the patch codec of this tree (1.3.0) and of the old
# firmware (FW12_REF, default release-1.2.0-pub6): bytes written, what each codec reads back and what it plays, for
#   old   a 1.2.x patch (Add, macro amount 0.5), old0 the same without a macro amount
#   new0  a 1.3.0 patch (Scale, no macro amount)
#   newN  a 1.3.0 patch (Scale, macro amount 0.5)
# The shipped contract: a 1.2 patch loads identically on 1.3 (no field 100 = Add, same bytes written back), and a 1.3 patch
# round-trips through the 1.2 codec byte for byte (field 100 is kept as 3 unknown bytes). A Scale patch with a macro amount
# plays as Add on 1.2.x: F1, a known accepted finding (reported as KNOWN, not as a failure).
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
"$W/c13" enc old "$W/old13.patch"; "$W/c13" enc old0 "$W/old013.patch"; "$W/c13" enc new0 "$W/new0.patch"; "$W/c13" enc newN "$W/newN.patch"
sz() { wc -c <"$1" | tr -d ' '; }
echo "bytes: 1.2 old $(sz "$W/old12.patch"), 1.3 old $(sz "$W/old13.patch"), new0 $(sz "$W/new0.patch"), newN $(sz "$W/newN.patch")"
# 1.2 patches load identically on 1.3
expect "old 1.2 patch: the 1.3.0 codec writes the same bytes as the 1.2 codec" "$(shasum "$W/old12.patch" | cut -c1-16)" "$(shasum "$W/old13.patch" | cut -c1-16)"
expect "old 1.2 patch without a macro amount: the same bytes on both codecs" "$(shasum "$W/old012.patch" | cut -c1-16)" "$(shasum "$W/old013.patch" | cut -c1-16)"
expect "old 1.2 patch, read by 1.3.0: Add, amount kept, re-saved unchanged" "bytes $(sz "$W/old12.patch") amount 0.5 unknown-raw 8 plays Add resave same" "$("$W/c13" dec "$W/old12.patch")"
expect "old 1.2 patch, read by 1.2: Add" "bytes $(sz "$W/old12.patch") amount 0.5 unknown-raw 8 plays Add resave same" "$("$W/c12" dec "$W/old12.patch")"
expect "old 1.2 patch without a macro amount, read by 1.3.0: Add (no field 100, as before), re-saved unchanged" "bytes $(sz "$W/old012.patch") amount 0 unknown-raw 8 plays Add resave same" "$("$W/c13" dec "$W/old012.patch")"
# 1.3 patches round-trip through the 1.2 codec byte for byte
expect "new patch with no macro amount: 1.3.0 writes field 100 (a0 06 01 at the end), 3 bytes more than the 1.2 file" "$(( $(sz "$W/old012.patch") + 3 )) a00601" "$(sz "$W/new0.patch") $(tail -c 3 "$W/new0.patch" | xxd -p)"
expect "new patch with no macro amount, read by 1.3.0: Scale, re-saved unchanged" "bytes $(sz "$W/new0.patch") amount 0 unknown-raw 8 plays Scale resave same" "$("$W/c13" dec "$W/new0.patch")"
expect "new patch with no macro amount, read by 1.2: field 100 kept as 3 unknown bytes, re-saved byte for byte (no amount, so the sound is the same)" "bytes $(sz "$W/new0.patch") amount 0 unknown-raw 11 plays Add resave same" "$("$W/c12" dec "$W/new0.patch")"
expect "Scale patch with a macro amount: 1.3.0 writes field 100 (a0 06 01 at the end), 3 bytes more" "$(( $(sz "$W/old12.patch") + 3 )) a00601" "$(sz "$W/newN.patch") $(tail -c 3 "$W/newN.patch" | xxd -p)"
expect "Scale patch with a macro amount, read by 1.3.0: Scale, re-saved unchanged" "bytes $(sz "$W/newN.patch") amount 0.5 unknown-raw 8 plays Scale resave same" "$("$W/c13" dec "$W/newN.patch")"
expect "Scale patch with a macro amount, read by 1.2: field 100 kept as 3 unknown bytes, re-saved byte for byte" "bytes $(sz "$W/newN.patch") amount 0.5 unknown-raw 11 plays Add resave same" "$("$W/c12" dec "$W/newN.patch")"
# F1, known and accepted (the 1.2.x release does not read field 100, nothing in 1.3.0 can change that): the sound differs
known="$("$W/c12" dec "$W/newN.patch" | sed 's/.*plays \([A-Za-z]*\).*/\1/')"
if [ "$known" = Add ]; then echo "KNOWN F1  a Scale patch with a macro amount plays as Add on 1.2.x (accepted finding, data kept)"; else echo "FAIL  F1 premise: 1.2.x reads the Scale patch as $known"; fail=$((fail + 1)); fi
[ "$fail" = 0 ] && { echo "RESULT: GREEN (F1 known, accepted)"; exit 0; } || { echo "RESULT: RED ($fail failing)"; exit 1; }
