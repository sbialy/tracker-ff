#!/bin/bash
# wave_ref_compat_check.sh - the wave file references (patch field 101) through the patch codec of this tree (the new firmware) and of an old
# firmware (OLD_REF, default the latest tracker-ff-v<A.B.C> release tag). The compat rule for a field added in a release (user rule 2026-11-12):
#   1. a patch without the field plays and saves exactly as before: the new codec writes the same bytes as the old codec for it, and re-saves it
#      unchanged (no field is written for a patch with no reference, so an old patch never grows)
#   2. a patch with the field is a patch of the same values plus the field: the old codec keeps the field as unknown bytes and writes it back
#      byte for byte, and reads the same values, so it plays the index it always played
#   3. an old firmware that changes User keeps the (now out of date) field: the new codec then sees the reference as stale and the index wins
# Exit 0 when every expectation holds, 1 otherwise.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REF="${OLD_REF:-$(git -C "$ROOT" tag --list 'tracker-ff-v[0-9]*' --sort=-v:refname | grep -E '^tracker-ff-v[0-9]+\.[0-9]+\.[0-9]+$' | head -1)}"
W="$(mktemp -d "${TMPDIR:-/tmp}/wave_ref_compat.XXXXXX")"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/old" && git -C "$ROOT" archive "$REF" synth-open | tar -x -C "$W/old" || { echo "no $REF" >&2; exit 2; }
T=$ROOT/synth-open/tools/wave_ref_compat_test.cpp
build() { c++ -std=c++20 -O1 $3 -I "$1/synth-open" -I "$1/synth-open/ot" -I "$ROOT/synth/include" -I "$ROOT/core/synth" -I "$ROOT/util/include" "$T" "$1/synth-open/open_patchio.cpp" -o "$2" 2>"$W/build.log" || { cat "$W/build.log" >&2; exit 2; }; }
if grep -q 'kWaveField' "$W/old/synth-open/open_patchdata.h"; then OLDREADS=1; else OLDREADS=0; fi
build "$ROOT" "$W/cn" -DTREE_WAVES
if [ "$OLDREADS" = 1 ]; then build "$W/old" "$W/co" -DTREE_WAVES; else build "$W/old" "$W/co" ""; fi
fail=0
expect() { if [ "$2" = "$3" ]; then echo "PASS  $1"; else echo "FAIL  $1"; echo "      expected: $2"; echo "      got:      $3"; fail=$((fail + 1)); fi; }
sz() { wc -c <"$1" | tr -d ' '; }
sha() { shasum "$1" | cut -c1-16; }
echo "old tree $REF (reads field 101: $OLDREADS), new tree $(git -C "$ROOT" rev-parse --short HEAD)"
"$W/co" enc plain "$W/plain_old.patch"; "$W/cn" enc plain "$W/plain_new.patch"
"$W/cn" enc ref2 "$W/ref2.patch"; "$W/cn" enc ref1 "$W/ref1.patch"; "$W/cn" enc refidx0 "$W/refidx0.patch"
echo "bytes: plain old $(sz "$W/plain_old.patch") new $(sz "$W/plain_new.patch"), ref1 $(sz "$W/ref1.patch"), ref2 $(sz "$W/ref2.patch"), refidx0 $(sz "$W/refidx0.patch")"
# 1. a patch without references
expect "no reference: the new codec writes the same bytes as the old one (no field 101)" "$(sha "$W/plain_old.patch")" "$(sha "$W/plain_new.patch")"
expect "no reference: the new codec reads it, no reference, re-saves it unchanged" "bytes $(sz "$W/plain_old.patch") user 37 userb 0 unknown-raw 8 refs 0 resave same" "$("$W/cn" dec "$W/plain_old.patch")"
expect "no reference: field 101 (tag aa 06) is not in the file" "0" "$(xxd -p "$W/plain_new.patch" | tr -d '\n' | grep -c 'aa06')"
# 2. a patch with references, read by the old codec
expect "ref1: 'ref' patches are the plain patch plus the field only (size grows by the field, tag aa 06 in it)" "1" "$(xxd -p "$W/ref1.patch" | tr -d '\n' | grep -c 'aa06')"
expect "ref2, read by the new codec: two references, both in force (index = the value), re-saved unchanged" "bytes $(sz "$W/ref2.patch") user 37 userb 0 unknown-raw 8 refs 2 resave same | param 16 index 37 AKWF_0004/AKWF_0301.wav in-force | param 21 index 0 AKWF_vgame/AKWF_vgame_0009.wav in-force" "$("$W/cn" dec "$W/ref2.patch")"
expect "refidx0 (file past the cap, index 0), new codec: in force while User is 0" "bytes $(sz "$W/refidx0.patch") user 0 userb 0 unknown-raw 8 refs 1 resave same | param 16 index 0 AKWF_vgame/AKWF_vgame_0009.wav in-force" "$("$W/cn" dec "$W/refidx0.patch")"
if [ "$OLDREADS" = 1 ]; then
    expect "ref2, read by $REF (it reads field 101 as well): the same" "bytes $(sz "$W/ref2.patch") user 37 userb 0 unknown-raw 8 refs 2 resave same | param 16 index 37 AKWF_0004/AKWF_0301.wav in-force | param 21 index 0 AKWF_vgame/AKWF_vgame_0009.wav in-force" "$("$W/co" dec "$W/ref2.patch")"
else
    ref2extra=$(( $(sz "$W/ref2.patch") - $(sz "$W/plain_new.patch") + 8 ))
    expect "ref2, read by $REF: the same values (User 37, User B 0 = what it plays), the field kept as $((ref2extra - 8)) unknown bytes, re-saved byte for byte" "bytes $(sz "$W/ref2.patch") user 37 userb 0 unknown-raw $ref2extra refs 0 resave same" "$("$W/co" dec "$W/ref2.patch")"
    expect "refidx0, read by $REF: User 0 (the built-in wave), field kept, re-saved byte for byte" "bytes $(sz "$W/refidx0.patch") user 0 userb 0 unknown-raw $(( $(sz "$W/refidx0.patch") - $(sz "$W/plain_new.patch") + 8 )) refs 0 resave same" "$("$W/co" dec "$W/refidx0.patch")"
fi
# 3. the old firmware changes User and saves: the field stays, the new firmware sees the reference as out of date
"$W/co" edit "$W/ref2.patch" "$W/edited.patch" 16 12
expect "an old firmware moves User 37 -> 12 and saves: the new codec reads User 12 and the reference as stale (the index plays)" "bytes $(sz "$W/edited.patch") user 12 userb 0 unknown-raw 8 refs 2 resave same | param 16 index 37 AKWF_0004/AKWF_0301.wav stale | param 21 index 0 AKWF_vgame/AKWF_vgame_0009.wav in-force" "$("$W/cn" dec "$W/edited.patch")"
# 4. a reference the codec cannot use stays as it is (kept raw)
python3 -I - "$W" <<'PY'
import sys
w = sys.argv[1]
base = open(w + '/plain_new.patch', 'rb').read()
def field(body): return bytes([0xAA, 0x06, len(body)]) + body
def ref(param, path, index=0):
    b = b''
    if param: b += bytes([0x08, param])
    b += bytes([0x12, len(path)]) + path
    if index: b += bytes([0x18, index])
    return b
cases = {
  'dotdot': ref(16, b'../x.wav', 5),
  'future': ref(16, b'a.wav', 5) + bytes([0x20, 1]),      # a sub field this release does not know
  'toolong': ref(16, b'a' * 60 + b'.wav', 5),
  'five': None,
}
for name, body in cases.items():
    if body is None:
        data = base + b''.join(field(ref(p, b'f%d.wav' % p, 1)) for p in (1, 2, 3, 4, 5))
    else:
        data = base + field(body)
    open('%s/%s.patch' % (w, name), 'wb').write(data)
PY
for c in dotdot future toolong; do
    n=$(( $(sz "$W/$c.patch") - $(sz "$W/plain_new.patch") + 8 ))
    expect "an unusable reference ($c): no reference taken, the field kept raw ($((n - 8)) bytes), re-saved byte for byte" "bytes $(sz "$W/$c.patch") user 37 userb 0 unknown-raw $n refs 0 resave same" "$("$W/cn" dec "$W/$c.patch")"
done
expect "five references: the first four are taken, the fifth kept raw, re-saved byte for byte" "refs 4 resave same" "$("$W/cn" dec "$W/five.patch" | sed 's/.*\(refs [0-9]* resave [a-zA-Z]*\).*/\1/; s/ | .*//' | sed 's/.*\(refs [0-9]* resave [a-zA-Z]*\)/\1/')"
if [ "$fail" = 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fail failing)"; exit 1
