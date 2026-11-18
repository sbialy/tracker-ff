#!/usr/bin/env bash
# Strum ramp depth host test (strum_depth_test.cpp): the ramp build at depth 0 prints the digest of the STRUM_RAMP=0 build, at depth 100
# the digest of the ramp before the depth existed. Host only. Usage: run_strum_depth_test.sh [WORKTREE]
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
wt=$(cd "${1:-$HERE/../..}" && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/sdt.XXXXXX"); trap 'rm -rf "$T"' EXIT
cc() { c++ -std=c++20 -O1 -Wall -I "$wt/synth-open" "$@" "$wt/synth-open/tools/strum_depth_test.cpp" -o "$T/$OUT"; }
bad=0
OUT=ramp;  cc               || { echo "BUILD FAIL ramp"; exit 1; }
OUT=flat;  cc -DSTRUM_RAMP=0 || { echo "BUILD FAIL flat"; exit 1; }
"$T/ramp" > "$T/ramp.out"; r1=$?
"$T/flat" > "$T/flat.out"; r2=$?
cat "$T/ramp.out"; echo "--- STRUM_RAMP=0"; cat "$T/flat.out"
[ $r1 -eq 0 ] && [ $r2 -eq 0 ] || bad=$((bad+1))
d0r=$(awk '$1=="DIGEST0"{print $2}' "$T/ramp.out"); d0f=$(awk '$1=="DIGEST0"{print $2}' "$T/flat.out")
if [ -n "$d0r" ] && [ "$d0r" = "$d0f" ]; then echo "ok    depth 0 of the ramp build == STRUM_RAMP=0 build ($d0r)"; else echo "FAIL  depth 0 digest $d0r vs STRUM_RAMP=0 $d0f"; bad=$((bad+1)); fi
echo "RESULT: $([ $bad -eq 0 ] && echo GREEN || echo RED)"
[ $bad -eq 0 ]
