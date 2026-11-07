#!/bin/sh
# End to end check on the Mac, no device: the runner (usbtest.py) drives the firmware's console and UI built as ui_shot --serve.
# Builds ui_shot, runs the host unit tests, the scripts (smoke, files, project, pages), the extra checks (gate, slow link, reboot,
# project open, pty serial path) and compares a screenshot that came through the console protocol with the regular ui_shot render.
#   synth-open/tools/usbtest/run_usbtest_e2e.sh            output in build/usbtest-e2e (or $OUT)
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
UI=$ROOT/build/ui-shot/ui_shot
T=$ROOT/synth-open/tools/usbtest
OUT=${OUT:-$ROOT/build/usbtest-e2e}
rm -rf "$OUT"; mkdir -p "$OUT"

echo "== unit tests"
"$T/run_usbtest_test.sh"

echo "== build ui_shot"
make -C "$ROOT/synth-open/tools/ui_shot" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" > "$OUT/make.log" 2>&1 || { tail -30 "$OUT/make.log"; exit 1; }

echo "== reference render (regular ui_shot front end, buttons through mtInterface)"
"$UI" "$OUT/ref_sd" "tap Master;wait 800;shot $OUT/ref_master.png" > /dev/null

echo "== runner: smoke, files, pages"
head -c 5000 /dev/urandom > "$OUT/payload.bin"
python3 -I "$T/usbtest.py" --sim "$UI" --sd "$OUT/sd" --out "$OUT" --cpu-thresholds "$T/cpu_thresholds.json" \
  --report "$OUT/report_main.json" "$T/scripts/smoke.ut" "$T/scripts/files.ut" "$T/scripts/pages.ut"
cmp "$OUT/payload.bin" "$OUT/got.bin"
echo "payload round trip: identical"

echo "== runner: project open, play, stop (project folder copied from the workspace)"
cp -R "$OUT/sd/Workspace" "$OUT/sd/Projects_Demo_tmp"
mkdir -p "$OUT/sd/Projects"; mv "$OUT/sd/Projects_Demo_tmp" "$OUT/sd/Projects/Demo"
python3 -I "$T/usbtest.py" --sim "$UI" --sd "$OUT/sd" --out "$OUT" --report "$OUT/report_project.json" "$T/scripts/project.ut"

echo "== screenshot through the console against the ui_shot render"
python3 -I - "$OUT" <<'PY'
import sys
sys.path.insert(0, sys.argv[1] + '/../../synth-open/tools/usbtest')
import usbtest
d, pct, box = usbtest.png_diff(sys.argv[1] + '/master.png', sys.argv[1] + '/ref_master.png')
print('master.png (console, rle, decoded) against ref_master.png (ui_shot): %d pixels differ (%.4f %%)' % (d, pct))
sys.exit(0 if d == 0 else 1)
PY

echo "== extra checks"
python3 -I "$T/e2e_extra.py" "$UI" "$OUT/extra"

echo "== fault injection: a threshold that is too tight must fail"
if UISHOT_FAKE_CPU=2000,3500 python3 -I "$T/usbtest.py" --sim "$UI" --sd "$OUT/sd_cpu" --out "$OUT" --cpu-thresholds "$T/cpu_thresholds.json" "$T/scripts/smoke.ut" > "$OUT/cpu_fail.log" 2>&1; then
  echo "FAIL: the budget check did not fail"; exit 1
else
  grep "cpu-check" "$OUT/cpu_fail.log"
  echo "budget exceeded -> runner fails, as intended"
fi
echo "ALL OK"
