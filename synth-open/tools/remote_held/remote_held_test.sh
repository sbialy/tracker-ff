#!/bin/bash
# Host test of the MIDI remote's key and pad state (remote_held_test.cpp), no Arm toolchain, no device. Builds the firmware's own
# Keys.cpp, ButtonsBaseImpl.cpp and midiRemote.cpp with ASan and UBSan against host stand-ins and runs the rows H1 to H7.
#   remote_held_test.sh [FW_TREE]    FW_TREE is the checkout to test, default this one. A tree without the fix (the base commit)
#                                    runs rows H1 to H4 only and is expected to fail them.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FW="${1:-$(cd "$HERE/../../.." && pwd)}"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/remote_held.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
FIX=""; grep -q "void physical" "$FW/platforms/mk66/3rdparty/Buttons/Keys.h" && FIX="-DREMOTE_HELD_FIX"
grep -q "releaseSource" "$FW/core/source/midiRemote.h" && FIX="$FIX -DREMOTE_HELD_UNPLUG"
CXX="${CXX:-c++}"
FLAGS=(-std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra $FIX
       -I "$HERE/stubs" -I "$FW/platforms/mk66/3rdparty/Buttons" -I "$FW/platforms/mk66/3rdparty/I2C" -I "$FW/platforms/mk66/source"
       -I "$FW/core/include" -I "$FW/core/source")
"$CXX" "${FLAGS[@]}" -c "$FW/platforms/mk66/3rdparty/Buttons/Keys.cpp" -o "$OUT/keys.o"
"$CXX" "${FLAGS[@]}" -c "$FW/platforms/mk66/source/ButtonsBaseImpl.cpp" -o "$OUT/base.o"
"$CXX" "${FLAGS[@]}" -DFEATURE_EXT_HOOKS -c "$FW/core/source/midiRemote.cpp" -o "$OUT/remote.o"
"$CXX" "${FLAGS[@]}" -c "$HERE/remote_held_test.cpp" -o "$OUT/test.o"
"$CXX" -fsanitize=address,undefined "$OUT"/*.o -o "$OUT/test"
"$OUT/test"
