#!/usr/bin/env bash
# s5_usb_e2e.sh [plain|san|all]   Simulator row S5 of ref/usb-mass-storage-feasibility.md (phase 1b) on the Mac: the USB drive end to end.
# The real teensy_core/usb_dev.c (text transform: the write-1-to-clear register writes become calls, see usbsim.c) and usb_desc.c run on a
# simulated Kinetis USB-FS controller (usbsim.c) driven like a macOS or Windows host (control transfers, 64-byte bulk packets, DATA0/1,
# stalls, SOF), under them the Bot, the Pump (the drive's main loop) and the firmware's real block path (FileSystemImpl, SD.cpp, FatFs) on
# a card image that macOS formats and writes (hdiutil, newfs_msdos). Sessions: macos, windows, pull (cable pulled in a write), reset
# (Bulk-Only error paths), exit (a read in flight at Exit), off (drive disabled at start), drop (negative control). Each in a build without
# and with the USB audio function, plain and ASan/UBSan. No device, no /Volumes, no serial port.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED".
set -uo pipefail
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1}
here=$(cd "$(dirname "$0")" && pwd); mk=$(cd "$here/../../.." && pwd); root=$(cd "$mk/../.." && pwd)
mode=${1:-all}
out=${TMPDIR:-/tmp}/usbmsc_s5.$$; mkdir -p "$out"; dev=""
trap '[ -n "$dev" ] && hdiutil detach "$dev" > /dev/null 2>&1; rm -rf "$out"' EXIT
SD=$mk/3rdparty/SD; TC=$mk/3rdparty/teensy_core; FS=$here/../fs_host
INC=(-I"$FS/shim" -I"$SD" -I"$SD/fatfs/source" -I"$SD/fatfs/source/fsl_sd_disk" -I"$mk/source" -I"$root/core/include" -I"$mk/usbmsc" -I"$here" -I"$TC")
DEF=(-DFEATURE_USB_MSC -DUSB_MIDI -D__MK66FX1M0__ -DF_CPU=180000000 -DTEENSYDUINO=142 -DLAYOUT_US_ENGLISH)
rc=0
# usb_dev.c as the host build sees it: the write-1-to-clear writes (USB0_ISTAT = X) go through usbsim_w1c, the section attribute of the
# descriptor table and the breakpoint of the reboot stub are host-incompatible. Nothing else changes.
mkdir -p "$out/gen"
sed -E -e 's/^([[:space:]]*)USB0_(ISTAT|ERRSTAT|OTGISTAT) = (.*);/\1usbsim_w1c(\&USB0_\2, \3);/' \
       -e 's/^__attribute__ \(\(section\("\.usbdescriptortable"\), used\)\)$//' -e 's/__asm__ volatile\("bkpt"\);/abort();/' "$TC/usb_dev.c" > "$out/gen/usb_dev_sim.c"
n=$(diff "$TC/usb_dev.c" "$out/gen/usb_dev_sim.c" | grep -c '^>')
[ "$n" = 13 ] || { echo "RESULT: RED (usb_dev.c transform changed $n lines, expected 13: the register writes or the host edits moved)"; exit 1; }
build() {  # name audio(0|1) flags...
  local n=$1 a=$2; shift 2
  local o=$out/obj-$n; mkdir -p "$o"
  local AD=(); [ "$a" = 1 ] && AD=(-DFEATURE_USB_AUDIO)
  for c in ff.c ffunicode.c diskio.c fsl_sd_disk/fsl_sd_disk.c; do cc -std=gnu11 -w -g "$@" "${INC[@]}" "${DEF[@]}" -c "$SD/fatfs/source/$c" -o "$o/$(basename "${c%.c}").o" || return 1; done
  cc -std=gnu11 -w -g "$@" "${DEF[@]}" "${AD[@]}" -include "$here/simregs.h" -I"$here" -I"$out/gen" -I"$TC" -I"$root/core/include" -I"$mk/config" -c "$here/usbsim.c" -o "$o/usbsim.o" || return 1
  cc -std=gnu11 -w -g "$@" "${DEF[@]}" "${AD[@]}" -include "$here/simregs.h" -I"$here" -I"$TC" -c "$here/usb_desc_tu.c" -o "$o/usb_desc_tu.o" || return 1
  for c in "$SD/SD.cpp" "$SD/SdFile.cpp" "$mk/source/FileSystemImpl.cpp" "$mk/source/util_string.cpp" "$mk/usbmsc/msc_bot.cpp" "$mk/usbmsc/msc_pump.cpp" \
           "$FS/fake_sd.cpp" "$here/s5_usb_e2e.cpp"; do
    c++ -std=gnu++17 -g -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-unused-variable "$@" "${INC[@]}" "${DEF[@]}" "${AD[@]}" \
      -include cstdarg -include cassert -c "$c" -o "$o/$(basename "${c%.cpp}").o" 2> "$o/err" || { head -30 "$o/err"; return 1; }
  done
  c++ "$@" "$o"/*.o -o "$out/s5-$n"
}
attach() { dev=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$1" | awk '{print $1; exit}'); }
detach() { hdiutil detach "$dev" > /dev/null; dev=""; }
mkfile -n 128m "$out/before.img" && attach "$out/before.img" && newfs_msdos -F 32 -v TRACKER "${dev/disk/rdisk}" > /dev/null 2>&1 || { echo "RESULT: RED (image)"; exit 1; }
mkdir -p "$out/mnt"; diskutil mount -mountPoint "$out/mnt" "$dev" > /dev/null
mkdir -p "$out/mnt/Workspace" "$out/mnt/Samples"; head -c 4000 /dev/urandom > "$out/mnt/Workspace/project.mt"
for i in 1 2 3; do head -c $((i * 90000)) /dev/urandom > "$out/mnt/Samples/old_$i.wav"; done
diskutil unmount "$out/mnt" > /dev/null; detach
cp "$out/before.img" "$out/ref.img"; attach "$out/ref.img"; diskutil mount -mountPoint "$out/mnt" "$dev" > /dev/null
mkdir -p "$out/mnt/Copied"; for i in 1 2 3 4 5 6; do head -c $((i * 51234)) /dev/urandom > "$out/mnt/Copied/take_$i.wav"; done
head -c 1800000 /dev/urandom > "$out/mnt/Copied/long.wav"
rm "$out/mnt/Samples/old_2.wav"; head -c 4100 /dev/urandom > "$out/mnt/Workspace/project.mt"
diskutil unmount "$out/mnt" > /dev/null; detach
fsck_msdos -n "$out/ref.img" > /dev/null 2>&1 || { echo "RESULT: RED (macOS's own image fails fsck)"; exit 1; }
session() {  # variant label mode [audio]
  local v=$1 l=$2 m=$3 a=${4:-}
  cp "$out/before.img" "$out/work.img"
  if "$out/s5-$v" "$out/work.img" "$out/ref.img" $m $a > "$out/$v-$l.log" 2>&1; then
    echo "ok    $l ($v): $(grep -E "^$m[+a-z]*:.*(mount reads|session|took|checks)|checks" "$out/$v-$l.log" | tr '\n' ' ' | cut -c1-300)"
  else
    echo "FAIL  $l ($v), log kept in ${KEEP:-/tmp}/usbmsc_s5-$v-$l.log"; cp "$out/$v-$l.log" "${KEEP:-/tmp}/usbmsc_s5-$v-$l.log"
    grep -E "^FAIL|ERROR: AddressSanitizer|runtime error|SUMMARY|^    #[0-9] " "$out/$v-$l.log" | head -14; rc=1
  fi
  if [ "$m" != off ] && [ "$m" != exit ] && [ "$m" != drop ]; then
    local f; f=$(fsck_msdos -n "$out/work.img" 2>&1)
    echo "$f" | grep -qiE "fix|corrupt|lost|cross|error|bad" && { echo "FAIL  fsck_msdos -n after $l ($v)"; echo "$f" | tail -5; rc=1; }
  fi
}
run1() {  # variant flags...
  local v=$1; shift
  if ! build "$v" 0 "$@"; then echo "FAIL  build ($v)"; rc=1; return; fi
  for m in macos windows pull reset exit off; do session "$v" "$m" "$m"; done
  # negative control: the same macOS session with its last WRITE(10) left out must be caught by the image compare
  cp "$out/before.img" "$out/work.img"
  if "$out/s5-$v" "$out/work.img" "$out/ref.img" drop > "$out/$v-drop.log" 2>&1; then echo "FAIL  negative control ($v): a dropped write passed"; rc=1
  elif grep -q "blocks differ from macOS's image" "$out/$v-drop.log"; then echo "ok    negative control ($v): $(grep -o '[0-9]* blocks differ from macOS.s image' "$out/$v-drop.log")"
  else echo "FAIL  negative control ($v): failed for another reason"; grep "^FAIL" "$out/$v-drop.log" | head -3; rc=1; fi
  # the same with the USB audio function compiled in: audio off at start, and on (the drive moves to interface 6)
  if ! build "$v-a" 1 "$@"; then echo "FAIL  build audio ($v)"; rc=1; return; fi
  session "$v-a" macos-audio-off macos
  for m in macos windows pull reset; do session "$v-a" "$m+audio" "$m" audio; done
  session "$v-a" off+audio off audio
}
[ "$mode" = san ] || run1 plain -O1
[ "$mode" = plain ] || run1 san -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -fsanitize-ignorelist="$FS/asan_ignore.txt"
echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)"
exit $rc
