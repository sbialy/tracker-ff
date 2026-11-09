#!/bin/bash
# run_sd_fault_test.sh ROOT [LABEL]   host fault injection of the SdDir list builders of ROOT/platforms/mk66/3rdparty/SD/SD.cpp
# ROOT = a tracker-ff checkout. Host only (macOS, clang, ASan+UBSan). Build in $OUT (default /tmp/sd_fault_<label>).
set -e
ROOT=$(cd "${1:?root}" && pwd); L=${2:-$(basename "$ROOT")}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/sd_fault_$L}; mkdir -p "$OUT"
SD=$ROOT/platforms/mk66/3rdparty/SD
{ echo '#include <cctype>'; echo '#include <new>'; echo '#include <cstdio>'; sed -n '/^bool SdDir::open(/,$p' "$SD/SD.cpp"; } > "$OUT/sd_dir_part.inc"
clang++ -std=gnu++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-sanitize=returns-nonnull-attribute -fno-omit-frame-pointer -fno-exceptions -w \
  -I"$OUT" -I"$SD" -I"$SD/fatfs/source" -o "$OUT/sd_fault_test" "$HERE/sd_fault_test.cpp"
ASAN_OPTIONS=detect_leaks=0:exitcode=77:handle_segv=1:abort_on_error=0 UBSAN_OPTIONS=halt_on_error=1:exitcode=77 "$OUT/sd_fault_test" "$L" 2>"$OUT/stderr.txt"
echo "(sanitizer reports of the crashing runs: $OUT/stderr.txt, $(grep -c 'ERROR: AddressSanitizer' "$OUT/stderr.txt") ASan, $(grep -c 'runtime error' "$OUT/stderr.txt") UBSan)"
