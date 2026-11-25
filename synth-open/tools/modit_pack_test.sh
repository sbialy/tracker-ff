#!/usr/bin/env bash
# Host test of the extras.plg (MOD/IT) module binding (synth-open/plug/check_ext.py, release_gate.sh EXTRAS_PLG, package_release.sh --extras):
# a module is good for the exact image it was packed with. Small synthetic images and modules, no toolchain:
#   check_ext.py       exit 0 for the image the module was packed for, 1 for an image that differs in one byte, for a damaged module (file CRC), a module
#                      of another kind and a slot address that is not the image's
#   release_gate.sh    with MODIT_PLG: FAIL (exit 1, before the simulator) for a module of another image, passes the check for the right one
#   package_release.sh a package with plugins and no module is refused (MODIT=skip says so and goes on), a module of another image is refused
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
bad=0
ok()   { echo "ok   $*"; }
fail() { echo "FAIL $*"; bad=$((bad+1)); }

# mk_ptf <out.ptf> <end> <fill byte>
mk_ptf() {
python3 - "$1" "$2" "$3" <<'PY'
import sys
out, end, fill = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
mem = bytearray([fill]) * end
mem[0:4] = (0x20030000).to_bytes(4, "little")
mem[4:8] = (0x1D1).to_bytes(4, "little")
mem[0x400:0x410] = bytes([0xFF] * 12 + [0xDE, 0xF9, 0xFF, 0xFF])
def rec(addr, rtype, data):
    raw = bytes([len(data), addr >> 8, addr & 0xFF, rtype]) + bytes(data)
    return ":" + (raw + bytes([(-sum(raw)) & 0xFF])).hex().upper()
lines = []
for base in range(0, end, 0x10000):
    lines.append(rec(0, 2, [(base >> 4) >> 8, (base >> 4) & 0xFF]))
    for a in range(base, min(base + 0x10000, end), 16):
        lines.append(rec(a - base, 0, mem[a:min(a + 16, end)]))
lines.append(rec(0, 1, []))
open(out, "w").write("\n".join(lines) + "\n")
PY
}
# mk_plg <out.plg> <image.ptf> [kind] [run]: a module file as pack_ext.py writes it, bound to the image
mk_plg() {   # OUT PTF [KIND] [RUN] [notable: no sub-slot table]
python3 - "$1" "$2" "${3:-6}" "${4:-0x70d80000}" ${5:+"$5"} <<'PY'
import struct, sys, zlib
out, ptf, kind, run = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4], 0)
mem, upper, top = {}, 0, 0
for line in open(ptf):
    raw = bytes.fromhex(line.strip()[1:])
    n, addr, t, d = raw[0], (raw[1] << 8) | raw[2], raw[3], raw[4:4 + raw[0]]
    if t == 0:
        for i in range(n): mem[upper + addr + i] = d[i]
        top = max(top, upper + addr + n)
    elif t == 2: upper = ((d[0] << 8) | d[1]) << 4
flash = bytearray(b"\xff" * top)
for a, b in mem.items(): flash[a] = b
image_id = zlib.crc32(bytes(flash)) & 0xFFFFFFFF
manifest = struct.pack("<I16s32s16sHBBIIIII4x", 96, b"MOD/IT", b"t", b"x", 0, 0, 0, 0, 0, 0, 0, 0)
if kind == 6 and len(sys.argv) < 6:   # the sub-slot table of extras.plg (ext_abi.h ext_rel_table_t): MOD/IT 1 KB at 0, four empty tenants
    manifest += struct.pack("<IHHII", 0x54524558, 2, 5, 40 * 1024, 1024) + struct.pack("<II", 0, 1024) + struct.pack("<II", 1024, 0) * 4
image = bytes(range(64)) * 4
payload = manifest + image
hdr = struct.pack("<IHBBBBH" + "I" * 13, 0x31474C50, 64 + len(manifest), 1, 1, kind, 1, 0, 1, 1 << 16, 1, len(image), 16, 0, 0, len(image) + 16, 0,
                  zlib.crc32(payload) & 0xFFFFFFFF, 128, image_id, run)
open(out, "wb").write(hdr + payload)
PY
}
chk() { python3 -I "$root/synth-open/plug/check_ext.py" "$@" > "$tmp/out.txt" 2>&1; }

mk_ptf "$tmp/a.ptf" 0x20800 0x5A      # the image
mk_ptf "$tmp/b.ptf" 0x20800 0x5B      # another build: one fill byte differs everywhere
mk_plg "$tmp/a.plg" "$tmp/a.ptf"
chk "$tmp/a.plg" "$tmp/a.ptf" --slot 0x70d80000 && ok "check_ext: the module of this image passes" || fail "check_ext good: $(cat "$tmp/out.txt")"
chk "$tmp/a.plg" "$tmp/b.ptf" && fail "check_ext: a module of another image passed" || { grep -q "NOT the same image" "$tmp/out.txt" && ok "check_ext: another image refused" || fail "check_ext other image: $(cat "$tmp/out.txt")"; }
python3 - "$tmp/a.plg" "$tmp/dmg.plg" <<'PY'
import sys
b = bytearray(open(sys.argv[1], "rb").read()); b[200] ^= 1; open(sys.argv[2], "wb").write(b)
PY
chk "$tmp/dmg.plg" "$tmp/a.ptf" && fail "check_ext: a damaged module passed" || { grep -q "CRC" "$tmp/out.txt" && ok "check_ext: damaged file refused (CRC)" || fail "check_ext damaged: $(cat "$tmp/out.txt")"; }
mk_plg "$tmp/k.plg" "$tmp/a.ptf" 4
chk "$tmp/k.plg" "$tmp/a.ptf" && fail "check_ext: a synth plugin passed" || ok "check_ext: another plugin kind refused"
chk "$tmp/a.plg" "$tmp/a.ptf" --slot 0x70d90000 && fail "check_ext: wrong slot passed" || ok "check_ext: another slot address refused"
# extras.plg (--rel): text, data and bss within the release region (ext_abi.h EXT_REL_BYTES)
chk --rel "$tmp/a.plg" "$tmp/a.ptf" && ok "check_ext --rel: a bundle within the release region passes" || fail "check_ext rel good: $(cat "$tmp/out.txt")"
python3 - "$tmp/a.plg" "$tmp/big.plg" <<'PY'
import struct, sys
b = bytearray(open(sys.argv[1], "rb").read()); struct.pack_into("<I", b, 40, 40 * 1024 + 8); open(sys.argv[2], "wb").write(b)
PY
mk_plg "$tmp/r1.plg" "$tmp/a.ptf" 6 0x70d80000 notable
chk --rel "$tmp/r1.plg" "$tmp/a.ptf" && fail "check_ext --rel: a round 1 bundle without the sub-slot table passed" || { grep -q "no sub-slot table" "$tmp/out.txt" && ok "check_ext --rel: a round 1 bundle without the sub-slot table refused" || fail "check_ext rel no table: $(cat "$tmp/out.txt")"; }
chk --rel "$tmp/big.plg" "$tmp/a.ptf" && fail "check_ext --rel: a bundle bigger than the region passed" || { grep -q "release region has" "$tmp/out.txt" && ok "check_ext --rel: a bundle bigger than the region refused" || fail "check_ext rel big: $(cat "$tmp/out.txt")"; }

# the gate: before the simulator, so no plugins or old release are needed (OLD_REF is any commit)
gate() { IMAGE="$1" EXTRAS_PLG="$2" OLD_REF=HEAD ALLOW_UNPROVEN_END=1 SDRAM_FREE=519416 IMAGE_CHECK_ONLY=1 bash "$root/synth-open/tools/release_gate.sh" > "$tmp/gate.txt" 2>&1; }
gate "$tmp/a.ptf" "$tmp/a.plg" && grep -q "extras.plg ok" "$tmp/gate.txt" && ok "release_gate: the module of the image passes" || fail "gate good: $(tail -3 "$tmp/gate.txt")"
gate "$tmp/b.ptf" "$tmp/a.plg"; rc=$?
[ "$rc" = 1 ] && grep -q "FAIL - extras.plg" "$tmp/gate.txt" && ok "release_gate: a module of another image fails (exit 1)" || fail "gate bad: rc $rc $(tail -3 "$tmp/gate.txt")"

# the package: a card kit with plugins and no modit.plg, MODIT=skip, a module of another image
mkdir -p "$tmp/card/Plugins/Synth" "$tmp/card/Plugins/Master" "$tmp/notices"; : > "$tmp/card/Plugins/Synth/x.plg"; : > "$tmp/card/Plugins/Master/y.plg"
cp "$tmp/a.ptf" "$tmp/PolyendTracker_FF_1.4.0.ptf"
pkg() { MISMATCH_GATE=skip STACK_GATE=skip "$@" bash "$root/synth-open/tools/package_release.sh" "$tmp/PolyendTracker_FF_1.4.0.ptf" "$tmp/out" --card "$tmp/card" --notices "$tmp/notices" $PKGARGS > "$tmp/pkg.txt" 2>&1; }
PKGARGS="" pkg env; rc=$?
[ "$rc" = 1 ] && grep -q "no MOD/IT module" "$tmp/pkg.txt" && ok "package_release: plugins and no extras.plg: refused" || fail "package no module: rc $rc $(tail -3 "$tmp/pkg.txt")"
PKGARGS="" pkg env EXTRAS=skip; grep -q "EXTRAS=skip, this package has NO MOD/IT module" "$tmp/pkg.txt" && ok "package_release: EXTRAS=skip goes on and says so" || fail "package skip: $(tail -3 "$tmp/pkg.txt")"
mk_plg "$tmp/other.plg" "$tmp/b.ptf"
PKGARGS="--extras $tmp/other.plg" pkg env; rc=$?
[ "$rc" = 1 ] && grep -q "does not belong" "$tmp/pkg.txt" && ok "package_release: a module of another image: refused" || fail "package other image: rc $rc $(tail -3 "$tmp/pkg.txt")"
mkdir -p "$tmp/card/Plugins/Ext"; cp "$tmp/other.plg" "$tmp/card/Plugins/Ext/extras.plg"
PKGARGS="" pkg env; rc=$?
[ "$rc" = 1 ] && grep -q "card kit's extras.plg does not belong" "$tmp/pkg.txt" && ok "package_release: the card kit's extras.plg of another image: refused" || fail "package kit other image: rc $rc $(tail -3 "$tmp/pkg.txt")"

[ "$bad" -eq 0 ] && echo "RESULT: GREEN" || echo "RESULT: RED ($bad failing)"
exit "$bad"
