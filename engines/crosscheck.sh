#!/bin/bash
# Checks of a finished engine set that need a firmware tree (the engine build itself does not):
#   - tools/validate_synth.py: the firmware's own checkSynth (cut out of plug_synth.cpp, cross-compiled, run in Unicorn) accepts every .plg
#   - synth_render_compare.sh: every engine renders bit identical to the firmware's own objects of the same source (needs the
#     compile lines of make -n at TRIM=1 with every engine, flags follow the set's flavour)
#   - card logos: tools/plug_logo_test.cpp on the .plgi sidecars, a .plg with a logo trailer (sidecar wins), a truncated and a bad magic file
#
#   TOOLCHAIN=.../arm-none-eabi- PY=/tmp/phzvenv/bin/python engines/crosscheck.sh SET_DIR [FIRMWARE_TREE]
#
# FIRMWARE_TREE defaults to this repository. It is only read (the render compare runs make -n and compiles the engine sources into a
# scratch dir). Needs a Python with unicorn in PY.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SET="$(cd "${1:?set dir}" && pwd)"
FW="$(cd "${2:-$HERE/..}" && pwd)"
PY="${PY:-python3}"
: "${TOOLCHAIN:?set TOOLCHAIN to the arm-none-eabi- prefix}"
SO="$FW/synth-open"
flavour=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['flavour'])" "$SET/SET.json")
export SYNTH_PLUG_OPT_ENGINES SYNTH_PLUG_OPT_O3
eval "$(python3 -c "
import json,sys
t=json.load(open(sys.argv[1])); f=t['flavours'][sys.argv[2]]
print('SYNTH_PLUG_OPT_ENGINES=\"%s\"; SYNTH_PLUG_OPT_O3=\"%s\"' % (f['engines'], f['o3']))" "$HERE/engines.json" "$flavour")"
ids=$(python3 -c "import json,sys; print(' '.join(e['id'] for e in json.load(open(sys.argv[1]))['engines']))" "$HERE/engines.json")
fail=0
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
echo "crosscheck: set $SET (flavour $flavour), firmware tree $FW"
(cd "$FW" && "$PY" -I synth-open/tools/validate_synth.py $(for e in $ids; do echo "$SET/$e.plg"; done)) || fail=1
PY="$PY" "$SO/plug/synth_render_compare.sh" "$SET" || fail=1
python3 "$HERE/build_set.py" logo-test --set "$SET" --out "$TMP/logo-test" --root "$FW"
lt="$TMP/logo-test"
# the trailer plugin is the shipped ju6.plg plus the trailer: same image bytes
cmp -s <(head -c "$(wc -c < "$SET/ju6.plg")" "$lt/ju6.plg" | tail -c +161) <(tail -c +161 "$SET/ju6.plg") ||
    { echo "logo FAIL logo-test/ju6.plg image differs from ju6.plg"; fail=1; }
esum() { "$PY" -I "$SO/plug/emu_synth.py" "$1" "$lt/h.plg" --scenario 0 --base 0x10000000 --rw-base 0x20027000 2>&1 | sed -n 's/^output checksum //p'; }
s1=$(esum "$SET/ju6.plg"); s2=$(esum "$lt/ju6.plg")
if [ -n "$s1" ] && [ "$s1" = "$s2" ]; then echo "logo ok  logo-test/ju6.plg (trailer) renders $s2 = ju6.plg"
else echo "logo FAIL logo-test/ju6.plg renders '$s2', ju6.plg '$s1'"; fail=1; fi
c++ -std=c++20 -O1 -Wall -I "$SO" "$SO/tools/plug_logo_test.cpp" -o "$TMP/plug_logo_test"
"$TMP/plug_logo_test" $(for e in $ids; do echo "$SET/$e.plgi"; done) "$lt/ju6.plg" | grep -E "FAIL|PASSED|ok  .*(plgi|plg):" || fail=1
for bad in fm4 btype; do
    if "$TMP/plug_logo_test" "$lt/$bad.plgi" > /dev/null; then echo "logo FAIL logo-test/$bad.plgi passes"; fail=1
    else echo "logo ok  logo-test/$bad.plgi is refused (text name)"; fi
done
[ "$fail" = 0 ] && echo "crosscheck PASSED" || { echo "crosscheck FAILED"; exit 1; }
