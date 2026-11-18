#!/usr/bin/env bash
# run.sh [plain|san|all]   Host tests of the USB drive (tracker-ff USB_MSC=1), system c++ (clang on the Mac), nothing touches a device:
#   msc_bot_test         Bulk-Only Transport + SCSI layer (msc_bot.cpp) against fake endpoints and a RAM disk (OpenTracker, Apache-2.0)
#   usb_msc_driver_test  the endpoint driver (teensy_core/usb_msc_impl.h) against a simulated Kinetis USB-FS controller, then the Bot on
#                        top of it (OpenTracker), with this tree's usb_desc.h (-DFEATURE_USB_MSC: endpoints 9 IN, 10 OUT)
#   usb_desc_test.sh     the descriptors of this tree's usb_desc.c against the R130 line in every flag / boot flag combination
#   fs_host/s3_fat_roundtrip.sh  row S3: the real FileSystemImpl, SD.cpp and FatFs with the card lent to macOS (msdosfs) and back
#   fs_host/s4_bot_replay.sh     row S4: macOS's own writes replayed as SCSI sessions through the Bot into the real block path
#   usb_e2e/s5_usb_e2e.sh        row S5: the same through the real usb_dev.c on a simulated USB-FS controller and the drive's pump:
#                                macOS, Windows, cable pull, Bulk-Only error paths, Exit with a read in flight, drive off, +USB audio
# (rows S2 and S6 run in the simulator: synth-open/tools/ui_shot/usb_msc_sim.sh with an EXTHOOKS=1 USB_MSC=1 ui_shot)
# plain = -O2, san = -O1 ASan + UBSan (no recover). Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (...)".
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd); mk=$(cd "$here/../.." && pwd)
mode=${1:-all}
out=${TMPDIR:-/tmp}/usbmsc_test.$$; mkdir -p "$out"; trap 'rm -rf "$out"' EXIT
CXX=${CXX:-c++}
BASE=(-std=c++17 -Wall -Wextra -Werror -g -ffp-contract=off -I"$here" -I"$mk/usbmsc")
USB=(-D__MK66FX1M0__ -DF_CPU=180000000 -DUSB_MIDI -DFEATURE_USB_MSC -I"$mk/3rdparty/teensy_core")
bad=""
run1() {  # variant flags...
  local v=$1; shift
  for t in msc_bot_test usb_msc_driver_test; do
    local extra=(); [ $t = usb_msc_driver_test ] && extra=("${USB[@]}")
    if ! $CXX "${BASE[@]}" "$@" "${extra[@]}" "$here/$t.cpp" "$mk/usbmsc/msc_bot.cpp" -o "$out/$t-$v" 2> "$out/$t-$v.cc"; then
      echo "FAIL  $t ($v): compile"; head -20 "$out/$t-$v.cc"; bad="$bad $t-$v"; continue
    fi
    if "$out/$t-$v" > "$out/$t-$v.log" 2>&1; then echo "ok    $t ($v): $(tail -1 "$out/$t-$v.log")"
    else echo "FAIL  $t ($v)"; tail -20 "$out/$t-$v.log"; bad="$bad $t-$v"; fi
  done
}
[ "$mode" = san ] || run1 plain -O2
[ "$mode" = plain ] || run1 san -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
if "$here/usb_desc_test.sh" > "$out/desc.log" 2>&1; then echo "ok    usb_desc_test.sh: $(grep -c '^ok' "$out/desc.log") checks"
else cat "$out/desc.log"; bad="$bad usb_desc"; fi
for t in fs_host/s3_fat_roundtrip fs_host/s4_bot_replay usb_e2e/s5_usb_e2e; do
  if "$here/$t.sh" "$mode" > "$out/$(basename $t).log" 2>&1; then echo "ok    $(basename $t).sh ($mode): $(grep -c '^ok' "$out/$(basename $t).log") passes"
  else grep -E "^ok|^FAIL" "$out/$(basename $t).log"; bad="$bad $t"; fi
done
if [ -z "$bad" ]; then echo "RESULT: GREEN"; else echo "RESULT: RED ($bad )"; exit 1; fi
