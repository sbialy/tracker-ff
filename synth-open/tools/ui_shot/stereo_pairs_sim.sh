#!/bin/bash
# stereo_pairs_sim.sh [BIN]
#
# Linked stereo track pairs (core/source/stereoPairs.h, round 1) on the real firmware code of this tree (ui_shot) on the Mac. Host only, no device.
# Pairs are 1+2 3+4 5+6 7+8 (bit k of the mask = pair k), project.mtff key track.N.pair on the odd track. Audio through the firmware's own callback
# (audio 1), read back per side (audiostat lrdump) or as a WAV (wav PATH, compared byte for byte).
#   1  an unlinked project renders byte identical to the base tip (BASE_BIN, the ui_shot of release-1.4.1-int b6982ccd), and `pair 0` after a link does too
#   2  a mono sample on a linked pair == the same sample on a centred mono track: byte identical WAV, also with a pan and both sends, also with all 4 pairs
#   3  a stereo sample on a linked pair: the left bus plays the file's left channel, the right bus the right channel, the pan law is a balance
#   4  a stereo sample on an unlinked track plays the left channel only, as before
#   5  the follower track of a pair plays nothing (its steps are kept), unlinking gives it back
#   6  the real loader: a stereo .pti on the card, project.mtff with and without the pair key (stereoSampleMode decided before the instruments load)
#   7  save and load of the pair keys in project.mtff (round trip, no key = no pair, a key on an even track or a value other than 1 is no pair)
#   8  round 2: a STREAMED stereo .pti on a pair plays L and R from the follower's idle ring (read past the 32,768 frame pre-cache, stream miss counter 0),
#      an unlinked streamed track is byte identical to the base tip, a streamed stereo sample on a track outside a linked pair plays left only
#   9  the amp LFO (random shape) of a pair: the L and R chain draw the same values
#   10 the Mixer (Master screen taps): Mute, Solo and Volume of either button of a pair act on the pair, both buses
#   11 the sample editor on a stereo instrument drops it to mono (isStereo off, address[1] = address[0]) after the Ok of the notice
#   12 Song Stems of a linked pair: one real L/R stem track1+2.wav, no file for the follower (round 3: no silent track2.wav), an unlinked song's stems equal the base tip
#   13 USB audio 8+3 on a linked pair: the follower's channel carries the R chain, every channel checked bit exact against its tap
#   15 round 3: the follower track is hidden in the pattern editor (label, reflow, cursor, selection, data survival)
#   16-18 round 4: D1 follower row FX, D2 preview address under a linked leader, Parts Build reflow
#   20 round 3: Config > MIDI hides the follower's Track N in channel row   21 the link notice scans every pattern of the song
#   14 goldens (goldens/stereopairs, STEREOPAIRS_UPDATE=1 rewrites): the pattern editor linked (follower hidden, 1+2 label) and unlinked, the Project Settings row, its list and editor, the ST mark
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-${UI_SHOT_BIN:-$ROOT/build/ui-shot/ui_shot}}"
BASE_BIN="${BASE_BIN:-}"
[ -x "$BIN" ] || { echo "stereo_pairs_sim.sh: no $BIN (make -C synth-open/tools/ui_shot builds it)" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/stereo_pairs_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = "ok" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
# wav BIN NAME SETUP: one render of 1500 ms of Play into $WORK/NAME.wav
wav() {
    local sd="$WORK/sd_$2"; rm -rf "$sd"
    ASAN_OPTIONS=detect_leaks=0 "$1" "$sd" "wait 500; $3 audio 1; wav $WORK/$2.wav; tap Play; wait 1500; tap Play; wav off" >/dev/null 2>"$WORK/$2.err"
}
same() { if cmp -s "$WORK/$1.wav" "$WORK/$2.wav" && [ -s "$WORK/$1.wav" ]; then check "$3" ok; else check "$3" "$1.wav and $2.wav differ ($(cmp "$WORK/$1.wav" "$WORK/$2.wav" 2>&1 | head -1))"; fi; }
# lr SETUP -> "rmsL rmsR zcL zcR" (means over blocks 20..80, zero crossings per block)
lr() {
    local sd="$WORK/sd_lr"; rm -rf "$sd"
    ASAN_OPTIONS=detect_leaks=0 "$BIN" "$sd" "wait 500; $1 audio 1; audiostat 1; tap Play; wait 1500; tap Play; audiostat 0; audiostat lrdump" 2>/dev/null | grep '^lr ' |
        awk -v a="${LRA:-20}" -v b="${LRB:-80}" 'NR>a&&NR<b{l+=$4;r+=$6;zl+=$8;zr+=$10;n++}END{printf "%.0f %.0f %.1f %.1f\n",l/n,r/n,zl/n,zr/n}'
}
# wavdiff A B -> "count max": the samples that differ and the largest difference in LSB of 16 bit
wavdiff() { python3 -I - "$WORK/$1.wav" "$WORK/$2.wav" <<'PY'
import sys, struct
a = open(sys.argv[1], 'rb').read()[44:]; b = open(sys.argv[2], 'rb').read()[44:]
n = min(len(a), len(b)) // 2
x = struct.unpack('<%dh' % n, a[:n * 2]); y = struct.unpack('<%dh' % n, b[:n * 2])
d = [abs(p - q) for p, q in zip(x, y)]
print(sum(1 for v in d if v), max(d) if d else 0, len(a) == len(b) and n > 0)
PY
}
# within A B MAXLSB NAME: the sends of a pair are two half gain inputs of a mono mixer instead of one full gain input, so the tails of delay and reverb may differ by rounding
within() { local out; out=$(wavdiff "$1" "$2"); set -- $out "$3" "$4"
    local cnt=$1 mx=$2 ok=$3 lim=$4 name=$5
    if [ "$ok" = True ] && [ "$mx" -le "$lim" ]; then check "$name (max difference $mx LSB, $cnt samples differ)" ok; else check "$name" "max difference $mx LSB (limit $lim), $cnt samples differ, same length $ok"; fi; }
near() { awk -v a="$1" -v b="$2" -v t="$3" 'BEGIN{d=a-b; if(d<0)d=-d; exit !(d<=t)}'; }
# expectation on the four numbers: "rmsL rmsR zcL zcR"
expect_lr() {   # NAME GOT "rmsLmin rmsLmax rmsRmin rmsRmax zcL zcR"
    set -- "$1" $2 $3
    local name="$1" rl="$2" rr="$3" zl="$4" zr="$5"; shift 5
    if [ "$rl" -ge "$1" ] && [ "$rl" -le "$2" ] && [ "$rr" -ge "$3" ] && [ "$rr" -le "$4" ] && near "$zl" "$5" 0.7 && near "$zr" "$6" 0.7; then check "$name" ok
    else check "$name" "got rmsL $rl rmsR $rr zcL $zl zcR $zr, want rmsL $1..$2 rmsR $3..$4 zcL $5 zcR $6"; fi
}

FOUR="setstep 0 0 60 0 0 0 0 0; setstep 2 4 64 1 0 0 0 0; setstep 4 8 67 2 0 0 0 0; setstep 6 12 71 3 0 0 0 0;"
TONES="sampletonehz 0 441 8000; sampletonehz 1 300 6000; sampletonehz 2 523 7000; sampletonehz 3 700 5000;"
SONG="$TONES instr 0; $FOUR"
SONGFX="$TONES setstep 0 0 60 0 31 20 30 80; setstep 1 2 62 1 32 90 31 70; setstep 2 4 64 2 30 40 32 60; setstep 3 6 67 3 31 100 0 0; setstep 5 8 55 1 31 0 32 50; setstep 7 10 50 2 0 0 0 0;"

echo "### 1: unlinked == the base tip, and unlinking restores it"
if [ -n "$BASE_BIN" ] && [ -x "$BASE_BIN" ]; then
    wav "$BASE_BIN" base1 "$SONG"; wav "$BIN" new1 "$SONG"; same base1 new1 "unlinked 4 track song: new firmware == base tip (byte identical WAV)"
    wav "$BASE_BIN" base2 "$SONGFX"; wav "$BIN" new2 "$SONGFX"; same base2 new2 "unlinked 8 track song with pan and both sends: new firmware == base tip"
else
    echo "skip  BASE_BIN not set: the compare with the base tip is skipped (set BASE_BIN to the ui_shot of release-1.4.1-int b6982ccd)"
fi
wav "$BIN" new3 "$SONGFX"
wav "$BIN" lnk3 "pair 15; pair 0; $SONGFX"
same new3 lnk3 "link all four pairs then unlink them: the render is the unlinked one"

echo "### 2: a mono sample on a linked pair == a centred mono track"
M1="sampletonehz 0 441 8000; instr 0; setstep 0 0 60 0 0 0 0 0;"
wav "$BIN" m_off "$M1"; wav "$BIN" m_on "pair 1; $M1"
same m_off m_on "pair 1 mono sample, default pan: byte identical to the mono track"
M2="sampletonehz 0 441 8000; setstep 0 0 60 0 31 20 0 0; setstep 0 6 64 0 31 80 0 0;"
wav "$BIN" mp_off "$M2"; wav "$BIN" mp_on "pair 1; $M2"
same mp_off mp_on "pair 1 mono sample, pan 20 and 80: byte identical to the mono track"
M3="sampletonehz 0 441 8000; setstep 0 0 60 0 30 80 0 0; setstep 0 6 64 0 32 90 0 0;"
wav "$BIN" ms_off "$M3"; wav "$BIN" ms_on "pair 1; $M3"
within ms_off ms_on 16 "pair 1 mono sample, delay and reverb sends (two half gain inputs)"
SONGODD="$TONES setstep 0 0 60 0 31 20 0 0; setstep 2 4 64 2 31 70 0 0; setstep 4 8 55 1 31 0 0 0; setstep 6 10 50 2 31 90 0 0;"
wav "$BIN" ma_off "$SONGODD"; wav "$BIN" ma_on "pair 15; $SONGODD"
same ma_off ma_on "all four pairs, mono samples on tracks 1 3 5 7 with pans: byte identical to the unlinked song"
SONGSND="$TONES setstep 0 0 60 0 31 20 30 80; setstep 2 4 64 2 30 40 32 60; setstep 4 8 55 1 31 0 32 50; setstep 6 10 50 2 31 90 30 20;"
wav "$BIN" mb_off "$SONGSND"; wav "$BIN" mb_on "pair 15; $SONGSND"
within mb_off mb_on 16 "all four pairs with pans and both sends"

echo "### 3: a stereo sample on a linked pair (L 441 Hz, R 882 Hz, 8000)"
ST="samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 0 0 0 0;"
expect_lr "pair 1, centre: L bus 441 Hz, R bus 882 Hz, both at full level" "$(lr "pair 1; $ST")" "20000 24000 20000 24000 2.6 5.1"
expect_lr "pair 1, pan 0 (hard left): the R bus is silent, the L bus keeps the left channel" "$(lr "pair 1; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 31 0 0 0;")" "20000 24000 0 100 2.6 0"
expect_lr "pair 1, pan 100 (hard right): the L bus is silent, the R bus keeps the right channel" "$(lr "pair 1; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 31 100 0 0;")" "0 100 20000 24000 0 5.1"
expect_lr "pair 1, pan 25: the R bus is attenuated, the L bus unchanged (today's balance law, same as the mono track)" "$(lr "pair 1; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 31 25 0 0;")" "20000 24000 9000 15000 2.6 5.1"
expect_lr "pair 4 (tracks 7+8): the same stereo sample" "$(lr "pair 8; samplestereo 0 441 882 8000; instr 0; setstep 6 0 60 0 0 0 0 0;")" "20000 24000 20000 24000 2.6 5.1"

echo "### 4: a stereo sample on an unlinked track plays the left channel only, as before"
expect_lr "unlinked: both buses carry the left channel (441 Hz)" "$(lr "$ST")" "20000 24000 20000 24000 2.6 2.6"
expect_lr "pair 2 linked, the sample on track 1 (not in a pair): the left channel only" "$(lr "pair 2; $ST")" "20000 24000 20000 24000 2.6 2.6"

echo "### 5: the follower plays nothing, unlinking gives it back"
FOL="sampletonehz 1 1500 8000; setstep 1 0 60 1 0 0 0 0; setstep 1 4 62 1 0 0 0 0;"
wav "$BIN" f_off "pair 1; $ST"; wav "$BIN" f_on "pair 1; $ST $FOL"
same f_off f_on "a follower with steps is silent: byte identical to the pair without them"
expect_lr "follower steps on a stereo pair: L 441 Hz and R 882 Hz stay clean" "$(lr "pair 1; $ST $FOL")" "20000 24000 20000 24000 2.6 5.1"
UNL="sampletonehz 0 441 8000; sampletonehz 1 1500 8000; instr 0; setstep 0 0 60 0 0 0 0 0; setstep 1 0 60 1 0 0 0 0;"
wav "$BIN" u_ref "$UNL"; wav "$BIN" u_on "pair 1; pair 0; $UNL"
same u_ref u_on "link, unlink: the follower track sounds again, byte identical to a project that never linked"

echo "### 6: the real loader, a stereo .pti on the card"
sd="$WORK/sd_pti"; rm -rf "$sd"
"$BIN" "$sd" "wait 500; samplestereo 0 441 882 8000 save; instr 0; setstep 0 0 60 0 0 0 0 0; dirty; wait 8000" >/dev/null 2>&1
size=$(stat -f %z "$sd/Workspace/instruments/instrument_01.pti" 2>/dev/null || echo 0)
check "the saved .pti holds both channels (head 392 + 4 bytes per frame)" "$([ "$size" -eq $((392 + 44100*4*4)) ] && echo ok || echo "size $size")"
cardrun() { rm -f "$WORK/poke.out"; ASAN_OPTIONS=detect_leaks=0 UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "wait 500; poke pairs; audio 1; audiostat 1; tap Play; wait 1500; tap Play; audiostat 0; audiostat lrdump" 2>/dev/null | grep '^lr ' |
    awk 'NR>20&&NR<80{l+=$4;r+=$6;zl+=$8;zr+=$10;n++}END{printf "%.0f %.0f %.1f %.1f\n",l/n,r/n,zl/n,zr/n}'; }
got="$(cardrun)"
check "no pair key: stereoSampleMode off, the loader keeps the left channel" "$(grep -q '^pairs 0 stereo 0' "$WORK/poke.out" && echo ok || cat "$WORK/poke.out")"
expect_lr "no pair key: the stereo .pti plays left only on both buses" "$got" "20000 24000 20000 24000 2.6 2.6"
echo "track.1.pair 1" >>"$sd/Workspace/project.mtff"
got="$(cardrun)"
check "pair key: the project load switched stereoSampleMode on before the instruments" "$(grep -q '^pairs 1 stereo 1' "$WORK/poke.out" && echo ok || cat "$WORK/poke.out")"
expect_lr "pair key: the stereo .pti plays L 441 Hz on the left bus and R 882 Hz on the right bus" "$got" "20000 24000 20000 24000 2.6 5.1"

echo "### 7: the pair keys in project.mtff"
sd="$WORK/sd_keys"; rm -rf "$sd"
"$BIN" "$sd" "wait 500; pair 5; dirty; wait 4000" >/dev/null 2>&1
check "save: the mask 5 is the lines track.1.pair 1 and track.5.pair 1" "$([ "$(grep '\.pair' "$sd/Workspace/project.mtff" | tr '\n' '|')" = "track.1.pair 1|track.5.pair 1|" ] && echo ok || cat "$sd/Workspace/project.mtff")"
rm -f "$WORK/poke.out"; UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "wait 500; poke pairs" >/dev/null 2>&1
check "load: the engine runs mask 5 again" "$(grep -q '^pairs 5 stereo 1' "$WORK/poke.out" && echo ok || cat "$WORK/poke.out")"
rm -f "$sd/Workspace/project.mtff"; rm -f "$WORK/poke.out"; UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "wait 500; poke pairs" >/dev/null 2>&1
check "no project.mtff: no pair" "$(grep -q '^pairs 0 stereo 0' "$WORK/poke.out" && echo ok || cat "$WORK/poke.out")"
printf 'tffproj 1\ntrack.2.pair 1\ntrack.3.pair 2\ntrack.9.pair 1\ntrack.7.pair 1\n' >"$sd/Workspace/project.mtff"; rm -f "$WORK/poke.out"; UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "wait 500; poke pairs" >/dev/null 2>&1
check "a key on an even track, a value other than 1 and a track past 8 link nothing, track.7.pair links pair 4" "$(grep -q '^pairs 8 stereo 1' "$WORK/poke.out" && echo ok || cat "$WORK/poke.out")"
printf 'tffproj 9\ntrack.1.pair 1\n' >"$sd/Workspace/project.mtff"; rm -f "$WORK/poke.out"; UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "wait 500; poke pairs" >/dev/null 2>&1
check "a file of a newer format links nothing" "$(grep -q '^pairs 0 stereo 0' "$WORK/poke.out" && echo ok || cat "$WORK/poke.out")"
printf 'tffproj 1\nstrum.ramp 40\ntrack.1.pair 1\nfoo.bar 1\n' >"$sd/Workspace/project.mtff"
"$BIN" "$sd" "wait 500; poke strumdepth; dirty; wait 4000" >/dev/null 2>&1
check "a rewrite keeps the pair key and the unknown line" "$([ "$(grep -c 'track.1.pair 1\|foo.bar 1' "$sd/Workspace/project.mtff")" = 2 ] && echo ok || cat "$sd/Workspace/project.mtff")"

echo "### 8: streamed stereo on a pair (the follower's idle ring), round 2"
# a 4 s stereo .pti (176,400 frames, streamed: the pre-cache holds 32,768 frames = 256 blocks, the window below is read past it)
sd="$WORK/sd_str"; rm -rf "$sd"
"$BIN" "$sd" "wait 500; samplestereo 0 441 882 8000 save stream; instr 0; setstep 0 0 60 0 0 0 0 0; dirty; wait 8000" >/dev/null 2>&1
strun() {   # STRUN CARD SCRIPT -> "rmsL rmsR zcL zcR", the miss counters in $WORK/miss.txt and instrinfo in $WORK/info.txt
    ASAN_OPTIONS=detect_leaks=0 "$BIN" "$1" "wait 500; instrinfo 0; vsmiss reset; audio 1; audiostat 1; ${2:-} tap Play; wait 2500; tap Play; audiostat 0; vsmiss; audiostat lrdump" 2>/dev/null >"$WORK/strun.txt"
    grep '^vsmiss' "$WORK/strun.txt" >"$WORK/miss.txt"; grep '^instrinfo' "$WORK/strun.txt" >"$WORK/info.txt"
    grep '^lr ' "$WORK/strun.txt" | awk -v a="${LRA:-400}" -v b="${LRB:-600}" 'NR>a&&NR<b{l+=$4;r+=$6;zl+=$8;zr+=$10;n++}END{printf "%.0f %.0f %.1f %.1f\n",l/n,r/n,zl/n,zr/n}'
}
cp -R "$sd" "$WORK/sd_str_pair"; cp -R "$sd" "$WORK/sd_str_p2"
echo "track.1.pair 1" >>"$WORK/sd_str_pair/Workspace/project.mtff"
got="$(strun "$WORK/sd_str_pair")"
check "streamed pair: the instrument streams (instrinfo stream 1)" "$(grep -q 'stream 1' "$WORK/info.txt" && echo ok || cat "$WORK/info.txt")"
expect_lr "streamed pair, blocks 400-600 (1.2-1.7 s, past the pre-cache): L bus 441 Hz, R bus 882 Hz" "$got" "20000 24000 20000 24000 2.6 5.1"
check "streamed pair: stream miss counter 0 0 on the host (L R)" "$([ "$(cat "$WORK/miss.txt")" = "vsmiss 0 0" ] && echo ok || cat "$WORK/miss.txt")"
got="$(strun "$WORK/sd_str")"
expect_lr "streamed, no pair key: the left channel on both buses (as the base tip)" "$got" "20000 24000 20000 24000 2.6 2.6"
check "streamed, no pair key: stream miss counter 0 0" "$([ "$(cat "$WORK/miss.txt")" = "vsmiss 0 0" ] && echo ok || cat "$WORK/miss.txt")"
echo "track.3.pair 1" >>"$WORK/sd_str_p2/Workspace/project.mtff"
got="$(strun "$WORK/sd_str_p2")"
expect_lr "streamed sample on track 1 while only pair 2 is linked: left channel only (the voice guard, no second ring)" "$got" "20000 24000 20000 24000 2.6 2.6"
check "streamed on a track outside a linked pair: stream miss counter 0 0" "$([ "$(cat "$WORK/miss.txt")" = "vsmiss 0 0" ] && echo ok || cat "$WORK/miss.txt")"
if [ -n "$BASE_BIN" ] && [ -x "$BASE_BIN" ]; then
    for w in base new; do rm -rf "$WORK/sd_sw_$w"; cp -R "$sd" "$WORK/sd_sw_$w"; done
    ASAN_OPTIONS=detect_leaks=0 "$BASE_BIN" "$WORK/sd_sw_base" "wait 500; audio 1; wav $WORK/str_base.wav; tap Play; wait 2500; tap Play; wav off" >/dev/null 2>&1
    ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_sw_new" "wait 500; audio 1; wav $WORK/str_new.wav; tap Play; wait 2500; tap Play; wav off" >/dev/null 2>&1
    same str_base str_new "unlinked streamed track: new firmware == base tip (byte identical WAV, 2.5 s of a streamed stereo .pti)"
fi

echo "### 9: the amp LFO of a pair (random shape)"
LFO="pair 1; sampletonehz 0 441 8000; instr 0; amprandom 0 60; setstep 0 0 60 0 0 0 0 0;"
wav "$BIN" lfo "$LFO"
res=$(python3 -I - "$WORK/lfo.wav" <<'PY'
import sys, struct
d = open(sys.argv[1], 'rb').read()[44:]
n = len(d) // 4
x = struct.unpack('<%dh' % (n * 2), d[:n * 4])
L = x[0::2]; R = x[1::2]
same = sum(1 for a, b in zip(L, R) if a == b)
blk = [max(abs(v) for v in L[i:i + 441]) for i in range(0, min(n, 44100), 441)]
print(same == n and n > 0, max(blk) - min(blk) if blk else 0)
PY
)
check "pair 1 mono sample with the amp random LFO: the L and R buses are identical (same draws)" "$([ "${res%% *}" = True ] && echo ok || echo "L != R ($res)")"
check "the LFO really modulates the amplitude (10 ms peaks vary by more than 500)" "$([ "${res##* }" -gt 500 ] && echo ok || echo "spread ${res##* }")"

echo "### 10: Mixer Mute, Solo and Volume act on the pair (Master screen, F1..F8 are the strips, a linked pair is one strip and the follower has none)"
MXH="wait 500; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 0 0 0 0; audio 1; tap Master; wait 400; tap Master; wait 400;"
mx() {   # SETUP MIXERTAPS -> "rmsL rmsR zcL zcR"
    rm -rf "$WORK/sd_mx"
    ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_mx" "$MXH $1; wait 300; audiostat 1; tap Play; wait 1500; tap Play; audiostat 0; audiostat lrdump" 2>/dev/null | grep '^lr ' |
        awk 'NR>20&&NR<80{l+=$4;r+=$6;zl+=$8;zr+=$10;n++}END{printf "%.0f %.0f %.1f %.1f\n",l/n,r/n,zl/n,zr/n}'
}
expect_lr "Mixer, pair 1 untouched: L 441 Hz, R 882 Hz" "$(mx "pair 1; ")" "20000 24000 20000 24000 2.6 5.1"
expect_lr "Mixer Mute on the leader's button F1: both buses silent" "$(mx "pair 1; tap F1")" "0 5 0 5 0 0"
expect_lr "Mixer: the follower has no strip and no button, F2 is the strip of track 3 (Mute on it leaves the pair playing)" "$(mx "pair 1; tap F2")" "20000 24000 20000 24000 2.6 5.1"
expect_lr "Mixer Mute twice on the leader's button F1: the pair plays again" "$(mx "pair 1; tap F1; tap F1")" "20000 24000 20000 24000 2.6 5.1"
expect_lr "Mixer: with one pair linked there are 7 strips, F8 has none (Mute on F8 leaves the pair playing)" "$(mx "pair 1; tap F8")" "20000 24000 20000 24000 2.6 5.1"
expect_lr "Mixer Volume on F2 (the strip of track 3, hold F2, encoder -30) leaves the pair alone" "$(mx "pair 1; hold F2; enc -30; unhold F2")" "20000 24000 20000 24000 2.6 5.1"
expect_lr "Mixer Volume on the leader's button gives the same level" "$(mx "pair 1; hold F1; enc -30; unhold F1")" "8000 12000 8000 12000 2.6 5.1"
MXS="wait 500; pair 3; samplestereo 0 441 882 8000; sampletonehz 1 300 8000; instr 0; setstep 0 0 60 0 0 0 0 0; setstep 2 0 60 1 0 0 0 0; audio 1; tap Master; wait 400; tap Master; wait 400;"
mxs() { rm -rf "$WORK/sd_mx"; ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_mx" "$MXS $1; wait 300; audiostat 1; tap Play; wait 1500; tap Play; audiostat 0; audiostat lrdump" 2>/dev/null | grep '^lr ' |
        awk 'NR>20&&NR<80{l+=$4;r+=$6;zl+=$8;zr+=$10;n++}END{printf "%.0f %.0f %.1f %.1f\n",l/n,r/n,zl/n,zr/n}'; }
expect_lr "Mixer Solo on F1 (Shift + F1): only pair 1 sounds (L 441 Hz, R 882 Hz)" "$(mxs "hold Shift; tap F1; unhold Shift")" "20000 24000 20000 24000 2.6 5.1"
expect_lr "Mixer Solo on F2 (Shift + F2, the strip of pair 2 because the follower of pair 1 has none): only pair 2 sounds (its mono sample, 300 Hz on both buses)" "$(mxs "hold Shift; tap F2; unhold Shift")" "20000 24000 20000 24000 1.8 1.8"

SEF=$(ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_flag" "editflags" 2>/dev/null | sed -n 's/^editflags stereoedit //p')
if [ "$SEF" = 1 ]; then
echo "### 11: the sample editor keeps a stereo instrument stereo (FEATURE_STEREO_EDIT, stereo_edit_sim.sh has the effects)"
ED="wait 500; pair 1; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 0 0 0 0; tap SampleEdit; wait 600;"
rm -rf "$WORK/sd_ed"; ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ed" "$ED instrinfo 0; tap Pattern; wait 600; instrinfo 0" 2>/dev/null | grep '^instrinfo . stereo' >"$WORK/ed.txt"
check "the editor opened: the instrument is still stereo (R != L), before and after leaving it" "$([ "$(sed -n 1p "$WORK/ed.txt")" = "instrinfo 0 stereo 1 rightIsLeft 0" ] && [ "$(sed -n 2p "$WORK/ed.txt")" = "instrinfo 0 stereo 1 rightIsLeft 0" ] && echo ok || cat "$WORK/ed.txt")"
expect_lr "after the editor round trip the pair still plays L 441 Hz and R 882 Hz" "$(lr "pair 1; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 0 0 0 0; tap SampleEdit; wait 600; tap Pattern; wait 300;")" "20000 24000 20000 24000 2.6 5.1"
else
echo "### 11: the sample editor on a stereo instrument drops it to mono"
ED="wait 500; pair 1; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 0 0 0 0; tap SampleEdit; wait 600;"
rm -rf "$WORK/sd_ed"; ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ed" "$ED instrinfo 0; tap F8; wait 800; instrinfo 0" 2>/dev/null | grep '^instrinfo . stereo' >"$WORK/ed.txt"
check "before the Ok of the notice: the instrument is stereo (R != L)" "$([ "$(sed -n 1p "$WORK/ed.txt")" = "instrinfo 0 stereo 1 rightIsLeft 0" ] && echo ok || cat "$WORK/ed.txt")"
check "after the Ok: isStereo off and address[1] == address[0]" "$([ "$(sed -n 2p "$WORK/ed.txt")" = "instrinfo 0 stereo 0 rightIsLeft 1" ] && echo ok || cat "$WORK/ed.txt")"
expect_lr "after the drop the pair plays the left channel on both buses (the mono sample on a pair)" "$(lr "pair 1; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 0 0 0 0; tap SampleEdit; wait 600; tap F8; wait 800; tap Pattern; wait 300;")" "20000 24000 20000 24000 2.6 2.6"
fi

echo "### 12: Song Stems of a linked pair"
stems() {   # NAME BIN SETUP: songstems into $WORK/stems_NAME
    local sd="$WORK/sd_stems_$1"; rm -rf "$sd"
    ASAN_OPTIONS=detect_leaks=0 "$2" "$sd" "wait 500; $3 plen 16; playlist 1; export songstems; wait 3000" >/dev/null 2>&1
    rm -rf "$WORK/stems_$1"; mkdir -p "$WORK/stems_$1"; cp "$sd"/Export/*/*/track*.wav "$WORK/stems_$1/" 2>/dev/null
}
stems pair "$BIN" "pair 1; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 0 0 0 0; setstep 1 0 62 0 0 0 0 0;"
res=$(python3 -I - "$WORK/stems_pair/track1+2.wav" "$WORK/stems_pair/track3.wav" <<'PY'
import sys, struct, wave
def ch(p):
    w = wave.open(p); n = w.getnframes(); x = struct.unpack('<%dh' % (n * w.getnchannels()), w.readframes(n))
    return x[0::2], x[1::2]
def zc(s): return sum(1 for i in range(1, len(s)) if (s[i - 1] < 0) != (s[i] < 0))
l, r = ch(sys.argv[1]); fl, fr = ch(sys.argv[2])
print(zc(l), zc(r), len(l), len(fl))
PY
)
set -- $res
check "stems track1+2.wav: left channel 441 Hz and right channel 882 Hz (zero crossings $1 / $2 of $3 frames)" "$([ "$1" -gt 2300 ] && [ "$1" -lt 2700 ] && [ "$2" -gt 4800 ] && [ "$2" -lt 5200 ] && echo ok || echo "zc $1 $2")"
check "stems: the unlinked track 3 stem has the same length as the pair stem ($3 / $4 frames)" "$([ "$3" = "$4" ] && [ "$3" -gt 0 ] && echo ok || echo "length $3 $4")"
check "stems: no file for the follower (no track2.wav, no silent one), tracks 3-8 keep theirs: $(cd "$WORK/stems_pair" && ls | tr '\n' ' ')" "$([ ! -e "$WORK/stems_pair/track2.wav" ] && [ ! -e "$WORK/stems_pair/track1.wav" ] && [ "$(ls "$WORK/stems_pair" | wc -l | tr -d ' ')" = 7 ] && echo ok || ls "$WORK/stems_pair")"
if [ -n "$BASE_BIN" ] && [ -x "$BASE_BIN" ]; then
    US="$TONES setstep 0 0 60 0 31 20 0 0; setstep 1 2 62 1 0 0 0 0; setstep 2 4 64 2 0 0 0 0; setstep 3 6 67 3 0 0 0 0;"
    stems ub "$BASE_BIN" "$US"; stems un "$BIN" "$US"
    d=0; for f in 1 2 3 4; do cmp -s "$WORK/stems_ub/track$f.wav" "$WORK/stems_un/track$f.wav" && [ -s "$WORK/stems_un/track$f.wav" ] || d=$((d+1)); done
    check "unlinked song: the stem files of tracks 1-4 are byte identical to the base tip" "$([ "$d" = 0 ] && echo ok || echo "$d of 4 stems differ")"
fi

echo "### 13: USB audio 8+3 on a linked pair"
usbrun() { rm -rf "$WORK/sd_usb"; env UISHOT_USB_FLAG=1 UISHOT_USB_MODE=1 ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_usb" "wait 500; usbmc sat 0; $1 audio 1; usbmc 1; tap Play; wait 3000; tap Play; wait 200; usbmc dump" 2>/dev/null | grep '^usbmc' >"$WORK/usb.txt"; }
ufield() { sed -nE "s/^usbmc track $1 .*$2 ([0-9]+).*/\1/p" "$WORK/usb.txt" | head -1; }
usbrun "pair 1; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 0 0 0 0; sampletonehz 2 495 300; setstep 2 0 60 2 0 0 0 0;"
check "pair 1 on USB: channels 1 and 2 (leader L chain, follower R chain) both carry sound" "$([ "$(ufield 1 live)" -gt 900 ] && [ "$(ufield 2 live)" -gt 900 ] && echo ok || cat "$WORK/usb.txt")"
check "pair 1 on USB: the L and R channels differ (the stereo sample), each bit exact against its tap" "$([ "$(ufield 1 panned)" -gt 900 ] && [ "$(ufield 1 bad)" = 0 ] && [ "$(ufield 2 bad)" = 0 ] && echo ok || cat "$WORK/usb.txt")"
check "pair 1 on USB: the unlinked track 3 stays bit exact and tracks 4-8 are silent" "$([ "$(ufield 3 live)" -gt 900 ] && [ "$(ufield 3 bad)" = 0 ] && [ "$(ufield 4 live)" = 0 ] && echo ok || cat "$WORK/usb.txt")"

echo "### 15: the follower track is hidden in the pattern editor (labels, reflow, cursor, selection, data survival)"
VIEWRUN() {   # NAME "script": the view / cursor / step lines of the script
    rm -rf "$WORK/sd_$1"; ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_$1" "wait 500; $2" 2>/dev/null | grep -E '^(view|cursor|step) '
}
RELOAD="tap SampleEdit; wait 300; tap Pattern; wait 400;"   # the pattern editor starts anew, as it does after Project Settings
NOTES="sampletonehz 0 441 8000; instr 0; setstep 0 0 60 0 0 0 0 0; setstep 1 0 62 0 0 0 0 0; setstep 2 0 64 0 0 0 0 0; setstep 3 0 65 0 0 0 0 0; setstep 1 5 67 0 31 20 0 0;"
out=$(VIEWRUN v1 "$NOTES pair 3; $RELOAD view; pair 0; $RELOAD view")
check "linked pairs 1+2 and 3+4: the followers are hidden (mask 0xa) and the leaders are labelled 1+2 and 3+4" "$([ "$(echo "$out" | sed -n 1p | sed -E 's/.*hidden (0x[0-9a-f]+) names (.*) sel.*/\1 \2/')" = "0xa 1+2 Track 2 3+4 Track 4" ] && echo ok || echo "$out" | sed -n 1p)"
check "unlinked again: nothing hidden and the track names are back" "$([ "$(echo "$out" | sed -n 2p | sed -E 's/.*hidden (0x[0-9a-f]+) names (.*) sel.*/\1 \2/')" = "0x0 Track 1 Track 2 Track 3 Track 4" ] && echo ok || echo "$out" | sed -n 2p)"
out=$(VIEWRUN v2 "pair 3; $RELOAD cursor; tap Right; cursor; tap Right; cursor; tap Left; cursor; tap Left; cursor; tap Left; cursor")
got=$(echo "$out" | sed -E 's/cursor track ([0-9]+) .*/\1/' | tr '\n' ' ')
check "cursor Right skips the hidden followers and Left too (tracks 1 3 5 7 are never entered: 0 2 4 2 0 0, got $got)" "$([ "$got" = "0 2 4 2 0 0 " ] && echo ok || echo "$got")"
out=$(VIEWRUN v3 "pair 8; $RELOAD tap Right; tap Right; tap Right; tap Right; tap Right; view; cursor")
check "pair 4 (7+8) linked: the cursor walks 0 1 2 3 4 5 6 then 8 (track 8 is the hidden follower), first visible track reflows (got: $(echo "$out" | sed -n 2p))" "$([ "$(echo "$out" | sed -n 2p)" = "cursor track 5 step 0 param 0 edit 0" ] && echo ok || echo "$out" | sed -n 2p)"
out=$(VIEWRUN v4 "$NOTES pair 3; $RELOAD tap Rec; hold Shift; tap Right; tap Right; tap Right; unhold Shift; wait 200; view; tap Delete; wait 300; step 0 0; step 1 0; step 2 0; step 3 0; step 1 5; pair 0; step 1 0; step 3 0; step 1 5")
check "a selection over two linked pairs ends on track 6 (0..5, followers skipped)" "$([ "$(echo "$out" | sed -n 1p | sed -E 's/.*sel (.*)/\1/')" = "2 0..5" ] && echo ok || echo "$out" | sed -n 1p)"
check "Delete on that selection clears the leaders 1 and 3 and leaves the followers' steps alone" "$([ "$(echo "$out" | sed -n 2p | grep -c 'note -1')$(echo "$out" | sed -n 4p | grep -c 'note -1')$(echo "$out" | sed -n 3p | grep -c 'note 62')$(echo "$out" | sed -n 5p | grep -c 'note 65')" = "1111" ] && echo ok || echo "$out")"
check "the follower's step with an FX survives link, edit and unlink untouched (fx 31:20 on step 5)" "$([ "$(echo "$out" | sed -n 6p)" = "step 1 5 note 67 instr 0 fx0 31:20 fx1 0:0" ] && [ "$(echo "$out" | sed -n 7p)" = "step 1 0 note 62 instr 0 fx0 0:0 fx1 0:0" ] && [ "$(echo "$out" | sed -n 8p)" = "step 3 0 note 65 instr 0 fx0 0:0 fx1 0:0" ] && echo ok || echo "$out" | sed -n 6,9p)"

echo "### 16: round 2 D1: step FX on the follower row do nothing while the pair is linked"
D1S="pair 1; samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 0 0 0 0; setstep 0 2 60 0 0 0 0 0;"
wav "$BIN" d1_base "$D1S"
wav "$BIN" d1_tune "$D1S setstep 1 0 -1 0 40 100 0 0; setstep 1 2 -1 0 40 100 0 0;"
same d1_base d1_tune "a note-less Tune FX on the follower row leaves the pair's render byte identical (it moved only R before the gate)"
wav "$BIN" d1_lp "$D1S setstep 1 0 -1 0 27 40 0 0;"
same d1_base d1_lp "a note-less Low pass FX on the follower row: byte identical"
wav "$BIN" d1_f2 "$D1S setstep 1 0 -1 0 0 0 40 100;"
same d1_base d1_f2 "the same Tune in the follower row's FX slot 2: byte identical"
wav "$BIN" d1_on "$D1S setstep 0 0 60 0 40 100 0 0;"
wav "$BIN" d1_off "$D1S setstep 0 0 60 0 0 0 0 0;"
check "control: the same Tune on the LEADER row still changes the render" "$(cmp -s "$WORK/d1_on.wav" "$WORK/d1_off.wav" && echo "no change, the control proves nothing" || echo ok)"

echo "### 17: round 2 D2: the sample editor effect preview on a linked pair (one preview address, two chains)"
rm -rf "$WORK/sd_d2"; ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_d2" "wait 500; pair 1; sampletonehz 0 441 8000; instr 0; tap SampleEdit; wait 600; tap F6; wait 500; audio 1; tap F7; wait 1200; tap F7; wait 300" >"$WORK/d2.out" 2>&1
rc=$?
check "Preview with pair 1 linked: no assert, no crash (rc $rc; the unfixed tree reads address[1] of a one element vector)" "$([ "$rc" = 0 ] && ! grep -q 'Assertion\|signal\|AddressSanitizer' "$WORK/d2.out" && echo ok || head -4 "$WORK/d2.out")"
if [ -n "${SAN_BIN:-}" ] && [ -x "$SAN_BIN" ]; then
    rm -rf "$WORK/sd_d2s"; ASAN_OPTIONS=detect_leaks=0 "$SAN_BIN" "$WORK/sd_d2s" "wait 500; pair 1; sampletonehz 0 441 8000; instr 0; tap SampleEdit; wait 600; tap F6; wait 500; audio 1; tap F7; wait 1200; tap F7; wait 300" >"$WORK/d2s.out" 2>&1
    check "the same under ASAN (SAN_BIN): no report" "$(grep -q 'AddressSanitizer' "$WORK/d2s.out" && grep -m2 'ERROR\|READ' "$WORK/d2s.out" || echo ok)"
fi

echo "### 18: round 2: Parts Build hides the follower (F-row, cursor, grid), the Part cells of the follower stay"
PB="tap Song; wait 300; hold Shift; tap F8; unhold Shift; wait 200; tap Rec; wait 100; tap Insert; wait 200;"
PRUN() {   # NAME "script"
    rm -rf "$WORK/sd_$1"; ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_$1" "wait 500; $2" 2>/dev/null | grep -E '^(partsui|part |partstart)'
}
cur() { sed -nE 's/.*cursor ([0-9]+)$/\1/p' | tr '\n' ' '; }
out=$(PRUN p1 "sampletonehz 0 441 8000; instr 0; setstep 0 0 60 0 0 0 0 0; pair 1; $PB partsui; tap Right; partsui; tap Right 2; partsui; tap Left; partsui")
got=$(echo "$out" | grep ^partsui | cur)
check "Parts Build, pair 1 linked: Right from track 1 goes to 3 then 5 (the cursor never enters track 2): 0 2 4 3 (got $got)" "$([ "$got" = "0 2 4 3 " ] && echo ok || echo "$got")"
out=$(PRUN p2 "sampletonehz 0 441 8000; instr 0; setstep 0 0 60 0 0 0 0 0; $PB partsui; tap Right; partsui; tap Right 2; partsui")
got=$(echo "$out" | grep ^partsui | cur)
check "Parts Build unlinked: the cursor walks every track (0 1 3, got $got)" "$([ "$got" = "0 1 3 " ] && echo ok || echo "$got")"
out=$(PRUN p3 "sampletonehz 0 441 8000; instr 0; setstep 0 0 60 0 0 0 0 0; pair 1; $PB tap Right 8; partsui")
got=$(echo "$out" | grep ^partsui | cur)
check "Parts Build, pair 1 linked: 8 Right steps end on track 10 (index 9), the grid has 15 columns (got $got)" "$([ "$got" = "9 " ] && echo ok || echo "$got")"
out=$(PRUN p4 "sampletonehz 0 441 8000; instr 0; setstep 0 0 60 0 0 0 0 0; $PB partset 0 src1 5; pair 1; tap Rec; wait 100; tap Rec; wait 100; hold F2; pad 2; unhold F2; wait 100; parts; hold F1; pad 6; unhold F1; wait 100; parts; pair 0; parts")
srcs() { echo "$1" | grep '^part 0' | sed 's/.* src //'; }
echo "$out" | grep -c '^part 0' >/dev/null
P4A=$(echo "$out" | grep '^part 0' | sed -n 1p | sed 's/.* src //'); P4B=$(echo "$out" | grep '^part 0' | sed -n 2p | sed 's/.* src //'); P4C=$(echo "$out" | grep '^part 0' | sed -n 3p | sed 's/.* src //')
check "F2 hold + pad 3 with pair 1 linked sets track 3 (index 2) to pattern 3 and leaves the follower's Part cell (track 2 = pattern 5) alone: src '$P4A'" "$([ "$(echo $P4A | cut -d' ' -f2)" = "5" ] && [ "$(echo $P4A | cut -d' ' -f3)" = "3" ] && echo ok || echo "$P4A")"
check "F1 hold + pad 7 sets track 1 only, the follower cell still 5: src '$P4B'" "$([ "$(echo $P4B | cut -d' ' -f1)" = "7" ] && [ "$(echo $P4B | cut -d' ' -f2)" = "5" ] && echo ok || echo "$P4B")"
check "unlinked again: the follower's Part cell is back untouched (track 2 = pattern 5): src '$P4C'" "$([ "$(echo $P4C | cut -d' ' -f2)" = "5" ] && echo ok || echo "$P4C")"
out=$(PRUN p5 "sampletonehz 0 441 8000; instr 0; setstep 0 0 60 0 0 0 0 0; pair 1; $PB hold F8; pad 4; unhold F8; wait 100; parts")
P5=$(echo "$out" | grep '^part 0' | sed -n 1p | sed 's/.* src //')
check "F8 with pair 1 linked is track 9 (index 8, the 8th column), not track 8: src '$P5'" "$([ "$(echo $P5 | cut -d' ' -f9)" = "5" ] && echo ok || echo "$P5")"

echo "### 19: round 2: the Song screen hides the follower (grid, cursor, selection, Delete)"
SG="sampletonehz 0 441 8000; instr 0; setstep 0 0 60 0 0 0 0 0; setstep 1 0 62 0 0 0 0 0; setstep 2 0 64 0 0 0 0 0; dirty;"
SRUN() {   # NAME "script": the songsel and step lines
    rm -rf "$WORK/sd_$1"; ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_$1" "wait 500; $2" 2>/dev/null | grep -E '^(songsel|step) '
}
SONGIN="tap Song; wait 500; tap Rec; wait 100; tap Left; wait 100;"
tr_() { sed -E 's/songsel pos [0-9]+ track ([0-9]+) len ([0-9]+).*/\1:\2/' | tr '\n' ' '; }
out=$(SRUN s1 "pair 3; $SONGIN songsel; tap Left 8; songsel; tap Left; songsel; tap Left; songsel; tap Left 4; songsel; tap Right 2; songsel")
got=$(echo "$out" | tr_)
check "Song screen, pairs 1+2 and 3+4 linked: the cursor walks 0-based 15 7 6 5, then Left 4 lands on 0 and Right 2 on 4 (it never rests on the hidden 1 or 3, got $got)" "$([ "$got" = "15:1 7:1 6:1 5:1 0:1 4:1 " ] && echo ok || echo "$got")"
out=$(SRUN s2 "pair 3; $SONGIN tap Left 15; songsel; hold Shift; tap Right; songsel; tap Right; songsel; tap Left; songsel; tap Left; songsel; unhold Shift")
got=$(echo "$out" | tr_)
check "Song screen Shift + Right over linked pairs: the range never ends on a hidden follower, 0:1 0:3 0:5 0:3 0:1 (got $got)" "$([ "$got" = "0:1 0:3 0:5 0:3 0:1 " ] && echo ok || echo "$got")"
out=$(SRUN s3 "pair 3; $SG $SONGIN tap Left 15; hold Shift; tap Right; unhold Shift; songsel; tap Delete; wait 800; tap Pattern; wait 500; step 0 0; step 1 0; step 2 0; pair 0; step 1 0")
check "Song screen Delete over tracks 1-3 (a range): the leaders 1 and 3 are cleared, the follower's step (note 62) survives link, Delete and unlink" "$([ "$(echo "$out" | sed -n 2p | grep -c 'note -1')$(echo "$out" | sed -n 3p | grep -c 'note 62')$(echo "$out" | sed -n 4p | grep -c 'note -1')$(echo "$out" | sed -n 5p | grep -c 'note 62')" = "1111" ] && echo ok || echo "$out")"

echo "### 20: round 3: Config > MIDI does not list the follower's Track N in channel row, unlinking lists it again"
midirows() { rm -rf "$WORK/sd_mr"; ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_mr" "wait 500; $1" 2>/dev/null | grep '^midirows'; }
LINK2='tap File; wait 800; tap F7; wait 800; tap Down 6; tap F8; wait 300; tap Down 1; tap F8; wait 300; enc 1; tap F8; wait 1600;'   # Project Settings > Stereo pairs: Tracks 3+4 = Stereo
mr=$(midirows "midirows")
check "Config > MIDI unlinked: 20 rows, Track 1 to 8 in channel all listed" "$([ "$(echo "$mr" | grep -o 'Track . in channel' | wc -l | tr -d ' ')" = 8 ] && echo "$mr" | grep -q '^midirows 20:' && echo ok || echo "$mr")"
mr=$(midirows "$LINK2 midirows")
check "linking pair 2 in Project Settings: 19 rows, Track 4 in channel gone, Track 3 in channel kept" "$(echo "$mr" | grep -q '^midirows 19:' && ! echo "$mr" | grep -q 'Track 4 in channel' && echo "$mr" | grep -q 'Track 3 in channel' && echo ok || echo "$mr")"
mr=$(midirows "pair 3; tap Config; wait 400; midirows; pair 0; tap Pattern; wait 200; tap Config; wait 400; midirows")
check "pair 3 then the Config screen: 18 rows without Track 2 and Track 4 in channel (a project load path)" "$(echo "$mr" | sed -n 1p | grep -q '^midirows 18:' && ! echo "$mr" | sed -n 1p | grep -q 'Track [24] in channel' && echo ok || echo "$mr")"
check "unlinked again and Config re-entered: 20 rows, Track 2 and Track 4 in channel are back" "$(echo "$mr" | sed -n 2p | grep -q '^midirows 20:' && echo "$mr" | sed -n 2p | grep -q 'Track 2 in channel' && echo ok || echo "$mr")"

echo "### 21: round 3: the link notice looks at every pattern of the song, once"
notice() {   # SETUP -> the notice (1 based follower track, 0 none) right after the link and after the popups ("a b")
    rm -rf "$WORK/sd_nt" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_nt" "wait 500; $1 ${LINK2%wait 1600;} wait 100; poke pairs; wait 1600; poke pairs" >/dev/null 2>&1
    sed -nE 's/^pairs 2 stereo 1 notice ([0-9]+) fx [01]$/\1/p' "$WORK/poke.out" | tr '\n' ' '
}
noticefx() {   # SETUP -> "TRACK FX" of the first poke right after the link: FX = 1 when the notice says "notes or FX"
    rm -rf "$WORK/sd_nt" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_nt" "wait 500; $1 ${LINK2%wait 1600;} wait 100; poke pairs; wait 1600" >/dev/null 2>&1
    sed -nE 's/^pairs 2 stereo 1 notice ([0-9]+) fx ([01])$/\1 \2/p' "$WORK/poke.out" | head -1
}
SB="instr 0; pattern 2; setstep 3 0 62 0 0 0 0 0; plen 16; pattern 1;"
check "a note on track 4 only in pattern 2 of the song (the open pattern 1 is empty): notice for Track 4, gone after the popups (once)" "$([ "$(notice "$SB playlist 1 2;")" = "4 0 " ] && echo ok || notice "$SB playlist 1 2;")"
check "the same note in a pattern the song does not use: no notice" "$([ "$(notice "$SB playlist 1;")" = "0 0 " ] && echo ok || notice "$SB playlist 1;")"
check "a note on the leader track 3 only (pattern 2): no notice" "$([ "$(notice "instr 0; pattern 2; setstep 2 0 62 0 0 0 0 0; plen 16; pattern 1; playlist 1 2;")" = "0 0 " ] && echo ok || echo no)"
check "the open pattern still counts (note on track 4 in pattern 1, no playlist): notice for Track 4" "$([ "$(notice "instr 0; setstep 3 0 62 0 0 0 0 0; playlist 1;")" = "4 0 " ] && echo ok || notice "instr 0; setstep 3 0 62 0 0 0 0 0; playlist 1;")"
# round 4 (poly-stereo-integrate): FX only follower steps say "notes or FX", a pattern only a Part uses is scanned too
check "a note on track 4 in the open pattern: Track 4, the text says notes (fx 0)" "$([ "$(noticefx "instr 0; setstep 3 0 62 0 0 0 0 0; playlist 1;")" = "4 0" ] && echo ok || noticefx "instr 0; setstep 3 0 62 0 0 0 0 0; playlist 1;")"
check "an FX only step on track 4 in the open pattern (no note): Track 4, notes or FX (fx 1)" "$([ "$(noticefx "instr 0; setstep 3 0 -1 0 5 10 0 0; playlist 1;")" = "4 1" ] && echo ok || noticefx "instr 0; setstep 3 0 -1 0 5 10 0 0; playlist 1;")"
check "a note on track 4 only in song pattern 2 (the bitmask cannot tell a note from an FX): notes or FX (fx 1)" "$([ "$(noticefx "$SB playlist 1 2;")" = "4 1" ] && echo ok || noticefx "$SB playlist 1 2;")"
PSB="$SB playlist 1; tap Song; wait 300; hold Shift; tap F8; unhold Shift; wait 200; tap Rec; wait 100; tap Insert; wait 200; partset 0 src3 2; tap Pattern; wait 300;"
check "a note on track 4 only in pattern 2 that only a Part uses (not in the song playlist): notice for Track 4" "$([ "$(notice "$PSB")" = "4 0 " ] && echo ok || notice "$PSB")"
PSB0="$SB playlist 1; tap Song; wait 300; hold Shift; tap F8; unhold Shift; wait 200; tap Rec; wait 100; tap Insert; wait 200; partset 0 src3 0; tap Pattern; wait 300;"
check "the same pattern 2 exists but the Part has track 4 off: no notice" "$([ "$(notice "$PSB0")" = "0 0 " ] && echo ok || notice "$PSB0")"

echo "### 14: goldens of the new UI (goldens/stereopairs, STEREOPAIRS_UPDATE=1 writes them)"
GOLD="$HERE/goldens/stereopairs"; SH="$WORK/shots"; mkdir -p "$SH"
golden() {   # NAME
    if [ -n "${STEREOPAIRS_UPDATE:-}" ]; then mkdir -p "$GOLD"; cp "$SH/$1.png" "$GOLD/$1.png"; check "golden $1 written" ok; return; fi
    if [ ! -s "$SH/$1.png" ]; then check "golden $1" "no screenshot"; elif cmp -s "$SH/$1.png" "$GOLD/$1.png"; then check "golden $1: byte equal to goldens/stereopairs/$1.png" ok
    else check "golden $1" "differs from goldens/stereopairs/$1.png (STEREOPAIRS_UPDATE=1 rewrites it)"; fi
}
rm -rf "$WORK/sd_ui"; rm -f "$WORK/poke.out"
UISHOT_POKE="$WORK/poke" ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ui" "wait 500; tap File; wait 800; tap F7; wait 800; tap Down 6; shot $SH/pairs-row.png; tap F8; wait 300; shot $SH/pairs-list.png; tap Down 1; tap F8; wait 300; shot $SH/pairs-editor.png; enc 1; tap F8; wait 500; shot $SH/pairs-linked.png; poke pairs" >/dev/null 2>&1
check "Project Settings > Stereo pairs: Tracks 3+4 set to Stereo links pair 2 (mask 2) and the sample mode" "$(grep -q '^pairs 2 stereo 1' "$WORK/poke.out" && echo ok || cat "$WORK/poke.out")"
for n in pairs-row pairs-list pairs-editor pairs-linked; do golden $n; done
# linking pair 2 while the follower track 4 holds a note: the sample reload popup runs first, then "Track 4 has notes" (once, after the popup is gone)
rm -rf "$WORK/sd_ui2"
ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ui2" "wait 500; instr 0; setstep 3 0 62 0 0 0 0 0; tap File; wait 800; tap F7; wait 800; tap Down 6; tap F8; wait 300; tap Down 1; tap F8; wait 300; enc 1; tap F8; wait 1600; shot $SH/pairs-notice.png" >/dev/null 2>&1
golden pairs-notice
ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ui" "wait 500; pair 3; instr 0; setstep 0 0 60 0 0 0 0 0; setstep 1 0 62 0 0 0 0 0; tap SampleEdit; wait 300; tap Pattern; wait 600; shot $SH/pattern-linked.png; pair 0; tap SampleEdit; wait 300; tap Pattern; wait 600; shot $SH/pattern-unlinked.png; pair 3; samplestereo 0 441 882 8000; hold Instr; wait 400; shot $SH/instr-list-st.png; unhold Instr" >/dev/null 2>&1
for n in pattern-linked pattern-unlinked instr-list-st; do golden $n; done
# the Mixer (page 2/3) and Performance with the pairs 1+2 and 3+4 linked, and the Mixer unlinked: the followers' strips and buttons are gone, the rest moves up
ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ui" "wait 500; pair 3; tap Master; wait 400; tap Master; wait 400; shot $SH/mixer-linked.png; pair 0; tap Master; wait 400; tap Master; wait 400; tap Master; wait 400; shot $SH/mixer-unlinked.png; pair 3; tap Performance; wait 500; shot $SH/performance-linked.png" >/dev/null 2>&1
ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ui" "wait 500; pair 3; tap SampleEdit; wait 300; tap Pattern; wait 400; hold Shift; wait 300; tap F2; wait 300; shot $SH/pattern-mute-linked.png; unhold Shift" >/dev/null 2>&1
ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ui" "wait 500; sampletonehz 0 441 8000; instr 0; setstep 0 0 60 0 0 0 0 0; setstep 1 0 62 0 0 0 0 0; setstep 2 0 64 0 0 0 0 0; setstep 3 0 65 0 0 0 0 0; setstep 4 0 67 0 0 0 0 0; pair 3; $PB shot $SH/parts-build-linked.png; pair 0; tap Rec; wait 100; tap Rec; wait 200; shot $SH/parts-build-unlinked.png" >/dev/null 2>&1
ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ui" "wait 500; sampletonehz 0 441 8000; instr 0; setstep 0 0 60 0 0 0 0 0; setstep 2 0 64 0 0 0 0 0; setstep 1 0 62 0 0 0 0 0; dirty; pair 3; tap Song; wait 500; tap Rec; wait 100; tap Left; wait 100; tap Left 15; wait 200; shot $SH/song-linked.png; pair 0; tap Pattern; wait 300; tap Song; wait 500; shot $SH/song-unlinked.png" >/dev/null 2>&1
ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ui" "wait 500; pair 3; tap Config; wait 500; tap Down 2; wait 200; tap F8; wait 300; shot $SH/config-midi-linked.png" >/dev/null 2>&1
rm -rf "$WORK/sd_ui3"   # a fresh card: sd_ui keeps the pair the linked shot saved with the project
ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/sd_ui3" "wait 500; tap Config; wait 500; tap Down 2; wait 200; tap F8; wait 300; shot $SH/config-midi-unlinked.png" >/dev/null 2>&1
for n in mixer-linked mixer-unlinked performance-linked pattern-mute-linked parts-build-linked parts-build-unlinked song-linked song-unlinked config-midi-linked config-midi-unlinked; do golden $n; done
[ -n "${STEREOPAIRS_SHOTS:-}" ] && { mkdir -p "$STEREOPAIRS_SHOTS"; cp "$SH"/*.png "$STEREOPAIRS_SHOTS/"; }
echo
if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; else echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1; fi
