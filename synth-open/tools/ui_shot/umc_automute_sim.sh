#!/bin/bash
# umc_automute_sim.sh
#
# USB audio 8+3 auto mute of the master Delay and Reverb (Config > USB > FX in 8+3, core/libs/Audio/fx_auto_mute.h) in the real
# engine of this tree on the Mac (ui_shot with EXTHOOKS=1, `usbstream N` is the host stand-in for "the computer records now"), read with the
# `poke fxauto` line (host_poke_fx.cpp). A runtime override on the Mixer's Mute, never written to the project:
#   engage    default setting (no fxmute line): not before 1 s of streaming, then the FX sleep fast (fade, no 3 s ring), the Mute keys are
#             locked and the Global Mixer labels read 8+3, release after 1 s without streaming, the FX come back
#   blips     a short burst never engages, a short gap does not release
#   flags     the user's flags (Delay muted) are the state after the stream: keys locked (flags unchanged), a save during streaming keeps
#             the flags, the saved project opens with them, the Mute keys work again after the release
#   off       FX in 8+3 Off (fxmute 1 in usbaudio.cfg): never engages, the keys work while streaming
#   offlive   Off set from the page while the override is on: released at once, the line is written to the file, Auto mute drops it again
#   stereo    Mode Stereo: streaming never engages
#   norecord  8+3 but the computer does not record: nothing happens
# Host only, no device. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)" (exit 1).
#   synth-open/tools/ui_shot/umc_automute_sim.sh       OUT=the ui_shot build directory (default build/ui-shot-ext), SHOTS=dir for the PNGs
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${OUT:-$ROOT/build/ui-shot-ext}"
SHOTS="${SHOTS:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/umc_automute_sim.XXXXXX")"
trap 'rm -rf "${WORK:?}"' EXIT
make -s -C "$HERE" EXTHOOKS=1 OUT="$OUT" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >"$OUT.make.log" 2>&1 || { tail -30 "$OUT.make.log"; exit 2; }
[ -n "$SHOTS" ] && mkdir -p "$SHOTS"
SH="${SHOTS:-$WORK}"

fails=0; checks=0
ok() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
bad() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n' "$1"; }
CFGHEAD='feature 1\nid usbaudio\nversion 1\non 1\n'
# run NAME ENV CFGLINES SCRIPT: WORK/NAME.poke.out (the fxauto lines), WORK/NAME.txt (the output), WORK/NAME.sd/Features/usbaudio.cfg
run() {
    local name="${1:?}"
    local sd="${WORK:?}/$name.sd"
    mkdir -p "$sd/Features"
    printf "$CFGHEAD$3" > "$sd/Features/usbaudio.cfg"
    env $2 UISHOT_POKE="$WORK/$name.poke" ASAN_OPTIONS=detect_leaks=0 timeout 300 "$OUT/ui_shot" "$sd" "$4" > "$WORK/$name.txt" 2> "$WORK/$name.err"
    echo "exit $?" >> "$WORK/$name.txt"
    grep -q "^exit 0" "$WORK/$name.txt" || bad "$name: ui_shot did not exit 0 ($(tail -2 "$WORK/$name.txt" | tr '\n' ' '))"
}
# line NAME N: the N-th fxauto line of the run
line() { sed -n "$2p" "$WORK/$1.poke.out"; }
# expect NAME N "what" "regex": the N-th line matches the extended regex
expect() { if line "$1" "$2" | grep -Eq "$4"; then ok "$1.$2: $3"; else bad "$1.$2: $3 [got: $(line "$1" "$2")]"; fi; }
D='delay muted 0 asleep 0 active 1 \| reverb muted 0 asleep 0 active 1 \| sendmix 1 1'   # both FX awake, send mixers in update_all
Z='delay muted 1 asleep 1 active 0 \| reverb muted 1 asleep 1 active 0 \| sendmix 0 0'    # both asleep, send mixers out
MC="UISHOT_USB_FLAG=1 UISHOT_USB_MODE=1"
NAVM="tap Master;wait 800;tap Master;wait 800;tap Master;wait 800"   # Master > Global Mixer 3/3
FX="poke fxauto"

# engage: 1 start  2 stream +0.6 s  3 +1.3 s  4 keys  5 stream off +0.6 s  6 +1.3 s
run engage "$MC" "" "audio 1;wait 500;$NAVM;$FX;usbstream 1;wait 600;$FX;wait 700;$FX;shot $SH/automute-locked.png;tap F1;tap F2;wait 300;$FX;usbstream 0;wait 600;$FX;wait 700;$FX;shot $SH/automute-released.png"
expect engage 1 "idle: enabled, not active, nothing streams, both FX awake, labels Mute" "enabled 1 active 0 streaming 0 .*$D .*labels delay 'Mute' reverb 'Mute'"
expect engage 2 "0.6 s of streaming: debounce, not engaged yet" "enabled 1 active 0 streaming 2 .*$D .*labels delay 'Mute'"
expect engage 3 "1.3 s of streaming: engaged, both FX asleep with their send mixers (no 3 s ring), labels 8+3" "enabled 1 active 1 streaming 2 \| flags delay 0 reverb 0 \| $Z \| labels delay '8\+3' reverb '8\+3'"
expect engage 4 "F1 and F2 while engaged: locked, the project flags are still 0 0, still asleep" "active 1 .*flags delay 0 reverb 0 \| $Z \| labels delay '8\+3'"
expect engage 5 "stream stopped 0.6 s: not released yet" "active 1 streaming 0 .*$Z"
expect engage 6 "stream stopped 1.3 s: released, both FX awake again, flags 0 0, labels Mute" "active 0 streaming 0 \| flags delay 0 reverb 0 \| $D \| labels delay 'Mute' reverb 'Mute'"

# blips: two 0.5 s bursts never engage (1), 1.5 s engages (2), a 0.5 s gap does not release (3), the stream is back (4, 5)
run blips "$MC" "" "audio 1;wait 500;$NAVM;usbstream 1;wait 500;usbstream 0;wait 500;usbstream 1;wait 500;usbstream 0;wait 1500;$FX;usbstream 1;wait 1500;$FX;usbstream 0;wait 500;$FX;usbstream 1;wait 300;$FX;wait 1500;$FX"
expect blips 1 "two 0.5 s bursts with a 0.5 s gap: never engaged, FX awake" "active 0 .*$D"
expect blips 2 "1.5 s of streaming: engaged" "active 1 .*$Z"
expect blips 3 "0.5 s gap: still engaged" "active 1 streaming 0 .*$Z"
expect blips 4 "the stream is back after the gap: still engaged, never woke" "active 1 streaming 2 .*$Z"
expect blips 5 "streaming on: still engaged" "active 1 .*$Z"

# flags: the user mutes Delay first, then the stream, keys, a save, the stream ends, the keys, a project open
run flags "$MC" "" "audio 1;wait 500;$NAVM;tap F1;wait 4000;$FX;usbstream 1;wait 1500;$FX;tap F1;tap F2;tap F2;wait 300;$FX;projsave Projects amsave;wait 300;$FX;usbstream 0;wait 1500;$FX;tap F2;wait 300;$FX;tap F2;tap F1;wait 300;$FX;projopen Projects amsave;wait 500;$FX"
expect flags 1 "Delay muted by the user (4 s ago): Delay asleep, Reverb awake" "active 0 streaming 0 \| flags delay 1 reverb 0 \| delay muted 1 asleep 1 active 0 \| reverb muted 0 asleep 0 active 1"
expect flags 2 "1.5 s of streaming: engaged, both asleep, the user's flags unchanged (1 0)" "active 1 .*flags delay 1 reverb 0 \| $Z"
expect flags 3 "F1 F2 F2 while engaged: locked, flags still 1 0" "active 1 .*flags delay 1 reverb 0 \| $Z"
expect flags 4 "save during streaming: flags still 1 0, still engaged" "active 1 .*flags delay 1 reverb 0 \| $Z"
expect flags 5 "stream ended 1.5 s: released, exactly the user's state: Delay muted and asleep, Reverb awake" "active 0 streaming 0 \| flags delay 1 reverb 0 \| delay muted 1 asleep 1 active 0 \| reverb muted 0 asleep 0 active 1 \| sendmix 0 1 \| labels delay 'Unmute' reverb 'Mute'"
expect flags 6 "after the release the Reverb key works again (F2: muted 1)" "active 0 .*flags delay 1 reverb 1 "
expect flags 7 "F2 and F1 again: both unmuted, both awake" "active 0 .*flags delay 0 reverb 0 \| $D"
expect flags 8 "the project saved during streaming opens with the user's flags (1 0), not the override" "flags delay 1 reverb 0 "

# off: fxmute 1 in the card file
run off "$MC" 'fxmute 1\n' "audio 1;wait 500;$NAVM;usbstream 1;wait 3000;$FX;tap F1;wait 300;$FX"
expect off 1 "FX in 8+3 Off: 3 s of streaming, never engaged, both FX awake" "enabled 0 active 0 streaming 2 \| flags delay 0 reverb 0 \| $D \| labels delay 'Mute'"
expect off 2 "FX in 8+3 Off: the Mute key works while streaming" "active 0 .*flags delay 1 reverb 0 "

# offlive: the page sets Off while the override is on (F8 opens the row, Down, F8 applies), then Auto mute again
NAVF='tap Config;wait 800;tap Down 3;wait 300;tap F8;wait 500;tap Down 1;wait 300'   # Config > USB (USB audio is on in the card's file) > FX in 8+3
run offlive "$MC" "" "audio 1;wait 300;usbstream 1;wait 1500;$FX;$NAVF;shot $SH/automute-row.png;tap F8;wait 400;tap Down;wait 200;tap F8;wait 600;$FX;wait 3000;$FX;tap F8;wait 400;tap Up;wait 200;tap F8;wait 600;$FX;wait 1500;$FX"
expect offlive 1 "engaged before the page is opened (default Auto mute)" "enabled 1 active 1 .*$Z"
expect offlive 2 "Off applied from the page: released at once, both FX awake, the stream still runs" "enabled 0 active 0 streaming 2 .*$D"
expect offlive 3 "3 s later: still not engaged" "enabled 0 active 0 streaming 2 .*$D"
expect offlive 4 "Auto mute again: enabled (the debounce starts over)" "enabled 1 active 0 streaming 2 "
expect offlive 5 "1.5 s later: engaged again" "enabled 1 active 1 streaming 2 .*$Z"
if [ "$(tr '\n' ' ' < "$WORK/offlive.sd/Features/usbaudio.cfg")" = "feature 1 id usbaudio version 1 on 1 " ]; then ok "offlive: Auto mute again leaves no fxmute line (the default is omitted)"; else bad "offlive: usbaudio.cfg is '$(tr '\n' ' ' < "$WORK/offlive.sd/Features/usbaudio.cfg")'"; fi

# the line is in the file while Off (a separate run that stops on Off)
run offfile "$MC" "" "audio 1;wait 300;$NAVF;tap F8;wait 400;tap Down;wait 200;tap F8;wait 600;wait 2500"
if tr '\n' ' ' < "$WORK/offfile.sd/Features/usbaudio.cfg" | grep -q "fxmute 1 "; then ok "offfile: Off writes the line 'fxmute 1'"; else bad "offfile: usbaudio.cfg is '$(tr '\n' ' ' < "$WORK/offfile.sd/Features/usbaudio.cfg")'"; fi

# stereo: Mode Stereo, the computer records: streaming 1, never engaged
run stereo "UISHOT_USB_FLAG=1 UISHOT_USB_MODE=0" "" "audio 1;wait 500;$NAVM;usbstream 1;wait 3000;$FX"
expect stereo 1 "Stereo mode: streaming 1, never engaged, both FX awake" "enabled 1 active 0 streaming 1 .*$D"

# norecord: 8+3, nothing records (the idle stream): nothing happens
run norecord "$MC" "" "audio 1;wait 500;$NAVM;wait 3000;$FX"
expect norecord 1 "8+3 without a recording host: nothing happens" "active 0 streaming 0 .*$D"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
