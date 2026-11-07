#!/bin/sh
# Builds and runs the host unit tests of the USB test console (host, NOT firmware):
#   usbtest_test.cpp      console core (CRC32, RLE, ring, parser, state machines) against a fake Host, with ASan and UBSan
#   keys_inject_test.cpp  the input queue in the firmware's Keys class (hold repeat, Shift, capacity)
#   synth-open/tools/usbtest/run_usbtest_test.sh
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${TMPDIR:-/tmp}/usbtest_test_$$
clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra \
  -DENABLE_DEV_CONSOLE -I "$ROOT/core/source" \
  "$ROOT/synth-open/tools/usbtest/usbtest_test.cpp" "$ROOT/core/source/UsbTestCore.cpp" "$ROOT/core/source/UsbTestFlash.cpp" -lz -o "$OUT"
"$OUT"
rm -f "$OUT"
clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra \
  -DENABLE_DEV_CONSOLE -I "$ROOT/platforms/mk66/3rdparty/Buttons" -I "$ROOT/platforms/mk66/3rdparty/I2C" \
  -I "$ROOT/synth-open/tools/ui_shot/host_stubs" \
  "$ROOT/synth-open/tools/usbtest/keys_inject_test.cpp" "$ROOT/platforms/mk66/3rdparty/Buttons/Keys.cpp" -o "$OUT"
"$OUT"
rc=$?
rm -f "$OUT"
exit $rc
