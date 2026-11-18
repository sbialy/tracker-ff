#!/bin/bash
# Digit identity of the float printf: newlib-nano's own (the old link, -u _printf_float) against ff::fm::formatFloat behind the
# _printf_float hook (the new link, platforms/mk66/source/{float_format,printf_float}.cpp). Both are built with the release compiler
# flags, run by the toolchain's own snprintf / sprintf under a Cortex-M4F emulator (Unicorn, NOT the device) over every float format
# of formats.txt and a value sweep, and diffed byte for byte (the output text and the returned length).
#   synth-open/tools/printf_diff/run_printf_diff.sh
# Needs the pinned toolchain in agt/ and a python with unicorn:  sh synth-open/tools/pyenv.sh (once, the default)
#   (PYTHON=$VENV/bin/python, PLUG_PARAMS_DIR=dir with the module .json files to check their parameter display too, TOOLCHAIN=.../arm-none-eabi- as for make original). Exit 0 = 0 diffs, 1 = diffs, 2 = a setup failure.
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
HERE=$ROOT/synth-open/tools/printf_diff
PY=${PYTHON:-${PY:-$(sh "$ROOT/synth-open/tools/pyenv.sh" python)}} || exit 2
TC=${TOOLCHAIN:-$ROOT/agt/12.3.rel1/bin/arm-none-eabi-}
OUT=${OUT_DIR:-${TMPDIR:-/tmp}/printf_diff_$$}
mkdir -p "$OUT"
"$PY" -I "$HERE/run_printf_diff.py" --check-sites "$ROOT" "$HERE/formats.txt"
FL="-std=gnu++17 -Os -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fno-rtti -fno-exceptions -fno-common -DFW_TRIM"
SRC=$ROOT/platforms/mk66/source
"$TC"c++ $FL -c "$HERE/printf_diff.cpp" -o "$OUT/main.o"
"$TC"c++ $FL -I"$SRC" -c "$SRC/float_format.cpp" -o "$OUT/float_format.o"
"$TC"c++ $FL -I"$SRC" -c "$SRC/printf_float.cpp" -o "$OUT/printf_float.o"
LD="-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -specs=nano.specs -nostartfiles -Wl,--gc-sections -Wl,-u,run -Wl,-T,$HERE/link.ld -Wl,--no-warn-rwx-segments"
"$TC"c++ $LD -Wl,-u,_printf_float "$OUT/main.o" -Wl,-Map="$OUT/old.map" -o "$OUT/old.elf"
"$TC"c++ $LD "$OUT/main.o" "$OUT/float_format.o" "$OUT/printf_float.o" -Wl,-Map="$OUT/new.map" -o "$OUT/new.elf"
# the new link must not carry newlib's dtoa, the old one must
"$TC"nm "$OUT/old.elf" | grep -q ' _dtoa_r$' || { echo "printf_diff: the old link has no _dtoa_r"; exit 2; }
if "$TC"nm "$OUT/new.elf" | grep -q ' _dtoa_r$'; then echo "printf_diff: newlib's _dtoa_r got linked into the new link"; exit 2; fi
"$PY" -I "$HERE/run_printf_diff.py" "$TC" "$OUT/old.elf" "$OUT/new.elf" "$HERE/formats.txt" "$OUT"
