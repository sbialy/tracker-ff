#!/bin/bash
# regress_crashes.sh [UI_SHOT_BINARY]
#
# Regression checks for the two ui_shot problems found by the usbtest scenario suite (ref/ui-shot-crash-investigation.md):
#   sampleload   the SampleLoad page lists the card root without crashing (host file system stub: list entries were never allocated)
#   locked       a folder that cannot be opened, entered on SampleLoad, shows an empty list with "/.." and does not crash
#                (firmware: cFileManager::listOnlyFolderNames kept the kListError result in a uint8_t)
#   perf-trip    pattern editor, Performance page, pattern editor: the second pattern screen equals the first
#                (host FT812 model: BITMAP_SIZE and the transform set inside SAVE_CONTEXT leaked out of RESTORE_CONTEXT)
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

echo "$fails check(s) failed"
exit "$fails"
