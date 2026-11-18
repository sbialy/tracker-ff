#!/usr/bin/env bash
# s4_bot_replay.sh [plain|san|all]   Simulator row S4 of ref/usb-mass-storage-feasibility.md on the Mac: macOS (msdosfs) changes a copy of a
# card directly, the blocks it changed are sent again as SCSI command sequences (a macOS mount and copy with eject, a Windows one, a macOS one
# with the cable pulled in a WRITE) through the Bot (msc_bot.cpp) into the original's real block path (FileSystemImpl, SD.cpp, FatFs on the
# image, s4_bot_replay.cpp). The result must equal macOS's image byte for byte and pass fsck_msdos -n. No device.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED".
set -uo pipefail
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1}
here=$(cd "$(dirname "$0")" && pwd); mk=$(cd "$here/../../.." && pwd); root=$(cd "$mk/../.." && pwd)
mode=${1:-all}
out=${TMPDIR:-/tmp}/usbmsc_s4.$$; mkdir -p "$out"; dev=""
trap '[ -n "$dev" ] && hdiutil detach "$dev" > /dev/null 2>&1; rm -rf "$out"' EXIT
SD=$mk/3rdparty/SD
INC=(-I"$here/shim" -I"$SD" -I"$SD/fatfs/source" -I"$SD/fatfs/source/fsl_sd_disk" -I"$mk/source" -I"$root/core/include" -I"$mk/usbmsc")
DEF=(-DFEATURE_USB_MSC)
rc=0
build() {  # name flags...
  local n=$1; shift
  local o=$out/obj-$n; mkdir -p "$o"
  for c in ff.c ffunicode.c diskio.c; do cc -std=gnu11 -w -g "$@" "${INC[@]}" "${DEF[@]}" -c "$SD/fatfs/source/$c" -o "$o/${c%.c}.o" || return 1; done
  cc -std=gnu11 -w -g "$@" "${INC[@]}" "${DEF[@]}" -c "$SD/fatfs/source/fsl_sd_disk/fsl_sd_disk.c" -o "$o/fsl_sd_disk.o" || return 1
  for c in "$SD/SD.cpp" "$SD/SdFile.cpp" "$mk/source/FileSystemImpl.cpp" "$mk/source/util_string.cpp" "$mk/usbmsc/msc_bot.cpp" "$here/fake_sd.cpp" \
           "$here/s4_bot_replay.cpp"; do
    c++ -std=gnu++17 -g -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-unused-variable "$@" "${INC[@]}" "${DEF[@]}" \
      -include cstdarg -include cassert -c "$c" -o "$o/$(basename "${c%.cpp}").o" 2> "$o/err" || { head -30 "$o/err"; return 1; }
  done
  c++ "$@" "$o"/*.o -o "$out/s4-$n"
}
attach() { dev=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$1" | awk '{print $1; exit}'); }
detach() { hdiutil detach "$dev" > /dev/null; dev=""; }
# the card before: formatted and filled by macOS
mkfile -n 128m "$out/before.img" && attach "$out/before.img" && newfs_msdos -F 32 -v TRACKER "${dev/disk/rdisk}" > /dev/null 2>&1 || { echo "RESULT: RED (image)"; exit 1; }
mkdir -p "$out/mnt"; diskutil mount -mountPoint "$out/mnt" "$dev" > /dev/null
mkdir -p "$out/mnt/Workspace" "$out/mnt/Samples"; head -c 4000 /dev/urandom > "$out/mnt/Workspace/project.mt"
for i in 1 2 3; do head -c $((i * 90000)) /dev/urandom > "$out/mnt/Samples/old_$i.wav"; done
diskutil unmount "$out/mnt" > /dev/null; detach
# the session, on a copy, by macOS itself: copy a folder, delete a sample, replace the project, eject
cp "$out/before.img" "$out/ref.img"; attach "$out/ref.img"; diskutil mount -mountPoint "$out/mnt" "$dev" > /dev/null
mkdir -p "$out/mnt/Copied"; for i in 1 2 3 4 5 6; do head -c $((i * 51234)) /dev/urandom > "$out/mnt/Copied/take_$i.wav"; done
head -c 1800000 /dev/urandom > "$out/mnt/Copied/long.wav"
rm "$out/mnt/Samples/old_2.wav"; head -c 4100 /dev/urandom > "$out/mnt/Workspace/project.mt"
diskutil unmount "$out/mnt" > /dev/null; detach
fsck_msdos -n "$out/ref.img" > /dev/null 2>&1 || { echo "RESULT: RED (macOS's own image fails fsck)"; exit 1; }
run1() {  # variant flags...
  local v=$1; shift
  if ! build "$v" "$@"; then echo "FAIL  build ($v)"; rc=1; return; fi
  for m in macos windows pull; do
    cp "$out/before.img" "$out/work.img"
    if "$out/s4-$v" "$out/work.img" "$out/ref.img" $m > "$out/$v-$m.log" 2>&1; then
      echo "ok    $m ($v): $(grep -E "^$m:|checks" "$out/$v-$m.log" | tr '\n' ' ')"
    else
      echo "FAIL  $m ($v), log kept in ${KEEP:-/tmp}/usbmsc_s4-$v-$m.log"; cp "$out/$v-$m.log" "${KEEP:-/tmp}/usbmsc_s4-$v-$m.log"
      grep -E "^FAIL|ERROR: AddressSanitizer|runtime error|SUMMARY|^    #[0-9] " "$out/$v-$m.log" | head -12; rc=1
    fi
    local f; f=$(fsck_msdos -n "$out/work.img" 2>&1)
    echo "$f" | grep -qiE "fix|corrupt|lost|cross|error|bad" && { echo "FAIL  fsck_msdos -n after $m ($v)"; echo "$f" | tail -5; rc=1; }
  done
  # negative control: the same macOS session with its last WRITE(10) left out must be caught by the image compare
  cp "$out/before.img" "$out/work.img"
  if "$out/s4-$v" "$out/work.img" "$out/ref.img" drop > "$out/$v-drop.log" 2>&1; then echo "FAIL  negative control ($v): a dropped write passed"; rc=1
  elif grep -q "blocks differ from macOS's image" "$out/$v-drop.log"; then echo "ok    negative control ($v): $(grep -o '[0-9]* blocks differ from macOS.s image' "$out/$v-drop.log")"
  else echo "FAIL  negative control ($v): failed for another reason"; grep "^FAIL" "$out/$v-drop.log" | head -3; rc=1; fi
}
[ "$mode" = san ] || run1 plain -O1
[ "$mode" = plain ] || run1 san -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -fsanitize-ignorelist="$here/asan_ignore.txt"
echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)"
exit $rc
