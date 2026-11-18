#!/usr/bin/env bash
# The paraphonic engine ALCO of the pinned engines set, in the Unicorn emulator (Mac only). The checks live in poly-engines at the pinned tag
# (engines/external.lock.json, set `engines`): tools/strum_depth_check.py (the project Strum ramp depth through PLUG_EXT_STRUM: a host without the query is
# depth 100, Strum 0 never asks, depth 0 / 50 / 100 differ, the depth is live, late notes get quieter, ALCO B and sub follow the played note) and
# tools/alco/ab_checks.py (ALCO Chord Osc A and B: a chord equals the sum of its single notes on the algorithms where that is deterministic, Poly ignores it).
#
#   engines_para_check.sh PLUGDIR OUTDIR     PLUGDIR holds the fetched modules (alco.plg). Exit 0 = all pass, 1 = a check failed, 2 = skipped
#   Environment: TOOLCHAIN (arm-none-eabi- prefix, builds the render harness), ENGINES_PY (a python with unicorn, numpy and capstone, default PY),
#                ENGINES_REPO (the poly-engines checkout, default the lock's local_repo)
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); wt=$(cd "$HERE/../.." && pwd)
plugdir=$(cd "${1:?PLUGDIR}" && pwd); out=${2:?OUTDIR}; mkdir -p "$out"; out=$(cd "$out" && pwd)
py=${ENGINES_PY:-${PY:-$(sh "$HERE/pyenv.sh" python)}} || exit 2
skip() { echo "engines_para_check: skipped, $1"; exit 2; }
[ -n "${TOOLCHAIN:-}" ] || skip "no TOOLCHAIN"
"$py" -c 'import unicorn, numpy, capstone' 2> /dev/null || skip "$py has no unicorn, numpy and capstone (set ENGINES_PY)"
[ -f "$plugdir/alco.plg" ] || skip "no alco.plg in $plugdir"
repo=${ENGINES_REPO:-$(python3 -I - "$wt" <<'PY'
import json, os, sys
wt = sys.argv[1]
s = json.load(open(os.path.join(wt, "engines", "external.lock.json")))["sets"]["engines"]
print(os.path.normpath(os.path.join(wt, "engines", "..", s["local_repo"])))
PY
)}
tag=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["sets"]["engines"]["tag"])' "$wt/engines/external.lock.json")
git -C "$repo" rev-parse "$tag^{commit}" > /dev/null 2>&1 || skip "$repo has no tag $tag"
src="$out/engines-src"; rm -rf "$src"; mkdir -p "$src"
git -C "$repo" archive "$tag" tools sdk build_set.py engines.json src | tar -x -C "$src" || skip "git archive of $tag failed"
rc=0
(cd "$src" && "$py" -I tools/strum_depth_check.py --new "$plugdir") > "$out/strum_depth_check.log" 2>&1 || rc=1
tail -1 "$out/strum_depth_check.log"
(cd "$src" && "$py" -I tools/alco/ab_checks.py --plg "$plugdir/alco.plg" --algs Sum,PM,FM\ Exp,Ring,Sign\ Mix,Chop,Fold,Sync) > "$out/ab_checks.log" 2>&1 || rc=1
tail -1 "$out/ab_checks.log"
exit $rc
