#!/bin/bash
# sample_loader_browse_sim.sh [PNG_DIR]
#
# The Sample Loader enters a folder while its list is still drawn (ui_shot, the host build, nothing here touches a device).
# cFileManager::browseSdCard starts an async browse that frees and refills the explorer array the list control draws from, while the
# control keeps the row count of the folder it came from. A list that redraws in that window (the selected row scrolls when its name is
# wider than the column, so a long folder name is enough) reached the NULL rows: cList::update called strlen(NULL), SIGSEGV on the host.
# On the MK66 strlen(0) reads the vector table (first byte 0) and the row draws empty.
#
#   SLB1  long folder name selected, enter a folder with 1 wav: the run ends (exit 0) and the shot has the folder's list
#   SLB2  the same with 40 wavs (the browse takes about 80 file manager steps, the long name keeps scrolling the whole time)
#   SLB3  enter and come back with "/..": the root list equals the one before the enter (pixel exact, short names), the long name run ends
#   SLB4  short folder names (no scroll, no redraw): still fine, and the entered list differs from the root list
#   SLB5  repeat the SLB1 enter 10 times with different waits after the key (the redraw lands on every step of the browse)
#
# BIN=path overrides the binary (the pre-fix one for the RED run). SAN=address,undefined: the sanitizer build.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${BIN:-${UI_SHOT_BIN:-}}"
OUT="${1:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/sample_loader_browse_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN:-}" ]; then   # the tree's own binary is built, a BIN given is used as it is
    BIN="$BUILD/ui_shot"
    mkdir -p "$BUILD"
    make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "sample_loader_browse_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"

fails=0
ok()   { printf 'ok    %s\n' "$1"; }
fail() { fails=$((fails + 1)); printf 'FAIL  %s\n' "$1"; }

# a 16 bit mono 44.1 kHz wav of 2,000 frames (python writes the header, the Mac has no sox)
mkwav() {
    python3 -I - "$1" <<'EOF'
import struct, sys, math
n = 2000
d = b''.join(struct.pack('<h', int(8000 * math.sin(i * 0.05))) for i in range(n))
h = b'RIFF' + struct.pack('<I', 36 + len(d)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, 44100, 88200, 2, 16) + b'data' + struct.pack('<I', len(d))
open(sys.argv[1], 'wb').write(h + d)
EOF
}

# a card with 20 folders of the given name prefix (the first one sorts first and holds $2 wavs), primed by one power up
card() {
    local sd="$1" prefix="$2" wavs="$3"
    rm -rf "$sd"; mkdir -p "$sd"
    "$BIN" "$sd" "wait 100" >/dev/null 2>&1 || return 1
    local i
    for i in $(seq -w 1 20); do mkdir -p "$sd/${prefix}${i}"; done
    mkwav "$WORK/base.wav"
    for i in $(seq 1 "$wavs"); do cp "$WORK/base.wav" "$sd/${prefix}01/s$(printf %02d "$i").wav"; done
}
LONG='folder_with_a_rather_long_name_number_'   # wider than the 270 px column, the selected row scrolls
SHORT='f'

run() {   # run SD "script" LOG
    timeout 120 "$BIN" "$1" "$2" >"$3" 2>&1
}
why() { grep -m1 -E 'signal|ERROR|AddressSanitizer' "$1" 2>/dev/null || echo "no output"; }
png() { [ -n "$OUT" ] && { mkdir -p "$OUT"; cp "$1" "$OUT/$2" 2>/dev/null; }; }

# the folders sort before Fonts, Themes and Workspace (case insensitive), so the first row is the long folder
card "$WORK/sd1" "$LONG" 1 || fail "SLB1 (card)"
run "$WORK/sd1" "tap SampleLoad; wait 1500; tap Insert; wait 20; wait 1500; shot $WORK/in1.png" "$WORK/slb1.log"; rc=$?
if [ $rc -eq 0 ] && [ -s "$WORK/in1.png" ]; then ok SLB1; png "$WORK/in1.png" slb1-in.png; else fail "SLB1 ($(why "$WORK/slb1.log"))"; fi

card "$WORK/sd2" "$LONG" 40 || fail "SLB2 (card)"
run "$WORK/sd2" "tap SampleLoad; wait 1500; tap Insert; wait 20; wait 6000; shot $WORK/in2.png" "$WORK/slb2.log"; rc=$?
if [ $rc -eq 0 ] && [ -s "$WORK/in2.png" ]; then ok SLB2; png "$WORK/in2.png" slb2-in.png; else fail "SLB2 ($(why "$WORK/slb2.log"))"; fi

# SLB3: the "/.." row is the first row of the folder, Insert on it goes back to the root list with the folder selected again. The pixel
# compare needs short names (a scrolling name is a different frame every time), the long name run only has to end
BACK='tap Insert; wait 20; wait 1500; tap Insert; wait 20; wait 1500'
card "$WORK/sd3" "$LONG" 1 || fail "SLB3 (card)"
run "$WORK/sd3" "tap SampleLoad; wait 1500; $BACK" "$WORK/slb3.log"; rc3=$?
card "$WORK/sd3s" "$SHORT" 1 || fail "SLB3 (short card)"
run "$WORK/sd3s" "tap SampleLoad; wait 1500; shot $WORK/root3a.png; $BACK; shot $WORK/root3b.png" "$WORK/slb3s.log"; rc3s=$?
if [ $rc3 -ne 0 ]; then fail "SLB3 (long names: $(why "$WORK/slb3.log"))"
elif [ $rc3s -ne 0 ]; then fail "SLB3 ($(why "$WORK/slb3s.log"))"
elif cmp -s "$WORK/root3a.png" "$WORK/root3b.png"; then ok SLB3
else fail "SLB3 (the root list after the way back differs from the one before)"; fi

card "$WORK/sd4" "$SHORT" 1 || fail "SLB4 (card)"
run "$WORK/sd4" "tap SampleLoad; wait 1500; shot $WORK/root4.png; tap Insert; wait 20; wait 1500; shot $WORK/in4.png" "$WORK/slb4.log"; rc=$?
if [ $rc -ne 0 ]; then fail "SLB4 ($(why "$WORK/slb4.log"))"
elif cmp -s "$WORK/root4.png" "$WORK/in4.png"; then fail "SLB4 (the list did not change after the enter)"
else ok SLB4; fi

bad=0
for w in 1 5 10 20 30 40 60 100 150 250; do
    card "$WORK/sd5" "$LONG" 6 || { bad=$((bad + 1)); continue; }
    run "$WORK/sd5" "tap SampleLoad; wait 1500; tap Insert; wait $w; wait 2500" "$WORK/slb5.log" || { bad=$((bad + 1)); echo "      wait $w: $(why "$WORK/slb5.log")"; }
done
if [ $bad -eq 0 ]; then ok SLB5; else fail "SLB5 ($bad of 10 runs failed)"; fi

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; else echo "RESULT: RED ($fails failing)"; exit 1; fi
