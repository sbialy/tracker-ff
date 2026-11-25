#!/bin/sh
# The parallel width of host builds and test fan-outs on a shared machine (make -j, xargs -P, the run_tests pools).
#
#   sh synth-open/tools/jobs.sh n [WANT]           print the width to use now (1 or more), never fails
#   sh synth-open/tools/jobs.sh run WANT -- CMD...  run CMD holding up to WANT machine-wide job tokens, ORCH_JOBS exported to CMD
#
# n: ORCH_JOBS when it is set (the hub's grant or a worker's start value), else `orch-jobs.sh n --want WANT` of the orchestrator
# (min of WANT, the free machine-wide tokens and the load headroom), else the CPU count. WANT (default the CPU count) caps all three.
# run: with orch-jobs.sh present and no grant held yet (ORCH_JOBS_HELD unset) the command runs inside `orch-jobs.sh run`, so the
# tokens are freed on any exit. Inside a grant, without the hub, or when the tokens did not come in RT_JOBS_WAIT seconds (default 300),
# the command runs at once with ORCH_JOBS = n. A machine without the orchestrator never waits and never fails here.
#   ORCH_JOBS_BIN   the orch-jobs.sh to use, default ~/prj/orchestrator/bin/orch-jobs.sh, off = never ask the hub
ncpu() { sysctl -n hw.ncpu 2> /dev/null || getconf _NPROCESSORS_ONLN 2> /dev/null || nproc 2> /dev/null || echo 4; }
isnum() { case ${1:-x} in *[!0-9]*|'') return 1;; esac; [ "$1" -ge 1 ]; }
hub() { b=${ORCH_JOBS_BIN:-$HOME/prj/orchestrator/bin/orch-jobs.sh}; [ "$b" != off ] && [ -x "$b" ] && printf '%s' "$b"; }

width() {  # width WANT
  w=$1; isnum "$w" || w=$(ncpu); isnum "$w" || w=4
  if isnum "${ORCH_JOBS:-}"; then j=$ORCH_JOBS
  elif b=$(hub); then j=$("$b" n --want "$w" 2> /dev/null); isnum "$j" || j=4
  else j=$(ncpu); isnum "$j" || j=4; fi
  [ "$j" -gt "$w" ] && j=$w
  echo "$j"
}

case ${1:-} in
  n) width "${2:-}" ;;
  run)
    want=${2:-}; isnum "$want" || { echo "jobs.sh: run WANT -- CMD" >&2; exit 2; }
    shift 2; [ "${1:-}" = -- ] && shift
    [ $# -gt 0 ] || { echo "jobs.sh: run WANT -- CMD" >&2; exit 2; }
    if [ -z "${ORCH_JOBS_HELD:-}" ] && b=$(hub); then
      m=$(mktemp "${TMPDIR:-/tmp}/jobs_sh.XXXXXX") || m=""
      [ -n "$m" ] && rm -f "$m"
      # the marker tells "the command ran and exited 3" from "orch-jobs gave up waiting, command not run" (both exit 3)
      "$b" run --want "$want" --wait "${RT_JOBS_WAIT:-300}" -- sh -c '[ -z "$1" ] || : > "$1"; shift; ORCH_JOBS_HELD=1; export ORCH_JOBS_HELD; exec "$@"' sh "$m" "$@"
      rc=$?
      if [ "$rc" -eq 3 ] && [ -n "$m" ] && [ ! -e "$m" ]; then
        echo "jobs.sh: no job tokens in ${RT_JOBS_WAIT:-300} s, running at width 1 without them" >&2
        ORCH_JOBS=1 ORCH_JOBS_HELD=1; export ORCH_JOBS ORCH_JOBS_HELD; "$@"; rc=$?
      fi
      [ -n "$m" ] && rm -f "$m"
      exit "$rc"
    fi
    ORCH_JOBS=$(width "$want"); export ORCH_JOBS; exec "$@" ;;
  *) sed -n '2,13p' "$0" >&2; exit 2 ;;
esac
