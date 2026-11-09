#!/bin/bash
# The firmware side checks of the plugin modules of the four module repositories, on one flat directory of their published files
# (python3 -I engines/external.py fetch DIR [--from-local] [--visibility public]):
#   - external.py check: every pinned file of the sets in DIR has the sha256 of engines/external.lock.json (NO_LOCK=1 skips it, for a
#     rebuild in the fused flavour, engines/rebuild_pinned.sh)
#   - check_modules.py verify-external: every module against its row in engines/engines.json (numbers, name, version, kind, ABI,
#     visibility and source repository of the manifests)
#   - synth-open/tools/validate_synth.py: the firmware's own checkSynth (cut out of plug_synth.cpp, cross-compiled, run in Unicorn)
#     accepts every synth engine .plg (a refused one shows as Missing on the device)
#   - card logos: synth-open/tools/plug_logo_test.cpp reads every .plgi sidecar as the firmware does
#
#   TOOLCHAIN=.../arm-none-eabi- PY=/path/to/venv/bin/python engines/crosscheck.sh DIR [public|private|all] [--with GROUP]...
#
# The second argument (default: all) picks the sets: public is what a clone without access to the private repositories can check.
# --with GROUP also checks an optional group (the rows with "optional" in engines.json, their pins in the lock, their sidecars).
# Without it those rows are not looked at. No group is defined in 1.2.0.
# The render checks of the engines themselves run in their module repository (make there, PY with unicorn).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SET="$(cd "${1:?module dir}" && pwd)"
shift
VIS=all
case "${1:-}" in public|private|all) VIS="$1"; shift;; esac
with=(); groups=()
while [ $# -gt 0 ]; do
    case "$1" in --with) with+=(--with "${2:?--with needs a group}"); groups+=("$2"); shift 2;; *) echo "crosscheck: unknown argument $1"; exit 2;; esac
done
SO="$HERE/../synth-open"
PY="${PY:-python3}"
: "${TOOLCHAIN:?set TOOLCHAIN to the arm-none-eabi- prefix}"
fail=0
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
vflag=(); [ "$VIS" = all ] || vflag=(--visibility "$VIS")
echo "crosscheck: $SET ($VIS modules)"
if [ -z "${NO_LOCK:-}" ]; then python3 -I "$HERE/external.py" check "$SET" ${vflag[@]+"${vflag[@]}"} ${with[@]+"${with[@]}"} || fail=1
else echo "external.py check skipped (NO_LOCK, a rebuild in another flavour)"; fi
python3 -I "$HERE/check_modules.py" verify-external "$SET" --host-abi "$SO/plug/plug_abi.h" --visibility "$VIS" ${with[@]+"${with[@]}"} || fail=1
ids=$(python3 -c "
import json,sys
t=json.load(open(sys.argv[1]))
print(' '.join(e['id'] for e in t['engines'] if sys.argv[2] in ('all', e['visibility']) and (not e.get('optional') or e['optional'] in sys.argv[3:])))" "$HERE/engines.json" "$VIS" ${groups[@]+"${groups[@]}"})
(cd "$SO/.." && "$PY" -I synth-open/tools/validate_synth.py $(for e in $ids; do echo "$SET/$e.plg"; done)) || fail=1
c++ -std=c++20 -O1 -Wall -I "$SO" "$SO/tools/plug_logo_test.cpp" -o "$TMP/plug_logo_test"
"$TMP/plug_logo_test" $(for e in $ids; do echo "$SET/$e.plgi"; done) | grep -E "FAIL|PASSED|ok  .*plgi:" || fail=1
[ "$fail" = 0 ] && echo "crosscheck PASSED" || { echo "crosscheck FAILED"; exit 1; }
