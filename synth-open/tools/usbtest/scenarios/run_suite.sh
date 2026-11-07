#!/bin/sh
# The scenario suite against the Mac build (ui_shot --serve): builds ui_shot, runs every scenario, writes the report.
#   run_suite.sh                 suite only, report in build/usbtest-suite/last/suite.md and suite.json
#   run_suite.sh selftest        also proves the suite itself (broken golden fails, regression diff, serial code path), about 3 minutes
#   run_suite.sh -- ARGS...      everything after -- goes to suite.py (--only, --tag, --update-goldens, --save-baseline ...)
set -e
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
T=$ROOT/synth-open/tools/usbtest/scenarios
UI=$ROOT/build/ui-shot/ui_shot
OUT=${OUT:-$ROOT/build/usbtest-suite}

echo "== build ui_shot"
mkdir -p "$OUT"
if ! make -C "$ROOT/synth-open/tools/ui_shot" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" > "$OUT/make.log" 2>&1
then
  tail -30 "$OUT/make.log"
  exit 1
fi

if [ "$1" = "selftest" ]
then
  exec python3 -I "$T/selftest.py" "$UI" "$OUT/selftest"
fi
[ "$1" = "--" ] && shift
echo "== suite"
python3 -I "$T/suite.py" --sim "$UI" --out "$OUT/last" "$@"
