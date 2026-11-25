#!/usr/bin/env bash
# Build extras.plg, the bundle of the release card modules, for one release image: MOD import, IT import and IT export
# (core/FileManager/fileManagerImportMod.cpp, fileManagerImportIt.cpp, fileManagerExportToMod.cpp, unchanged), the custom theme JSON parser,
# the factory self test and the two Capture tenants, back to back in the release region (extras.ld, ext_abi.h EXT_SUB_*). pack_ext.py writes
# the sub-slot table from the linked sizes.
#   build_extras.sh IMAGE_DIR OBJ_DIR OUTDIR
# IMAGE_DIR holds the image build: PolyendTracker.axf (the FINAL one: make original sizes the tail pad and relinks, which changes the image id)
# and the image's .ptf. OBJ_DIR is the object folder core/makefile fills with the module's objects (MODIT_MODULE=1, <core build dir>/obj/modit):
# the three sources and modit_dispatch.cpp, modit_entry.cpp compiled with the image's own flags and the forced include modit_names.h (it renames
# the five entry members of the sources so the image's forwarders keep the stock names). They are linked at the image's release region (s_extRel, never the console's s_ext)
# against the image's symbols (ld --just-symbols) and packed by pack_ext.py --region rel into OUTDIR/extras.plg, bound to that image. A symbol the image lacks
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
for n in fileManagerImportMod fileManagerImportIt fileManagerExportToMod modit_dispatch modit_entry ThemeManagerJson mtTest mtSequencer_captureMod mtSequencer_captureFile; do
  [ -f "$objdir/$n.o" ] || { echo "no $objdir/$n.o: build the image with MODIT_MODULE=1 (make original-release)"; exit 1; }
  objs+=("$objdir/$n.o")
done
addr=$(${TC}nm "$axf" | awk '/_ZN12_GLOBAL__N_18s_extRelE/{a=$1} END{print a}')
[ -n "$addr" ] || { echo "the image has no release region (s_extRel): not built with FEATURE_EXT_HOOKS, or older than the multi-ext loader"; exit 1; }
# the region size of ext_abi.h (EXT_REL_BYTES, "(N * 1024u)" or "Nu"): the linker's last check, pack_ext.py and the loader check it again
region=$(python3 -I -c 'import re,sys; m=re.search(r"#define EXT_REL_BYTES \(?([0-9]+)u?(?: \* ([0-9]+)u)?\)?", open(sys.argv[1]).read()); print(int(m.group(1)) * int(m.group(2) or 1))' "$fw/core/include/ext_abi.h")
echo "module base 0x$addr, region $region B"
quiet=(); ${TC}ld --help | grep -q -e --no-warn-rwx-segments && quiet=(--no-warn-rwx-segments)
rc=0
${TC}ld -o "$out/extras.elf" -T "$here/extras.ld" --defsym=MODULE_BASE=0x$addr --defsym=REGION_BYTES=$region --just-symbols="$axf" \
   --gc-sections -e ext_entry -Map "$out/extras.map" ${quiet[@]+"${quiet[@]}"} ${SIZE_ONLY:+--unresolved-symbols=ignore-all} "${objs[@]}" \
   2>"$out/link.err" || rc=$?
und=$(grep -o "undefined reference to \`[^']*'" "$out/link.err" | sort -u || true)
[ -z "$und" ] || echo "$und"
[ "$rc" -eq 0 ] || { cat "$out/link.err"; exit 1; }
${TC}size -A "$out/extras.elf" | awk 'NF==3 && $2>0'
[ -n "${SIZE_ONLY:-}" ] && exit 0
TOOLCHAIN=$TC python3 -I "$fw/synth-open/plug/pack_ext.py" --region rel --elf "$out/extras.elf" --axf "$axf" --ptf "$ptf" --out "$out/extras.plg" \
   --name "extras" --author "tracker-ff" --licence "see the release" --version "1.4.1"
ls -l "$out/extras.plg"
