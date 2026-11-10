#!/usr/bin/env bash
# Host test of the USB audio ring (tracker-ff USB_AUDIO=1). Builds with the system c++ and runs it.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${TMPDIR:-/tmp}/usb_audio_out_test
c++ -std=c++17 -O2 -Wall -Wextra -I"$here" -I"$here/.." "$here/usb_audio_out_test.cpp" "$here/../UsbAudioOut.cpp" -o "$out"
"$out"
