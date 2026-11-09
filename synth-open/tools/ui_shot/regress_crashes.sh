#!/bin/bash
# regress_crashes.sh [UI_SHOT_BINARY]
#
# Regression checks for the two ui_shot problems found by UI scenario runs:
#   sampleload   the SampleLoad page lists the card root without crashing (host file system stub: list entries were never allocated)
#   locked       a folder that cannot be opened, entered on SampleLoad, shows an empty list with "/.." and does not crash
#                (firmware: cFileManager::listOnlyFolderNames kept the kListError result in a uint8_t)
#   perf-trip    pattern editor, Performance page, pattern editor: the second pattern screen equals the first
#                (host FT812 model: BITMAP_SIZE and the transform set inside SAVE_CONTEXT leaked out of RESTORE_CONTEXT)
#   synth-enum-oom    synth patch editor: the value list of an enum parameter (WTFM Character 1, 9 values of 24 B = a 216 B block) when
#                     the heap is full, operator new returns null like on the MK66 (host_fault.cpp). The page shows the bar instead
#   synth-badparam / synth-badmacro-name / synth-badmacro-value
#                     synth patch editor: a parameter or macro index outside the patch (corrupt metadata, host_poke.cpp paramid and
#                     macroid) must be refused by the three bounds checks (they were written with && and never fired)
# Exit code is the number of failed checks. Host only, nothing here is in the firmware image.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-$ROOT/build/ui-shot/ui_shot}"
[ -x "$BIN" ] || { echo "regress_crashes.sh: no ui_shot at $BIN (build it with ui_shot.sh first)" >&2; exit 99; }
TMP="$(mktemp -d "${TMPDIR:-/tmp}/uishot-regress.XXXXXX")"
cleanup() { chmod -R u+rwx "$TMP" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
fails=0
pass() { echo "ok   $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

# ui_shot SD_DIR "script" with a time limit, a stuck run counts as a failure
run() { local sd="$1" script="$2" log="$3"; mkdir -p "$sd"; timeout 120 "$BIN" "$sd" "$script" >"$log" 2>&1; }

run "$TMP/sd1" "wait 500; tap SampleLoad; wait 1500; shot $TMP/sampleload.png" "$TMP/sampleload.log"
if [ $? -eq 0 ] && [ -s "$TMP/sampleload.png" ]; then pass sampleload; else fail "sampleload ($(grep -m1 -E 'signal|ERROR' "$TMP/sampleload.log"))"; fi

mkdir -p "$TMP/sd2/Locked"; chmod 000 "$TMP/sd2/Locked"
# the SD card root lists Fonts, Locked, Plugins ... once the firmware has created its folders: Locked is the first folder after the card's own
run "$TMP/sd2" "wait 500; tap SampleLoad; wait 1500; tap Down; wait 300; tap F1; wait 2500; shot $TMP/locked.png" "$TMP/locked.log"
if [ $? -eq 0 ] && [ -s "$TMP/locked.png" ]; then pass locked; else fail "locked ($(grep -m1 -E 'signal|ERROR' "$TMP/locked.log"))"; fi
chmod 755 "$TMP/sd2/Locked"

run "$TMP/sd3" "wait 800; shot $TMP/pat1.png; tap Performance; wait 1000; tap Pattern; wait 1000; shot $TMP/pat2.png" "$TMP/perf.log"
if [ $? -eq 0 ] && cmp -s "$TMP/pat1.png" "$TMP/pat2.png"; then pass perf-trip; else fail "perf-trip (pattern screen differs after the Performance page)"; fi

# synth patch editor (instrument 64 is the first synth part, WTFM). Edit Patch, section Osc, param list focused: the list of
# Character 1 appears with Down 5 (216 B of strings). pad 25 selects macro section M1
SYN='instr 64; tap Params; wait 1200; tap F8; wait 2000'
export UISHOT_POKE="$TMP/poke"

# control: the list shows, the injection is not active
run "$TMP/sd4" "$SYN; tap Right; wait 800; tap Right; wait 800; tap Down 5; wait 600" "$TMP/enum-ok.log"
if [ $? -eq 0 ]; then pass synth-enum-control; else fail "synth-enum-control ($(grep -m1 -E 'signal|ERROR' "$TMP/enum-ok.log"))"; fi
UISHOT_FAIL=size=216 UISHOT_FAIL_REPORT="$TMP/enum.rep" run "$TMP/sd4" "$SYN; tap Right; wait 800; tap Right; wait 800; tap Down 5; wait 600; shot $TMP/enum.png" "$TMP/enum.log"
if [ $? -eq 0 ] && [ -s "$TMP/enum.png" ] && grep -q 'failed alloc of 216 B' "$TMP/enum.rep" 2>/dev/null; then pass synth-enum-oom
else fail "synth-enum-oom ($(grep -m1 -E 'signal|ERROR' "$TMP/enum.log"), injected: $(wc -l <"$TMP/enum.rep" 2>/dev/null))"; fi

# the index is set after the list was built, the next key reads it. 2147483647 puts the read far outside the heap
rm -f "$TMP/poke.out"
run "$TMP/sd4" "$SYN; tap Right; wait 800; tap Right; wait 800; poke paramid 5 2147483647; tap Down 5; wait 600; shot $TMP/bp.png" "$TMP/bp.log"
if [ $? -eq 0 ] && grep -q 'paramid\[5\] = 2147483647' "$TMP/poke.out" 2>/dev/null; then pass synth-badparam; else fail "synth-badparam ($(grep -m1 -E 'signal|ERROR' "$TMP/bp.log"))"; fi
rm -f "$TMP/poke.out"
run "$TMP/sd4" "$SYN; pad 25; wait 800; poke macroid 9 2147483647; tap F6; wait 600; shot $TMP/bmn.png" "$TMP/bmn.log"
if [ $? -eq 0 ] && grep -q 'macroid\[9\] = 2147483647' "$TMP/poke.out" 2>/dev/null; then pass synth-badmacro-name; else fail "synth-badmacro-name ($(grep -m1 -E 'signal|ERROR' "$TMP/bmn.log"))"; fi
rm -f "$TMP/poke.out"
run "$TMP/sd4" "$SYN; pad 25; wait 800; tap Down; wait 500; poke macroid 9 2147483647; enc 1; wait 600; shot $TMP/bmv.png" "$TMP/bmv.log"
if [ $? -eq 0 ] && grep -q 'macroid\[9\] = 2147483647' "$TMP/poke.out" 2>/dev/null; then pass synth-badmacro-value; else fail "synth-badmacro-value ($(grep -m1 -E 'signal|ERROR' "$TMP/bmv.log"))"; fi
unset UISHOT_POKE

echo "$fails check(s) failed"
exit "$fails"
