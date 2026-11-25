#!/usr/bin/env bash
# Host test of the computer -> Tracker ring (FEATURE_USB_AUDIO_IN). Builds with the system c++ and runs it.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${TMPDIR:-/tmp}/usb_audio_in_test
c++ -std=c++17 -O2 -Wall -Wextra -I"$here/.." "$here/usb_audio_in_test.cpp" "$here/../UsbAudioIn.cpp" -o "$out"
"$out"
