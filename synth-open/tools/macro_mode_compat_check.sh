#!/bin/bash
# macro_mode_compat_check.sh - the macro depth mode (patch field 100) through the patch codec of this tree (the new firmware) and of the old
# firmware (OLD_REF, default release-1.2.0-pub6, FW12_REF is the old name of the variable): bytes written, what each codec reads back and
# what it plays, for
#   old   a patch of the pre field 100 kind (Add, macro amount 0.5), old0 the same without a macro amount
#   new0  a patch of this tree (Scale, no macro amount)
#   newN  a patch of this tree (Scale, macro amount 0.5)
# The contract depends on what the OLD tree reads, which is taken from the tree itself (kMacroModeField in open_patchdata.h):
#   old tree without field 100 (1.2.x): an old patch loads identically on the new tree (no field 100 = Add, same bytes written back), and a
#   new patch round-trips through the old codec byte for byte (field 100 is kept as 3 unknown bytes). A Scale patch with a macro amount
#   plays as Add there: F1, a known accepted finding (reported as KNOWN, not as a failure).
#   old tree with field 100 (1.3.x and later): both codecs read and write the same, an old patch and a new patch give the same bytes, the
#   same sound (Add or Scale) and an unchanged re-save on both. F1 does not exist for that pair.
# Exit 0 when every expectation holds, 1 otherwise.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REF="${OLD_REF:-${FW12_REF:-release-1.2.0-pub6}}"
W="$(mktemp -d "${TMPDIR:-/tmp}/macro_mode_compat.XXXXXX")"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/t12" && git -C "$ROOT" archive "$REF" synth-open | tar -x -C "$W/t12" || { echo "no $REF" >&2; exit 2; }
T=$ROOT/synth-open/tools/macro_mode_compat_test.cpp
build() { c++ -std=c++20 -O1 $3 -I "$1/synth-open" -I "$1/synth-open/ot" -I "$ROOT/synth/include" -I "$ROOT/core/synth" -I "$ROOT/util/include" "$T" "$1/synth-open/open_patchio.cpp" -o "$2" 2>"$W/build.log" || { cat "$W/build.log" >&2; exit 2; }; }
NV="$(sed -n 's/^SYNTH_OPEN_VER[[:space:]]*:\{0,1\}=[[:space:]]*//p' "$ROOT/version.mk" | head -1 | sed 's/-.*//')"
OV="$(git -C "$ROOT" show "$REF:version.mk" 2>/dev/null | sed -n 's/^SYNTH_OPEN_VER[[:space:]]*:\{0,1\}=[[:space:]]*//p' | head -1 | sed 's/-.*//')"; OV="${OV:-old}"
OLDF100=0; grep -q 'kMacroModeField' "$W/t12/synth-open/open_patchdata.h" && OLDF100=1
build "$ROOT" "$W/c13" -DTREE13
if [ "$OLDF100" = 1 ]; then build "$W/t12" "$W/c12" -DTREE13; else build "$W/t12" "$W/c12" ""; fi
fail=0
expect() { if [ "$2" = "$3" ]; then echo "PASS  $1"; else echo "FAIL  $1"; echo "      expected: $2"; echo "      got:      $3"; fail=$((fail + 1)); fi; }
"$W/c12" enc old "$W/old12.patch"; "$W/c12" enc old0 "$W/old012.patch"
"$W/c13" enc old "$W/old13.patch"; "$W/c13" enc old0 "$W/old013.patch"; "$W/c13" enc new0 "$W/new0.patch"; "$W/c13" enc newN "$W/newN.patch"
sz() { wc -c <"$1" | tr -d ' '; }
echo "old tree $OV (reads field 100: $OLDF100), new tree $NV"
echo "bytes: $OV old $(sz "$W/old12.patch"), $NV old $(sz "$W/old13.patch"), new0 $(sz "$W/new0.patch"), newN $(sz "$W/newN.patch")"
# an old patch loads identically on the new tree
expect "old patch: the $NV codec writes the same bytes as the $OV codec" "$(shasum "$W/old12.patch" | cut -c1-16)" "$(shasum "$W/old13.patch" | cut -c1-16)"
expect "old patch without a macro amount: the same bytes on both codecs" "$(shasum "$W/old012.patch" | cut -c1-16)" "$(shasum "$W/old013.patch" | cut -c1-16)"
expect "old patch, read by $NV: Add, amount kept, re-saved unchanged" "bytes $(sz "$W/old12.patch") amount 0.5 unknown-raw 8 plays Add resave same" "$("$W/c13" dec "$W/old12.patch")"
expect "old patch, read by $OV: Add" "bytes $(sz "$W/old12.patch") amount 0.5 unknown-raw 8 plays Add resave same" "$("$W/c12" dec "$W/old12.patch")"
expect "old patch without a macro amount, read by $NV: Add (no field 100, as before), re-saved unchanged" "bytes $(sz "$W/old012.patch") amount 0 unknown-raw 8 plays Add resave same" "$("$W/c13" dec "$W/old012.patch")"
# new patches round-trip through the old codec byte for byte
expect "new patch with no macro amount: $NV writes field 100 (a0 06 01 at the end), 3 bytes more than the $OV file" "$(( $(sz "$W/old012.patch") + 3 )) a00601" "$(sz "$W/new0.patch") $(tail -c 3 "$W/new0.patch" | xxd -p)"
expect "new patch with no macro amount, read by $NV: Scale, re-saved unchanged" "bytes $(sz "$W/new0.patch") amount 0 unknown-raw 8 plays Scale resave same" "$("$W/c13" dec "$W/new0.patch")"
expect "Scale patch with a macro amount: $NV writes field 100 (a0 06 01 at the end), 3 bytes more" "$(( $(sz "$W/old12.patch") + 3 )) a00601" "$(sz "$W/newN.patch") $(tail -c 3 "$W/newN.patch" | xxd -p)"
expect "Scale patch with a macro amount, read by $NV: Scale, re-saved unchanged" "bytes $(sz "$W/newN.patch") amount 0.5 unknown-raw 8 plays Scale resave same" "$("$W/c13" dec "$W/newN.patch")"
if [ "$OLDF100" = 1 ]; then
    # the old tree reads field 100 too: the same sound and the same bytes as the new tree, no unknown bytes
    expect "new patch with no macro amount, read by $OV: Scale like $NV, re-saved unchanged (it reads field 100)" "bytes $(sz "$W/new0.patch") amount 0 unknown-raw 8 plays Scale resave same" "$("$W/c12" dec "$W/new0.patch")"
    expect "Scale patch with a macro amount, read by $OV: Scale like $NV, re-saved unchanged (it reads field 100)" "bytes $(sz "$W/newN.patch") amount 0.5 unknown-raw 8 plays Scale resave same" "$("$W/c12" dec "$W/newN.patch")"
    echo "PASS  F1 does not apply: $OV reads field 100, so Scale patches sound the same on both releases"
else
    expect "new patch with no macro amount, read by $OV: field 100 kept as 3 unknown bytes, re-saved byte for byte (no amount, so the sound is the same)" "bytes $(sz "$W/new0.patch") amount 0 unknown-raw 11 plays Add resave same" "$("$W/c12" dec "$W/new0.patch")"
    expect "Scale patch with a macro amount, read by $OV: field 100 kept as 3 unknown bytes, re-saved byte for byte" "bytes $(sz "$W/newN.patch") amount 0.5 unknown-raw 11 plays Add resave same" "$("$W/c12" dec "$W/newN.patch")"
    # F1, known and accepted (the $OV release does not read field 100, nothing in the new tree can change that): the sound differs
    known="$("$W/c12" dec "$W/newN.patch" | sed 's/.*plays \([A-Za-z]*\).*/\1/')"
    if [ "$known" = Add ]; then echo "KNOWN F1  a Scale patch with a macro amount plays as Add on $OV (accepted finding, data kept)"; else echo "FAIL  F1 premise: $OV reads the Scale patch as $known"; fail=$((fail + 1)); fi
fi
if [ "$fail" = 0 ]; then
    [ "$OLDF100" = 1 ] && echo "RESULT: GREEN" || echo "RESULT: GREEN (F1 known, accepted)"
    exit 0
fi
echo "RESULT: RED ($fail failing)"; exit 1
