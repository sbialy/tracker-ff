#!/usr/bin/env bash
# s3_fat_roundtrip.sh [plain|san|all]   Simulator row S3 of ref/usb-mass-storage-feasibility.md on the Mac: the real FileSystemImpl, SD.cpp and
# FatFs of the original (USB_MSC=1) on a FAT32 image that macOS formats, mounts and writes with its own msdosfs while the firmware has lent
# the card (fs_host_test.cpp). Ends with fsck_msdos -n of the image. Needs hdiutil, diskutil, newfs_msdos (macOS), no device.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED".
set -uo pipefail
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1}
here=$(cd "$(dirname "$0")" && pwd); mk=$(cd "$here/../../.." && pwd); root=$(cd "$mk/../.." && pwd)
mode=${1:-all}
out=${TMPDIR:-/tmp}/usbmsc_s3.$$; mkdir -p "$out"; trap 'hdiutil detach "$dev" > /dev/null 2>&1; rm -rf "$out"' EXIT
dev=""
SD=$mk/3rdparty/SD
INC=(-I"$here/shim" -I"$SD" -I"$SD/fatfs/source" -I"$SD/fatfs/source/fsl_sd_disk" -I"$mk/source" -I"$root/core/include" -I"$mk/usbmsc")
DEF=(-DFEATURE_USB_MSC -DENABLE_LOG)
rc=0
build() {  # name flags...
  local n=$1; shift
  local o=$out/obj-$n; mkdir -p "$o"
  for c in ff.c ffunicode.c diskio.c; do cc -std=gnu11 -w -g "$@" "${INC[@]}" "${DEF[@]}" -c "$SD/fatfs/source/$c" -o "$o/${c%.c}.o" || return 1; done
  cc -std=gnu11 -w -g "$@" "${INC[@]}" "${DEF[@]}" -c "$SD/fatfs/source/fsl_sd_disk/fsl_sd_disk.c" -o "$o/fsl_sd_disk.o" || return 1
  for c in "$SD/SD.cpp" "$SD/SdFile.cpp" "$mk/source/FileSystemImpl.cpp" "$mk/source/util_string.cpp" "$here/fake_sd.cpp" "$here/fs_host_test.cpp"; do
    c++ -std=gnu++17 -g -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-unused-variable "$@" "${INC[@]}" "${DEF[@]}" \
      -include cstdarg -include cassert -c "$c" -o "$o/$(basename "${c%.cpp}").o" 2> "$o/$(basename "$c").err" || { cat "$o/$(basename "$c").err" | head -30; return 1; }
  done
  c++ "$@" "$o"/*.o -o "$out/fs_host_test-$n"
}
# the computer's files
src=$out/src; mkdir -p "$src/Copied"
head -c 3000 /dev/urandom > "$src/project_host.mt"
head -c 409600 /dev/urandom > "$src/stream_b_new.wav"
for i in 1 2 3 4 5 6 7 8; do head -c $((i * 37000 + 123)) /dev/urandom > "$src/Copied/sample_$i.wav"; done
head -c 2500000 /dev/urandom > "$src/Copied/long_loop.wav"
# what macOS does while it owns the card (run by fs_host_test between lendToHost and remount)
cat > "$out/host.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail
dev=\$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$out/card.img" | awk '{print \$1; exit}')
mkdir -p "$out/mnt"; diskutil mount -mountPoint "$out/mnt" "\$dev" > /dev/null
s=\$(stat -f %z "$out/mnt/Workspace/pending.txt"); [ "\$s" = 1000 ] || { echo "host: pending.txt is \$s B on the Mac, want 1000"; exit 1; }
rm "$out/mnt/Samples/stream_a.wav"
cp "$src/stream_b_new.wav" "$out/mnt/Samples/stream_b.wav"
cp "$src/project_host.mt" "$out/mnt/Workspace/project.mt"
cp -R "$src/Copied" "$out/mnt/"
echo "host: macOS mounted the card, saw pending.txt 1000 B, deleted stream A, replaced stream B and the project, copied \$(ls "$src/Copied" | wc -l | tr -d ' ') files"
diskutil unmount "$out/mnt" > /dev/null; hdiutil detach "\$dev" > /dev/null
EOF
chmod +x "$out/host.sh"
fresh_image() {  # a 128 MB card formatted by macOS
  rm -f "$out/card.img"; mkfile -n 128m "$out/card.img" || return 1
  dev=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$out/card.img" | awk '{print $1; exit}') || return 1
  newfs_msdos -F 32 -v TRACKER "${dev/disk/rdisk}" > "$out/newfs.log" 2>&1 || { cat "$out/newfs.log"; return 1; }
  hdiutil detach "$dev" > /dev/null; dev=""
}
run1() {  # variant flags...
  local v=$1; shift
  if ! build "$v" "$@"; then echo "FAIL  build ($v)"; rc=1; return; fi
  if ! fresh_image; then echo "FAIL  image ($v)"; rc=1; return; fi
  if "$out/fs_host_test-$v" "$out/card.img" "$src" "$out/host.sh" > "$out/run-$v.log" 2>&1; then
    echo "ok    fs_host_test ($v): $(grep -E '^host:|^lent:|^back:|checks' "$out/run-$v.log" | sed 's/^/  /' | tr '\n' ' ')"
  else
    echo "FAIL  fs_host_test ($v), log kept in ${KEEP:-/tmp}/usbmsc_s3-$v.log"; cp "$out/run-$v.log" "${KEEP:-/tmp}/usbmsc_s3-$v.log"
    grep -E "^FAIL|ERROR: AddressSanitizer|runtime error|SUMMARY|^    #[0-9] " "$out/run-$v.log" | head -20; rc=1
  fi
  local f; f=$(fsck_msdos -n "$out/card.img" 2>&1)
  if echo "$f" | grep -qiE "fix|corrupt|lost|cross|error|bad"; then echo "FAIL  fsck_msdos -n ($v):"; echo "$f" | tail -8; rc=1
  else echo "ok    fsck_msdos -n ($v): $(echo "$f" | tail -1)"; fi
}
[ "$mode" = san ] || run1 plain -O1
[ "$mode" = plain ] || run1 san -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -fsanitize-ignorelist="$here/asan_ignore.txt"
echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)"
exit $rc
