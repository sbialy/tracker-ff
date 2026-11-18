#!/bin/bash
# The Mixer's Mute of delay and reverb in the real engine on the Mac (fx_mute_gate.h): Master > Global Mixer 3/3, F1 (Delay) and F2 (Reverb)
# toggle the project flags, the audio engine follows them before update_all (audioEngine::update, MixerAndFxs::syncFxMute). Checks with the
# `poke fxmute` line (host_poke_fx.cpp): a muted FX keeps running while its tail rings (the 3 s cap), then it and its send mixer are out of
# update_all, unmuting one FX brings back only that FX and its send mixer. Host only, NOT firmware. Ends with RESULT: GREEN or RED.
#   synth-open/tools/ui_shot/fx_mute_sim.sh [UI_SHOT_BINARY [OUTDIR]]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
BIN=${1:-$ROOT/build/ui-shot/ui_shot}
O=${2:-$ROOT/build/fx-mute-sim}
[ -x "$BIN" ] || make -C "$HERE" -s > /dev/null || { echo "RESULT: RED (no ui_shot)"; exit 1; }
rm -rf "$O"; mkdir -p "$O/sd"
S="audio 1;wait 500;poke fxmute"
S="$S;tap Master;wait 800;tap Master;wait 800;tap Master;wait 800;shot $O/mixer-before.png"
S="$S;tap F1;tap F2;wait 1000;poke fxmute;shot $O/mixer-muted.png"   # 2: 1 s after the mute, the tails ring
S="$S;wait 3000;poke fxmute"                                           # 3: 4 s after, both asleep
S="$S;tap F1;wait 100;poke fxmute"                                     # 4: delay unmuted, reverb still asleep
S="$S;tap F2;wait 300;poke fxmute;shot $O/mixer-unmuted.png"            # 5: both back
UISHOT_POKE=$O/poke timeout 300 "$BIN" "$O/sd" "$S" > "$O/log.txt" 2>&1
echo "ui_shot exit $?" >> "$O/log.txt"
rc=0
check() {  # line expected label
  local got
  got=$(sed -n "${1}p" "$O/poke.out" | sed 's/^fxmute //')
  if [ "$got" = "$2" ]; then echo "PASS $3"; else echo "FAIL $3: got '$got', expected '$2'"; rc=1; fi
}
check 1 "flags delay 0 reverb 0 | delay muted 0 asleep 0 active 1 sendmix 1 | reverb muted 0 asleep 0 active 1 sendmix 1" "unmuted at start, both in update_all"
check 2 "flags delay 1 reverb 1 | delay muted 1 asleep 0 active 1 sendmix 1 | reverb muted 1 asleep 0 active 1 sendmix 1" "1 s after Mute: muted, the tails still ring"
check 3 "flags delay 1 reverb 1 | delay muted 1 asleep 1 active 0 sendmix 0 | reverb muted 1 asleep 1 active 0 sendmix 0" "4 s after Mute: FX and send mixers out of update_all"
check 4 "flags delay 0 reverb 1 | delay muted 0 asleep 0 active 1 sendmix 1 | reverb muted 1 asleep 1 active 0 sendmix 0" "delay unmuted: only the delay and its send mixer are back"
check 5 "flags delay 0 reverb 0 | delay muted 0 asleep 0 active 1 sendmix 1 | reverb muted 0 asleep 0 active 1 sendmix 1" "reverb unmuted: both back"
grep -q "ui_shot exit 0" "$O/log.txt" || { echo "FAIL ui_shot did not exit 0"; rc=1; }
[ $rc = 0 ] && echo "RESULT: GREEN" || echo "RESULT: RED"
exit $rc
