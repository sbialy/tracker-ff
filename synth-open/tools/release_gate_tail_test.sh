#!/usr/bin/env bash
# Host test and negative control of the image tail check in release_gate.sh (check_ptf.py --min-tail 0x800).
#   synth-open/tools/release_gate_tail_test.sh [REAL_IMAGE.ptf]
# Builds small valid .ptf files (vector table, stock flash config, data up to a chosen end) whose last 4 KB sector holds N bytes, and checks:
#   check_ptf.py --min-tail 0x800   exit 0 for a tail of 0x800 and more (and for 0 = the image ends on a sector line), exit 1 below
#   release_gate.sh                 exit 1 BEFORE the simulator for a short tail (so the control needs no plugins or old release), exit 2 without an image
# With REAL_IMAGE (a built release .ptf) the same image is also cut so that its last sector holds 0x100 B: the gate must fail on it.
# The positive case of the gate on a real image is the full release_gate.sh run (it takes minutes, the simulator runs).
set -u
here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
bad=0
ok()   { echo "ok   $*"; }
fail() { echo "FAIL $*"; bad=$((bad+1)); }

# mk <out.ptf> <end address>: image of [0, end): vector table (SP 0x20030000, reset 0x1D1), stock flash config at 0x400, 0x5A elsewhere
mk() {
python3 - "$1" "$2" <<'PY'
import sys
out, end = sys.argv[1], int(sys.argv[2], 0)
mem = bytearray([0x5A]) * end
mem[0:4] = (0x20030000).to_bytes(4, "little")
mem[4:8] = (0x1D1).to_bytes(4, "little")
mem[0x400:0x410] = bytes([0xFF] * 12 + [0xDE, 0xF9, 0xFF, 0xFF])
def rec(addr, rtype, data):
    raw = bytes([len(data), addr >> 8, addr & 0xFF, rtype]) + bytes(data)
    return ":" + (raw + bytes([(-sum(raw)) & 0xFF])).hex().upper()
lines = []
for base in range(0, end, 0x10000):   # type 02 segment address every 64 KB, like the stock images
    lines.append(rec(0, 2, [(base >> 4) >> 8, (base >> 4) & 0xFF]))
    for a in range(base, min(base + 0x10000, end), 16):
        lines.append(rec(a - base, 0, mem[a:min(a + 16, end)]))
lines.append(rec(0, 1, []))
open(out, "w").write("\n".join(lines) + "\n")
PY
}

# cut <in.ptf> <out.ptf> <tail>: the real image with everything above (last sector line + tail) dropped
cut_tail() {
python3 - "$1" "$2" "$3" <<'PY'
import sys
src, out, tail = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
rows, upper, top = [], 0, 0
for line in open(src):
    raw = bytes.fromhex(line.strip()[1:])
    if raw[3] == 0:
        top = max(top, upper + ((raw[1] << 8) | raw[2]) + raw[0])
    elif raw[3] == 2:
        upper = ((raw[4] << 8) | raw[5]) << 4
end = (top & ~0xFFF) + tail
upper = 0
with open(out, "w") as f:
    for line in open(src):
        raw = bytes.fromhex(line.strip()[1:])
        if raw[3] == 2:
            upper = ((raw[4] << 8) | raw[5]) << 4
        if raw[3] == 0:
            a = upper + ((raw[1] << 8) | raw[2])
            if a >= end:
                continue
            n = min(raw[0], end - a)
            data = raw[4:4 + n]
            r = bytes([n, raw[1], raw[2], 0]) + data
            line = ":" + (r + bytes([(-sum(r)) & 0xFF])).hex().upper()
        f.write(line.strip() + "\n")
PY
}

chk() { # <expect rc> <label> <ptf>
    python3 -I "$here/check_ptf.py" --min-tail 0x800 "$3" > "$tmp/o" 2>&1; local rc=$?
    if [ "$rc" = "$1" ]; then ok "check_ptf $2 -> $rc"; else fail "check_ptf $2: want $1 got $rc"; sed 's/^/     /' "$tmp/o"; fi
}
gate() { # <expect rc> <label> <ptf or ->
    if [ "$3" = - ]; then IMAGE="$tmp/none.ptf" "$here/release_gate.sh" > "$tmp/o" 2>&1; else IMAGE="$3" "$here/release_gate.sh" > "$tmp/o" 2>&1; fi
    local rc=$?
    if [ "$rc" = "$1" ]; then ok "release_gate $2 -> $rc"; else fail "release_gate $2: want $1 got $rc"; sed 's/^/     /' "$tmp/o" | head -8; fi
}

mk "$tmp/t0100.ptf" 0xF8100;  chk 1 "tail 0x100" "$tmp/t0100.ptf"
mk "$tmp/t0124.ptf" 0xF8124;  chk 1 "tail 0x124 (the tip image before the pad)" "$tmp/t0124.ptf"
mk "$tmp/t07ff.ptf" 0xF87FF;  chk 1 "tail 0x7ff (one under the line)" "$tmp/t07ff.ptf"
mk "$tmp/t0800.ptf" 0xF8800;  chk 0 "tail 0x800 (the line)" "$tmp/t0800.ptf"
mk "$tmp/t0e48.ptf" 0xF8E48;  chk 0 "tail 0xe48 (the padded release)" "$tmp/t0e48.ptf"
mk "$tmp/t1000.ptf" 0xF9000;  chk 0 "tail 0 (ends on a sector line, full last sector)" "$tmp/t1000.ptf"
gate 1 "short tail 0x100 (negative control)" "$tmp/t0100.ptf"
grep -q "release gate: FAIL - image check" "$tmp/o" && grep -q "last 4 KB sector" "$tmp/o" && ok "gate message names the sector" || { fail "gate message"; sed 's/^/     /' "$tmp/o"; }
gate 2 "no image" -
if [ -n "${1:-}" ]; then
    cut_tail "$1" "$tmp/real_short.ptf" 0x100 || fail "cut_tail"
    chk 1 "REAL image cut to a 0x100 tail" "$tmp/real_short.ptf"
    gate 1 "REAL image cut to a 0x100 tail (negative control)" "$tmp/real_short.ptf"
    chk 0 "REAL image as built" "$1"
fi
echo
[ "$bad" = 0 ] && { echo "release_gate_tail_test: all passed"; exit 0; }
echo "release_gate_tail_test: $bad failed"; exit 1
