#!/bin/bash
# usb_multich_sim.sh
#
# USB audio 8+3 mono (Config > USB > Audio mode): the tap at the end of the audio graph (core/AudioEngine/UsbMultiTap) and the
# platform's interleave with its synth delay (platforms/mk66/usbaudio/UsbAudioOut.cpp) run in the real engine of this tree on the Mac
# (ui_shot, host_usbmc.cpp), every block checked bit exact against the Song Stems tap points and against the Tracker's own bus:
#   mc      8+3, mixer stage Hard Clip: three sample tracks (sines), synth parts 1 and 2. USB track c == MonoToStereo L == R (centre),
#           USB synth p == (L + R) >> 1 of the part's panning amplifiers (aligned), sum of the 11 USB channels == the tracks mixer L and R
#   pan     instrument 1 hard left: track 1 keeps its dry level on USB (pre pan) while its right side is silent
#   soft    the project's default mixer stage: the per source checks hold, the bus is not compared (the stage is not a plain sum)
#   stereo  Mode Stereo (flag on, mode byte stereo): no tap, the graph is the stereo one
#   off     USB audio off with the mode byte at 8+3: no tap
# Host only, no device. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)" (exit 1).
#   synth-open/tools/ui_shot/usb_multich_sim.sh       OUT=the ui_shot build directory (default build/ui-shot-usbmc), SAN=address,undefined
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
OUT="${OUT:-$ROOT/build/ui-shot-usbmc${SAN:+-san}}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/usb_multich_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
BIN="${UI_SHOT_BIN:-$OUT/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make
mkdir -p "$OUT"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" OUT="$OUT" ${SAN:+SAN=$SAN} -j"$(sh "$HERE/../jobs.sh" n)" >"$OUT.make.log" 2>&1 || { tail -30 "$OUT.make.log"; exit 2; }

fails=0; checks=0
ok() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
bad() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n' "$1"; }
expect() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
run() {   # NAME ENV SCRIPT: the usbmc lines in WORK/NAME.txt
    local sd="$WORK/$1.sd"; rm -rf "$sd"
    env $2 ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "$3" > "$WORK/$1.txt" 2> "$WORK/$1.err"
    if grep -E "runtime error|AddressSanitizer" "$WORK/$1.err" | grep -v "mtWavetableEnvelopes.cpp:206" | grep -q .; then
        bad "$1: sanitizer finding: $(grep -E 'runtime error|AddressSanitizer' "$WORK/$1.err" | head -1)"
    fi
}
field() { sed -nE "s/.*$2 ([0-9]+).*/\1/p" <<<"$(grep -E "^usbmc $1" "$WORK/$3.txt" | head -1)"; }
# "usbmc track N centre C bad B panned P bad Q live L": B + Q over the 8 tracks
tracksbad() { grep -E "^usbmc track" "$WORK/$1.txt" | awk '$6 == "bad" && $10 == "bad" { s += $7 + $11; n++ } END { print (n == 8 ? s + 0 : "x") }'; }

TONES="sampletonehz 0 220 300; sampletonehz 1 330 300; sampletonehz 2 495 300; setstep 0 0 60 0 0 0 0 0; setstep 1 0 60 1 0 0 0 0; setstep 2 0 60 2 0 0 0 0"
SYN="setstep 3 0 48 64 0 0 0 0; setstep 4 2 55 65 0 0 0 0"
PLAY="audio 1; usbmc 1; tap Play; wait 3000; tap Play; wait 200; usbmc dump"
MC="UISHOT_USB_FLAG=1 UISHOT_USB_MODE=1"

run mc "$MC" "wait 500; usbmc sat 0; $TONES; $SYN; $PLAY"
n=$(field sink sink mc); blk=$(field sink blocks mc)
expect "mc: the tap ran every block ($n blocks)" '[ "${n:-0}" -gt 900 ] && [ "$n" = "$blk" ] && [ "$(field sink sinkmissing mc)" = 0 ]'
expect "mc: tracks 1-3 sound on USB, 4-8 silent" '[ "$(field "track 1" live mc)" -gt 900 ] && [ "$(field "track 3" live mc)" -gt 900 ] && [ "$(field "track 4" live mc)" = 0 ]'
expect "mc: every track channel == its Stems tap (MonoToStereo L and R) bit exact" '[ "$(tracksbad mc)" = 0 ] && [ "$(field "track 1" centre mc)" = "$blk" ]'
expect "mc: synth parts 1 and 2 sound, part 3 silent" '[ "$(field "synth 1" live mc)" -gt 900 ] && [ "$(field "synth 2" live mc)" -gt 100 ] && [ "$(field "synth 3" live mc)" = 0 ]'
expect "mc: every synth channel == (L + R) >> 1 of its panning amplifiers, aligned (2 block delay)" '[ "$(field "synth 1" bad mc)" = 0 ] && [ "$(field "synth 2" bad mc)" = 0 ] && [ "$(field "synth 3" bad mc)" = 0 ]'
expect "mc: sum of the 11 USB channels == the Tracker's tracks bus L and R, bit exact ($(field sink live mc) live blocks)" '[ "$(field sink bus mc)" = "$blk" ] && [ "$(field sink bad mc)" = 0 ] && [ "$(field sink live mc)" -gt 900 ]'
expect "mc: the interleaved frames decode to the tap inputs" '[ "$(field sink decodebad mc)" = 0 ]'

run pan "$MC" "wait 500; usbmc sat 0; $TONES; usbmc ipan 0 0; $PLAY"
expect "pan: track 1 hard left is panned on the bus, dry on USB (pre pan)" '[ "$(field "track 1" panned pan)" -gt 900 ] && [ "$(tracksbad pan)" = 0 ]'
expect "pan: the other tracks stay bit exact at centre" '[ "$(field "track 2" centre pan)" -gt 900 ] && [ "$(field "track 2" live pan)" -gt 900 ]'

run soft "$MC" "wait 500; $TONES; $SYN; $PLAY"
expect "soft: the project's mixer stage, per source checks bit exact" '[ "$(tracksbad soft)" = 0 ] && [ "$(field "synth 1" bad soft)" = 0 ] && [ "$(field sink decodebad soft)" = 0 ] && [ "$(field "synth 1" live soft)" -gt 900 ]'

run stereo "UISHOT_USB_FLAG=1 UISHOT_USB_MODE=0" "wait 500; usbmode; $TONES; $PLAY"
expect "stereo: no tap (Mode Stereo runs the stereo graph)" 'grep -q "usb audio mode running 0 eeprom 0" "$WORK/stereo.txt" && [ "$(field sink sink stereo)" = 0 ]'

run off "UISHOT_USB_FLAG=0 UISHOT_USB_MODE=1" "wait 500; usbmode; $TONES; $PLAY"
expect "off: no tap (USB audio off, the mode byte alone does nothing)" 'grep -q "usb audio mode running 0 eeprom 1" "$WORK/off.txt" && [ "$(field sink sink off)" = 0 ]'

# negative control: the same run with the synth delay off (UISHOT_USBMC_NODELAY=1, host only) MUST fail the aligned checks
run neg "$MC UISHOT_USBMC_NODELAY=1" "wait 500; usbmc sat 0; $TONES; $SYN; $PLAY"
expect "negative control: without the synth delay the synth and bus checks go red" '[ "$(field "synth 1" bad neg)" -gt 900 ] && [ "$(field sink bad neg)" -gt 900 ]'
# negative control: a track channel off by one LSB MUST fail the track check
run neg2 "$MC UISHOT_USBMC_TRACKLSB=1" "wait 500; usbmc sat 0; $TONES; $PLAY"
expect "negative control: one LSB on track 2 goes red" '[ "$(tracksbad neg2)" -gt 900 ]'

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
