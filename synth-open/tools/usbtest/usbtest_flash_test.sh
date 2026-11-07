#!/bin/sh
# The two .ptf validators must agree: the console's PtfValidator (core/source/UsbTestFlash.cpp, runs on the device before a hand-off to
# the bootloader) and ptfcheck.py (runs on the Mac before the first byte is sent). Both judge a generated corpus (ptfgen.py: good images
# and every kind of bad one) and, when present, the real images under build/. Host only, NOT firmware.
#   synth-open/tools/usbtest/usbtest_flash_test.sh        REAL_DIR (default build/) and REAL_IMAGES (12) pick the real images
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
T=$ROOT/synth-open/tools/usbtest
W=$(mktemp -d "${TMPDIR:-/tmp}/ptf_parity.XXXXXX")
clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -DENABLE_DEV_CONSOLE \
  -I "$ROOT/core/source" "$T/ptf_verdict.cpp" "$ROOT/core/source/UsbTestFlash.cpp" "$ROOT/core/source/UsbTestCore.cpp" -lz -o "$W/ptf_verdict"
python3 -I "$T/ptfgen.py" "$W/corpus"
python3 -I - "$T" "$W" $(ls "${REAL_DIR:-$ROOT/build}"/*/*.ptf 2>/dev/null | head -${REAL_IMAGES:-12}) <<'PY'
import os, subprocess, sys
here, work, real = sys.argv[1], sys.argv[2], sys.argv[3:]
sys.path.insert(0, here)
import ptfcheck
want = dict(l.split() for l in open(work + '/corpus/verdicts.txt'))
files = [work + '/corpus/' + n + '.ptf' for n in want] + real
out = subprocess.run([work + '/ptf_verdict'] + files, capture_output=True, text=True, check=True).stdout.splitlines()
bad = 0
for path, line in zip(files, out):
    name = os.path.basename(path)[:-4]
    f, cverdict, ctop, csp, creset = line.rsplit(' ', 4)
    r = ptfcheck.validate(open(path, 'rb').read())
    expected = want.get(name, 'ok')
    same = (r.error == cverdict == expected and r.top == int(ctop) and r.sp == int(csp, 16) and r.reset == int(creset, 16))
    if not same:
        bad += 1
        print('MISMATCH %-28s expected %-14s c++ %-14s python %-14s' % (name, expected, cverdict, r.error))
print('ptf validators: %d files (%d generated, %d real), %d mismatches' % (len(files), len(want), len(real), bad))
sys.exit(1 if bad else 0)
PY
rm -rf "$W"
