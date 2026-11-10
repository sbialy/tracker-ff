#!/usr/bin/env bash
# usb_desc_test.sh [REF_COMMIT]   Host proof of the boot-time USB audio switch (Config > Features > USB audio).
# Compiles the real teensy_core/usb_desc.c three ways and compares what a computer is served:
#   ref  usb_desc.c/.h of REF_COMMIT (default: the parent of the commit that added the switch), no USB_AUDIO flag = today's image
#   off  this tree with -DFEATURE_USB_AUDIO, boot flag 0      must equal ref byte for byte (descriptors, lengths, endpoints)
#   on   this tree with -DFEATURE_USB_AUDIO, boot flag 1      6 interfaces, total length = the descriptor, endpoint 8 isochronous IN
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../../../.." && pwd); tc=$root/platforms/mk66/3rdparty/teensy_core
out=${TMPDIR:-/tmp}/usb_desc_test; rm -rf "$out"; mkdir -p "$out/ref"
ref=${1:-$(git -C "$root" log --format=%h -1 --diff-filter=A -- platforms/mk66/usbaudio/usb_audio_ff.h)~1}
for f in usb_desc.c usb_desc.h; do git -C "$root" show "$ref:platforms/mk66/3rdparty/teensy_core/$f" > "$out/ref/$f" || exit 1; done
D=(-DUSB_MIDI -D__MK66FX1M0__ -DF_CPU=180000000 -DTEENSYDUINO=142 -DLAYOUT_US_ENGLISH -w)
# the ref tree's own usb_desc.{c,h} first, the rest of the Teensy headers from this tree
cp "$here/usb_desc_dump.c" "$out/ref/usb_desc_dump.c"; cc "${D[@]}" -I"$out/ref" -I"$tc" "$out/ref/usb_desc_dump.c" -o "$out/dump_ref" || exit 1
cc "${D[@]}" -DFEATURE_USB_AUDIO -I"$tc" "$here/usb_desc_dump.c" -o "$out/dump_feat" || exit 1
"$out/dump_ref" > "$out/ref.txt"; "$out/dump_feat" 0 > "$out/off.txt"; "$out/dump_feat" 1 > "$out/on.txt"
rc=0
ok() { echo "ok    $1"; }
bad() { echo "FAIL  $1"; rc=1; }
cmp -s "$out/ref.txt" "$out/off.txt" && ok "off: every descriptor, its served length and ENDPT1..8 equal the image without USB audio ($ref)" \
  || { bad "off differs from $ref"; diff "$out/ref.txt" "$out/off.txt" | head -20; }
grep -q "requests endpoint bound 7" "$out/off.txt" && ok "off: endpoint requests (GET_STATUS, SET/CLEAR_FEATURE) bound to 7 as before" || bad "off: endpoint bound"
cfg_on=$(grep "^desc 0200" "$out/on.txt"); cfg_ref=$(grep "^desc 0200" "$out/ref.txt")
read -r -a b <<< "${cfg_on#*:}"; tot=$((16#${b[3]}*256 + 16#${b[2]}))
n_on=$(echo "$cfg_on" | sed -E 's/.*len ([0-9]+):.*/\1/'); n_ref=$(echo "$cfg_ref" | sed -E 's/.*len ([0-9]+):.*/\1/')
[ "$tot" = "$n_on" ] && [ "${b[4]}" = 06 ] && ok "on: configuration $n_on B (stock $n_ref B + $((n_on - n_ref)) B audio), wTotalLength $tot, 6 interfaces" \
  || bad "on: wTotalLength $tot served $n_on interfaces ${b[4]}"
[ "${cfg_on:0:$(( ${#cfg_ref} ))}" != "" ] && pre_on=$(echo "${cfg_on#*:}" | cut -c1-$(( $(echo "${cfg_ref#*:}" | wc -c) - 1 )) | cut -d' ' -f7-) \
  && pre_ref=$(echo "${cfg_ref#*:}" | cut -d' ' -f7-) && [ "$pre_on" = "$pre_ref" ] && ok "on: interfaces 0..3 unchanged in front of the audio block" || bad "on: stock part changed"
grep -q " 8=14" "$out/on.txt" && ok "on: ENDPT8 = 0x14 (EPCTLDIS + EPTXEN, isochronous: no handshake)" || bad "on: endpoint 8 $(grep endpoints "$out/on.txt")"
# walk the descriptors of the on configuration: every bLength lands exactly on the end, the audio interfaces are 4 and 5
python3 -I - "${b[@]}" <<'PY' || rc=1
import sys
b=[int(x,16) for x in sys.argv[1:]]; i=0; ifs=set(); eps=[]
while i < len(b):
    L,T=b[i],b[i+1]
    if L < 2: print("FAIL  on: zero length descriptor at", i); sys.exit(1)
    if T == 4: ifs.add(b[i+2])
    if T == 5: eps.append((b[i+2], b[i+3], b[i+4] | b[i+5] << 8))
    i += L
ok = i == len(b) and ifs == {0,1,2,3,4,5} and (0x88, 5, 180) in eps
print(("ok    " if ok else "FAIL  ") + "on: descriptors chain to %d B, interfaces %s, audio endpoint %s" % (i, sorted(ifs), [e for e in eps if e[0] == 0x88]))
sys.exit(0 if ok else 1)
PY
echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)"
exit $rc
