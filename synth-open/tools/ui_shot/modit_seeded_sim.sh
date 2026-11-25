#!/bin/bash
# modit_seeded_sim.sh
#
# Why a MOD import on the unit differs from the host golden (modit_golden_sim.sh G1) in pattern_01.mtp, project.mt and project.mtff, and what is
# expected. Found in the multi-ext round 1 device run (poly-multi-ext-r1b), simulator first:
#   S1  project.mt: the golden is the file the host import script leaves, it stops before the Workspace autosave (WorkspaceProjectSave.cpp). The unit
#       saves project.mt after the import, with the imported play order (the 128 order bytes of the MOD, +1). `wsave` is that save: the file then
#       carries a play order that is not just [1, 0, 0 ...]
#   S2  pattern_01.mtp: the import clears a pattern with clearPattern, which resets the FX types and leaves the FX value bytes (clearStepStored).
#       The unit imports into the project that was open, the golden into a blank one, so the value bytes of FX slots with type 0 differ and
#       the pattern crc with them. Everything else is equal. A blank start and a start from a project with such leftover bytes are compared
#       byte by byte, only value bytes of type 0 slots (and the crc) may differ
#   S3  project.mtff differs in the build tag and the project.mt crc word (the unit's own build id, the saved project.mt): nothing to compare
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
MODIT="$ROOT/synth-open/tools/modit"
BIN="${BIN:-$ROOT/build/ui-shot/ui_shot}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/modit_seeded.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN_PREBUILT:-}" ]; then
    mkdir -p "$ROOT/build"
    make -s -C "$HERE" -j4 >"$ROOT/build/ui-shot.make.log" 2>&1 || { tail -30 "$ROOT/build/ui-shot.make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "no ui_shot binary $BIN"; exit 2; }
fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
card() { local sd="$WORK/$1"; rm -rf "$sd"; mkdir -p "$sd/Mods"; cp "$MODIT"/fixtures/golden.* "$sd/Mods/"; echo "$sd"; }
run() { ASAN_OPTIONS=detect_leaks=0 "$BIN" "$1" "wait 500; $2" 2>/dev/null | grep -E '^(modimport|wsave)'; }

# blank start: the golden's own run, then the autosave
b="$(card blank)"; run "$b" "modimport Mods golden.mod" >/dev/null; cp "$b/Workspace/project.mt" "$WORK/blank.project.mt"; cp "$b/Workspace/patterns/pattern_01.mtp" "$WORK/blank.pattern_01.mtp"
run "$b" "modimport Mods golden.mod; wsave" >/dev/null; cp "$b/Workspace/project.mt" "$WORK/blank.saved.project.mt"
# seeded start: the Workspace of the project the unit had open, with FX value bytes left under type 0 (what deleting an FX in the editor leaves)
s="$(card seeded)"; run "$s" "modimport Mods golden.it; setstep 0 0 -1 0 0 7 0 9; setstep 1 5 -1 0 0 33 0 0; dirty; wait 1500" >/dev/null
grep -q "" "$s/Workspace/patterns/pattern_01.mtp" 2>/dev/null || fail "seed" "no Workspace pattern"
out="$(run "$s" "modimport Mods golden.mod; wsave")"
case "$out" in *"golden.mod started"*"wsave done"*) pass "seeded import ran ($out)";; *) fail "seeded import" "$out"; echo "RESULT: RED ($fails failing of $checks)"; exit 1;; esac

python3 -I - "$WORK" "$s/Workspace" "$MODIT/fixtures/golden.mod" <<'PY' && pass "S1 and S2 byte checks" || fail "S1 or S2 byte check" "see above"
import sys
work, ws, modf = sys.argv[1:4]
rd = lambda p: open(p, 'rb').read()
ok = True
def bad(m):
    global ok; ok = False; print('      ' + m)
# S1: the saved project.mt carries the play order of the MOD, the unsaved one the default [1, 0, 0 ...]
pl_unsaved = rd(work + '/blank.project.mt')[16:16 + 128]
pl_saved = rd(work + '/blank.saved.project.mt')[16:16 + 128]
if pl_unsaved != bytes([1]) + bytes(127): bad('unsaved play order is not the default: %s' % pl_unsaved.hex())
mod = rd(modf)
nsamp = 31 if mod[1080:1084] in (b'M.K.', b'M!K!', b'4CHN', b'FLT4') else 15
order = mod[20 + nsamp * 30 + 2: 20 + nsamp * 30 + 2 + 128]
if pl_saved != bytes(b + 1 for b in order): bad('saved play order is not the MOD order list + 1: %s' % pl_saved.hex())
if pl_saved == pl_unsaved: bad('the autosave changed nothing')
if rd(ws + '/project.mt')[16:16 + 128] != pl_saved: bad('seeded start saves another play order')
# S2: pattern 1 of a blank start against the seeded start: only value bytes of FX slots with type 0, and the crc
a = rd(work + '/blank.pattern_01.mtp'); b = rd(ws + '/patterns/pattern_01.mtp')
if len(a) != len(b): bad('pattern sizes differ')
else:
    # layout: 28 byte header, 16 tracks of (length byte + 128 steps of 8 bytes: note, instrument, fx0 type/value, fx1 type/value, volume, micromove), crc
    allowed = set(); ntrack = 16; per = (len(a) - 28 - 4) // ntrack
    for t in range(ntrack):
        base = 28 + t * per + 1
        for st in range((per - 1) // 8):
            o = base + st * 8
            for k in (2, 4):
                if a[o + k] == 0 and b[o + k] == 0: allowed.add(o + k + 1)
    diff = [i for i in range(len(a) - 4) if a[i] != b[i]]
    stray = [i for i in diff if i not in allowed]
    if stray: bad('pattern bytes that differ outside the FX value of a type 0 slot: %s' % stray[:12])
    if not diff: bad('seeded and blank pattern are equal: the seed carried no FX value bytes, the check proves nothing')
    print('      pattern_01: %d stale FX value bytes under type 0, crc differs: %s' % (len(diff), a[-4:] != b[-4:]))
sys.exit(0 if ok else 1)
PY
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
