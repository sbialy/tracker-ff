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
tag_sha=$(git -C "$repo" rev-parse "$tag^{commit}" 2> /dev/null) || skip "$repo has no tag $tag"
# Result cache: the checks run only the pinned engines code under Unicorn (no file of this tree but this script is in the loop), so a pass is
# stored under the sha256 of its inputs (the tag commit, every .plg of PLUGDIR, the checks part of this script (between its CHECKS markers), the toolchain and python versions) in
# RT_CACHE (default build/test-cache of the main checkout, shared by its worktrees, RT_CACHE=off turns it off). A hit prints the stored
# result lines and exits 0, a miss or a failure runs the checks. Only a pass is stored.
cache=${RT_CACHE:-$(cd "$wt" && cd "$(git rev-parse --git-common-dir)/.." && pwd)/build/test-cache}; key=""
if [ "$cache" != off ]; then
  key=$({ echo "tag $tag $tag_sha"; (cd "$plugdir" && shasum -a 256 ./*.plg); sed -n '/^# CHECKS-BEGIN/,/^# CHECKS-END/p' "$HERE/engines_para_check.sh" | shasum -a 256
          "${TOOLCHAIN}gcc" --version 2> /dev/null | head -1; "$py" -c 'import sys, unicorn, numpy, capstone; print(sys.version, unicorn.__version__, numpy.__version__)'
        } | shasum -a 256 | cut -c1-24)
  hit="$cache/engines-para/$key"
  if [ -f "$hit/result.txt" ]; then
    cp "$hit"/*.log "$out/" 2> /dev/null; echo "engines_para_check: cached pass $key ($hit)"; cat "$hit/result.txt"; exit 0
  fi
fi
# CHECKS-BEGIN (the cache key hashes the lines from here to CHECKS-END, so an edit of the header, the skips or the cache code costs no 85 s miss,
# an edit of the checks themselves does: change nothing between the markers that does not change what is checked)
src="$out/engines-src"; rm -rf "$src"; mkdir -p "$src"
git -C "$repo" archive "$tag" tools sdk build_set.py engines.json src | tar -x -C "$src" || skip "git archive of $tag failed"
rc=0   # the checks are independent (each run builds its own harness in a temp dir): the Strum depth check and ab_checks in four groups of
# two algorithms run side by side, ab_checks.log is the four group logs in order
(cd "$src" && "$py" -I tools/strum_depth_check.py --new "$plugdir") > "$out/strum_depth_check.log" 2>&1 & pids=($!)
groups=("Sum,PM" "FM Exp,Ring" "Sign Mix,Chop" "Fold,Sync")
# at most jobs.sh n of the five at a time (the machine-wide width, ORCH_JOBS inside run_tests)
W=$(sh "$HERE/jobs.sh" n 5)
for g in 0 1 2 3; do
  while [ "$(for p in "${pids[@]}"; do kill -0 "$p" 2> /dev/null && echo; done | wc -l)" -ge "$W" ]; do sleep 0.2; done
  (cd "$src" && "$py" -I tools/alco/ab_checks.py --plg "$plugdir/alco.plg" --algs "${groups[$g]}") > "$out/ab_checks.$g.log" 2>&1 & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p" || rc=1; done
for g in 0 1 2 3; do echo "### ab_checks --algs ${groups[$g]}"; cat "$out/ab_checks.$g.log"; done > "$out/ab_checks.log"
# CHECKS-END
tail -1 "$out/strum_depth_check.log"
for g in 0 1 2 3; do printf 'ab_checks %s: %s\n' "${groups[$g]}" "$(tail -1 "$out/ab_checks.$g.log")"; done
if [ "$rc" -eq 0 ] && [ -n "$key" ]; then   # store the pass: a private dir, then one rename (a parallel writer of the same key loses nothing)
  mkdir -p "$cache/engines-para" && tmp=$(mktemp -d "$cache/engines-para/.tmp.XXXXXX") && cp "$out"/strum_depth_check.log "$out"/ab_checks.log "$tmp/" &&
    { tail -1 "$out/strum_depth_check.log"; for g in 0 1 2 3; do printf 'ab_checks %s: %s\n' "${groups[$g]}" "$(tail -1 "$out/ab_checks.$g.log")"; done; } > "$tmp/result.txt" && { [ -e "$hit" ] && rm -rf "$tmp" || mv "$tmp" "$hit"; }
fi
exit $rc
