#!/usr/bin/env bash
# usb_desc_test.sh [REF_COMMIT]   Host proof of the boot-time USB drive switch (USB_MSC=1, Config > Features > USB drive).
# Compiles the real teensy_core/usb_desc.c of REF_COMMIT (default 8240339, the R130 line this branch starts from) and of this
# tree, in the four builds (no flag, USB_AUDIO only, USB_MSC only, both), and compares what a computer is served:
#   - USB_MSC unset: this tree equals REF byte for byte (descriptors, served lengths, ENDPT1..10, endpoint request bound),
#     plain and USB_AUDIO with the audio flag 0 and 1
#   - USB_MSC=1 with the drive flag 0: equals REF of the same USB_AUDIO setting (audio flag 0 and 1)
#   - drive flag 1: the configuration is the off one plus one mass storage interface (class 08/06/50, bulk IN 0x89 and bulk OUT
#     0x0A, 64 B) numbered right after the last interface served (4, or 6 with audio on), the descriptor chain walks to
#     wTotalLength, endpoint addresses are unique and their directions match ENDPT9 = 0x15 (TX) and ENDPT10 = 0x19 (RX)
# Checks adapted from OpenTracker tests/usb_desc_test.cpp (Apache-2.0, see usbmsc/msc_bot.h).
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../../../.." && pwd); tc=$root/platforms/mk66/3rdparty/teensy_core
ref=${1:-8240339}
out=${TMPDIR:-/tmp}/usb_msc_desc_test.$$; rm -rf "$out"; mkdir -p "$out/ref"; trap 'rm -rf "$out"' EXIT
for f in usb_desc.c usb_desc.h; do git -C "$root" show "$ref:platforms/mk66/3rdparty/teensy_core/$f" > "$out/ref/$f" || exit 1; done
D=(-DUSB_MIDI -D__MK66FX1M0__ -DF_CPU=180000000 -DTEENSYDUINO=142 -DLAYOUT_US_ENGLISH -w)
cp "$here/usb_desc_dump.c" "$out/ref/usb_desc_dump.c"
rc=0
ok() { echo "ok    $1"; }
bad() { echo "FAIL  $1"; rc=1; }
build() {  # name tree(ref|new) flags...
  local n=$1 t=$2; shift 2
  if [ "$t" = ref ]; then cc "${D[@]}" "$@" -I"$out/ref" -I"$tc" "$out/ref/usb_desc_dump.c" -o "$out/$n" || exit 1
  else cc "${D[@]}" "$@" -I"$tc" "$here/usb_desc_dump.c" -o "$out/$n" || exit 1; fi
}
build ref_plain ref; build ref_uac ref -DFEATURE_USB_AUDIO
build new_plain new; build new_uac new -DFEATURE_USB_AUDIO
build new_msc new -DFEATURE_USB_MSC; build new_both new -DFEATURE_USB_AUDIO -DFEATURE_USB_MSC
same() {  # label refbin refargs newbin newargs
  "$out/$2" $3 > "$out/a.txt"; "$out/$4" $5 > "$out/b.txt"
  cmp -s "$out/a.txt" "$out/b.txt" && ok "$1" || { bad "$1"; diff "$out/a.txt" "$out/b.txt" | head -10; }
}
same "USB_MSC unset, no USB_AUDIO: equal to $ref" ref_plain "" new_plain ""
same "USB_MSC unset, USB_AUDIO audio off: equal to $ref" ref_uac "0" new_uac "0"
same "USB_MSC unset, USB_AUDIO audio on: equal to $ref" ref_uac "1" new_uac "1"
same "USB_MSC=1 drive off, no USB_AUDIO: equal to $ref without the flag" ref_plain "" new_msc "0 0"
same "USB_MSC=1 drive off, USB_AUDIO audio off: equal to $ref" ref_uac "0" new_both "0 0"
same "USB_MSC=1 drive off, USB_AUDIO audio on: equal to $ref" ref_uac "1" new_both "1 0"
# USB audio 8+3 mono (audio flag 2, on 1.4.0 only, so no REF): the drive off serves what the tree without USB_MSC serves
same "USB_MSC=1 drive off, USB_AUDIO 8+3 mono: equal to the tree without USB_MSC" new_uac "2" new_both "2 0"
# negative control: the comparison must see the drive (flag 1 differs from REF)
"$out/ref_uac" 1 > "$out/a.txt"; "$out/new_both" 1 1 > "$out/b.txt"
cmp -s "$out/a.txt" "$out/b.txt" && bad "negative control: drive on compares equal to $ref" || ok "negative control: drive on differs from $ref"
# drive on: structure against the matching off configuration
check_on() {  # label offbin offargs onbin onargs want_iface
  "$out/$2" $3 > "$out/off.txt"; "$out/$4" $5 > "$out/on.txt"
  python3 -I - "$1" "$6" "$out/off.txt" "$out/on.txt" <<'PY' || rc=1
import sys
label, want_if, offp, onp = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
def load(p):
    descs, eps, bound, iface = {}, {}, None, None
    for line in open(p):
        if line.startswith("desc "):
            head, data = line.split(":", 1)
            k = head.split()[1] + head.split()[2]
            descs[k] = [int(x, 16) for x in data.split()]
        elif line.startswith("endpoints:"):
            for kv in line.split(":", 1)[1].split():
                a, b = kv.split("="); eps[int(a)] = int(b, 16)
        elif line.startswith("requests endpoint bound"):
            bound = int(line.split()[-1])
        elif line.startswith("msc interface"):
            iface = int(line.split()[-1])
    return descs, eps, bound, iface
off, on = load(offp), load(onp)
errs = []
def chk(c, m):
    if not c: errs.append(m)
c0, c1 = off[0]["02000000"], on[0]["02000000"]
chk(len(c1) == len(c0) + 23, "configuration %d B, want %d + 23" % (len(c1), len(c0)))
chk((c1[2] | c1[3] << 8) == len(c1), "wTotalLength %d, served %d" % (c1[2] | c1[3] << 8, len(c1)))
chk(c1[4] == c0[4] + 1, "bNumInterfaces %d, want %d" % (c1[4], c0[4] + 1))
chk(c1[5:9] == c0[5:9], "configuration value / attributes / bMaxPower changed")
chk(c1[9:len(c0)] == c0[9:], "the interfaces in front of the drive changed")
# every other descriptor (device, strings, HID reports) unchanged
for k in off[0]:
    if k != "02000000": chk(on[0].get(k) == off[0][k], "descriptor %s changed" % k)
chk(set(on[0]) == set(off[0]), "descriptor list changed")
# walk the chain (OpenTracker usb_desc_test parse())
i, ifs, eps, cur, iads = 0, [], [], None, 0
while i < len(c1):
    L, T = c1[i], c1[i + 1]
    if L < 2 or i + L > len(c1): errs.append("bad descriptor at %d" % i); break
    if T == 4: ifs.append(tuple(c1[i + 2:i + 9])); cur = c1[i + 2]
    if T == 5: eps.append((c1[i + 2], c1[i + 3], c1[i + 4] | c1[i + 5] << 8, cur))
    if T == 11: iads += 1
    i += L
chk(i == len(c1), "chain ends at %d of %d" % (i, len(c1)))
m = ifs[-1]
chk(m[0] == want_if and on[3] == want_if, "drive interface %d (served %s), want %d" % (m[0], on[3], want_if))
chk(m[1:6] == (0, 2, 0x08, 0x06, 0x50), "drive interface fields %s" % (m[1:6],))
nums = sorted(set(f[0] for f in ifs))
chk(nums == list(range(c1[4])), "interface numbers %s for bNumInterfaces %d" % (nums, c1[4]))
addrs = [e[0] for e in eps]
chk(len(addrs) == len(set(addrs)), "endpoint addresses not unique %s" % addrs)
msc = [e for e in eps if e[3] == want_if]
chk(sorted((e[0], e[1], e[2]) for e in msc) == [(0x0A, 2, 64), (0x89, 2, 64)], "drive endpoints %s" % msc)
E = on[1]
for a, attr, size, f in eps:
    n = a & 0x0F; c = E.get(n, 0)
    chk(1 <= n <= 10, "endpoint number %d" % n)
    chk((c & 0x04) if a & 0x80 else (c & 0x08), "endpoint 0x%02x direction vs ENDPT%d=0x%02x" % (a, n, c))
chk(E[9] == 0x15 and E[10] == 0x19, "ENDPT9 0x%02x ENDPT10 0x%02x" % (E[9], E[10]))
chk(all(E[k] == off[1][k] for k in range(1, 9)), "ENDPT1..8 changed")
chk(on[2] == 10, "endpoint request bound %s, want 10" % on[2])
print(("ok    " if not errs else "FAIL  ") + label + (": %d B, interface %d, %d interfaces, IADs %d" % (len(c1), m[0], c1[4], iads) if not errs else ": " + "; ".join(errs)))
sys.exit(1 if errs else 0)
PY
}
check_on "USB_MSC=1 drive on, no USB_AUDIO" new_msc "0 0" new_msc "0 1" 4
check_on "USB_MSC=1 drive on, USB_AUDIO audio off" new_both "0 0" new_both "0 1" 4
check_on "USB_MSC=1 drive on, USB_AUDIO audio on" new_both "1 0" new_both "1 1" 6
check_on "USB_MSC=1 drive on, USB_AUDIO 8+3 mono" new_both "2 0" new_both "2 1" 6
echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)"
exit $rc
