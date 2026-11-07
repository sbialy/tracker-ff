#!/bin/sh
# Builds and runs usbtest_test.cpp (host, NOT firmware): the USB test console core against a fake Host.
#   synth-open/tools/usbtest/run_usbtest_test.sh
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${TMPDIR:-/tmp}/usbtest_test_$$
clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra \
  -DENABLE_DEV_CONSOLE -I "$ROOT/core/source" \
  "$ROOT/synth-open/tools/usbtest/usbtest_test.cpp" "$ROOT/core/source/UsbTestCore.cpp" -lz -o "$OUT"
"$OUT"
rc=$?
rm -f "$OUT"
exit $rc
