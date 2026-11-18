#!/usr/bin/env bash
# Build modit.plg, the MOD import, IT import and IT export card module (core/FileManager/fileManagerImportMod.cpp, fileManagerImportIt.cpp,
# fileManagerExportToMod.cpp, unchanged), for one release image.
#   build_modit.sh IMAGE_DIR OBJ_DIR OUTDIR
# IMAGE_DIR holds the image build: PolyendTracker.axf (the FINAL one: make original sizes the tail pad and relinks, which changes the image id)
# and the image's .ptf. OBJ_DIR is the object folder core/makefile fills with the module's objects (MODIT_MODULE=1, <core build dir>/obj/modit):
# the three sources and modit_dispatch.cpp, modit_entry.cpp compiled with the image's own flags and the forced include modit_names.h (it renames
# the five entry members of the sources so the image's forwarders keep the stock names). They are linked at the image's SDRAM slot (s_ext)
# against the image's symbols (ld --just-symbols) and packed by pack_ext.py into OUTDIR/modit.plg, bound to that image. A symbol the image lacks
# is a link error (synth-open/ext_module.cpp keepForModule keeps what the module needs). SIZE_ONLY=1 links with unresolved symbols ignored and
# packs nothing. PTF=<file> names the image .ptf when IMAGE_DIR holds several. Run it after every build of the image: the file is only good for the image it was packed with.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); fw=$(cd "$here/../.." && pwd)
img=$(cd "$1" && pwd); objdir=$(cd "$2" && pwd); mkdir -p "$3"; out=$(cd "$3" && pwd)
TC=${TOOLCHAIN:-/Users/sbialy/prj/poly/agt/12.3.rel1/bin/arm-none-eabi-}
axf=$img/PolyendTracker.axf
ptfs=("$img"/PolyendTracker_*.ptf); ptf=${PTF:-${ptfs[0]}}   # PTF=: the one .ptf of this build when the folder holds more
[ -f "$axf" ] && [ -f "$ptf" ] || { echo "IMAGE_DIR needs PolyendTracker.axf and a .ptf"; exit 1; }
objs=()
for n in fileManagerImportMod fileManagerImportIt fileManagerExportToMod modit_dispatch modit_entry; do
  [ -f "$objdir/$n.o" ] || { echo "no $objdir/$n.o: build the image with MODIT_MODULE=1 (make original-release)"; exit 1; }
  objs+=("$objdir/$n.o")
done
addr=$(${TC}nm "$axf" | awk '/_ZN12_GLOBAL__N_15s_extE/{a=$1} END{print a}')
[ -n "$addr" ] || { echo "the image has no extension slot (s_ext): not built with FEATURE_EXT_HOOKS"; exit 1; }
echo "module base 0x$addr"
quiet=(); ${TC}ld --help | grep -q -e --no-warn-rwx-segments && quiet=(--no-warn-rwx-segments)
rc=0
${TC}ld -o "$out/modit.elf" -T "$here/modit.ld" --defsym=MODULE_BASE=0x$addr --just-symbols="$axf" \
   --gc-sections -e ext_entry -Map "$out/modit.map" ${quiet[@]+"${quiet[@]}"} ${SIZE_ONLY:+--unresolved-symbols=ignore-all} "${objs[@]}" \
   2>"$out/link.err" || rc=$?
und=$(grep -o "undefined reference to \`[^']*'" "$out/link.err" | sort -u || true)
[ -z "$und" ] || echo "$und"
[ "$rc" -eq 0 ] || { cat "$out/link.err"; exit 1; }
${TC}size -A "$out/modit.elf" | awk 'NF==3 && $2>0'
[ -n "${SIZE_ONLY:-}" ] && exit 0
TOOLCHAIN=$TC python3 -I "$fw/synth-open/plug/pack_ext.py" --elf "$out/modit.elf" --axf "$axf" --ptf "$ptf" --out "$out/modit.plg" \
   --name "MOD/IT" --author "tracker-ff" --licence "see the release" --version "1.4.0"
ls -l "$out/modit.plg"
