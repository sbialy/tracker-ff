#!/usr/bin/env bash
# run_tests.sh without plug/build_plugs.sh (no Arm toolchain, no unicorn needed) and against a plugin set you name:
#   PLUGDIR=DIR_WITH_PLG_AND_PLGI synth-open/tools/run_host_tests.sh [WORKTREE [OUTDIR]]
# Same verdict line: "RESULT: GREEN" (exit 0) or "RESULT: RED (...)" (exit 1).
set -uo pipefail
: "${PLUGDIR:?set PLUGDIR to a directory with the shipped .plg and .plgi (e.g. the fetched modules of run_tests.sh)}"
export PLUGDIR NO_BUILD_PLUGS=1
exec bash "$(cd "$(dirname "$0")" && pwd)/run_tests.sh" "$@"
