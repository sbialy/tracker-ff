#!/bin/bash
# synth_rec_sim.sh
#
# REC on the Synth play page (ref/synth-ab-rec.md): a mono take of the master output as heard, written into the free tail of the sample memory
# and landed as an instrument in the first slot after the last used one. The real UI, the real file manager, the real audio callback and the
# real engines of this tree (ui_shot, the host build, nothing here touches a device). The synth plays through the real audio callback, the
# audio dump (UISHOT_AUDIO_DUMP, audiomark) is the reference the workspace file is compared with sample for sample (ab_rec_check.py). The
# sample memory is set up with "poolslot" (an active instrument of N frames at its packed place), read back with "pool", "instrinfo" and
# "recstate" (ui_shot_main.cpp).
#
# Rows:
#   R1  start and stop: the take runs from the REC press, grows while it runs, stops with the second press, the instrument has the take's frames
#       (rendered audio, sample for sample) and lands in the workspace (instrument_NN.pti), the page shows the saved popup
#   R2  slot landing: the first slot after the LAST used instrument (slots 0 and 5 used: slot 6, slots 1 to 4 stay free), the sample at the packed place
#   R3  no free slot after the last instrument (slot 47 used): popup, no take, nothing changed
#   R4  sample memory full before the start (less than 1 s left): popup, no take
#   R5  trailing silence below -60 dBFS is cut when the take stops (ab_rec_check.py: the dropped frames are all at or below it, the last kept one is above)
#   R6  auto stop when the sample memory is full: the take stops by itself at the pool end, lands what is there, the popup says so, the pool limit holds
#   R7  existing samples byte for byte as before after a take (pool hash of the bytes below the take), a second take goes to the next slot and gets the next name
#   R8  nothing played: a take of digital silence lands nothing (popup, no instrument, memory as before)
#   R9  a page key during a take stops it, lands it and then leaves the page (mode 0, the instrument is there)
#   R10 works with B on (A/B mode) and with B off, the same take
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)". SAN=address,undefined: the sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/synth_rec_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }

# run NAME "script": the project starts empty, the page is entered with Shift + Perform from the Pattern page, audio on
run() {
    local n="$1"
    rm -rf "$WORK/$n"; mkdir -p "$WORK/$n"
    UISHOT_AUDIO_DUMP="$WORK/$n.raw" UISHOT_POKE="$WORK/$n.poke" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$n" "wait 500; instr 64; audio 1; $2" >"$WORK/$n.out" 2>"$WORK/$n.err"
    [ -f "$WORK/$n.poke.out" ] || : >"$WORK/$n.poke.out"
}
out() { cat "$WORK/$1.out"; }
# the Nth line of a kind in the run's output: "recstate" and "instrinfo N" lines, the popup lines, the pool lines, the audiomarks
nth() { grep -E "$2" "$WORK/$1.out" | sed -n "${3:-1}p"; }
popup() { grep '^popuplines' "$WORK/$1.poke.out" | sed -n "${2:-1}p" | sed 's/^popuplines //'; }
mark() { grep '^audio frames' "$WORK/$1.out" | sed -n "${2:-1}p" | awk '{print $3}'; }
rs() { nth "$1" '^recstate' "$2" | sed 's/^recstate //'; }                   # "S frames F cap C"
rframes() { rs "$1" "$2" | awk '{print $3}'; }
ii() { nth "$1" "^instrinfo $2 " 1 | sed "s/^instrinfo $2 //"; }              # the instrinfo of slot N (the last print of it)
ilen() { ii "$1" "$2" | sed -n 's/.* length \([0-9]*\) .*/\1/p'; }
ioff() { ii "$1" "$2" | sed -n 's/.* offset \(-*[0-9]*\) .*/\1/p'; }
iact() { ii "$1" "$2" | awk '{print $2}'; }
pooln() { grep '^pool ' "$WORK/$1.out" | sed -n "$2p" | sed 's/^pool //'; }
poolhash() { pooln "$1" "$2" | awk '{print $6}'; }
pti() { printf '%s/%s/Workspace/instruments/instrument_%02d.pti' "$WORK" "$1" "$2"; }   # slot N (1 based, as the file name)

ENTER="hold Shift; tap Performance; unhold Shift; wait 300"
POP="poke popuplines"
# a note held for the whole take (sustained by the engine)
NOTE="midiono 1 60 100; midiono 1 64 100; midiono 1 67 100"

# ===================================================================== R1 start and stop
run r1 "poolslot 0 5000; poolslot 1 3000; pool 16000; $ENTER; $NOTE; wait 300; recstate; audiomark; tap Rec; wait 400; recstate; wait 600; recstate; midionoff 1 60; midionoff 1 64; midionoff 1 67; wait 3000; recstate; tap Rec; wait 1500; recstate; $POP; instrinfo 2; pool 16000; pool; fm; poke synthplay"
expect "R1 before the press: no take" "0 frames 0 cap 0" "$(rs r1 1)"
expect "R1 the take runs after the press (state 1)" "1" "$(rs r1 2 | awk '{print $1}')"
expect "R1 and grows while it runs" "yes" "$([ "$(rframes r1 3)" -gt "$(rframes r1 2)" ] && [ "$(rframes r1 2)" -gt 0 ] && echo yes || echo no)"
expect "R1 the capacity is the free pool: (max - 16,000 B) / 2 in whole blocks" "5890176" "$(rs r1 2 | awk '{print $5}')"
expect "R1 the second press stops it (state 0)" "0" "$(rs r1 5 | awk '{print $1}')"
expect "R1 the instrument is the first slot after the last used one (slot 3), named AB_001, mono, in RAM, at the packed place (byte 16000)" "active 1 name AB_001 length $(ilen r1 2) offset 16000 ram $(ilen r1 2) stream 0" "$(ii r1 2)"
expect "R1 the instrument is not empty" "yes" "$([ "$(ilen r1 2)" -gt 44100 ] && echo yes || echo no)"
expect "R1 the saved popup" "'Take saved.' 'AB_001 in slot 3'" "$(popup r1 1)"
expect "R1 the file manager is idle again" "fm status 0 op 0" "$(nth r1 '^fm ' 1)"
expect "R1 the used memory grew by the take: 16,000 + 2 x frames" "$((16000 + 2 * $(ilen r1 2)))" "$(pooln r1 2 | awk '{print $2}')"
if [ -f "$(pti r1 3)" ]; then
    expect "R1 the workspace file is the rendered audio sample for sample (trailing silence cut)" "PASS" "$(python3 -I "$HERE/ab_rec_check.py" "$WORK/r1.raw" "$(mark r1 1)" "$(pti r1 3)" "$(ilen r1 2)" "$(rframes r1 4)" --min-rms 100 | cut -c1-4)"
else
    expect "R1 the workspace file instrument_03.pti exists" "yes" "no"
fi
expect "R1 the samples of the used slots are byte for byte as before (hash of the first 16,000 bytes)" "$(poolhash r1 1)" "$(poolhash r1 2)"
expect "R1 the page is still open" "open 1" "$(grep '^synthplay' "$WORK/r1.poke.out" | sed -n 1p | grep -o 'open [01]')"

# ===================================================================== R2 the slot after the last used one
run r2 "poolslot 0 5000; poolslot 5 3000; pool 16000; $ENTER; $NOTE; wait 300; tap Rec; wait 1200; tap Rec; wait 1500; instrinfo 0; instrinfo 1; instrinfo 2; instrinfo 3; instrinfo 4; instrinfo 5; instrinfo 6; instrinfo 7; pool 16000; $POP"
expect "R2 slots 0 and 5 used: the take is slot 6 (index 6), AB_001, at byte 16000" "active 1 name AB_001 length $(ilen r2 6) offset 16000 ram $(ilen r2 6) stream 0" "$(ii r2 6)"
expect "R2 the free slots below the last used one stay free (slots 1 to 4)" "active 0|active 0|active 0|active 0" "$(for k in 1 2 3 4; do echo "active $(iact r2 $k)"; done | paste -sd'|' -)"
expect "R2 the slot after the take is free" "active 0" "active $(iact r2 7)"
expect "R2 the two used slots are as they were (slot 0: 5000 frames at 0, slot 5: 3000 frames at 10000)" "5000 0|3000 10000" "$(ilen r2 0) $(ioff r2 0)|$(ilen r2 5) $(ioff r2 5)"
expect "R2 the pool below the take is byte for byte as before" "$(poolhash r2 1)" "$(poolhash r2 2)"
[ -f "$(pti r2 7)" ] && expect "R2 the file is instrument_07.pti" "yes" "yes" || expect "R2 the file is instrument_07.pti" "yes" "no"

# ===================================================================== R3 no free slot after the last instrument
run r3 "poolslot 47 1000; pool; $ENTER; $NOTE; wait 300; tap Rec; wait 300; recstate; $POP; wait 500; recstate; pool; fm"
expect "R3 slot 47 used: the popup" "'No free slot after' 'the last instrument.'" "$(popup r3 1)"
expect "R3 no take runs, nothing written" "0 frames 0 cap 0|0 frames 0 cap 0" "$(rs r3 1)|$(rs r3 2)"
expect "R3 the memory is as before" "$(pooln r3 1)" "$(pooln r3 2)"
expect "R3 the file manager was not touched" "fm status 0 op 0" "$(nth r3 '^fm ' 1)"
[ ! -e "$WORK/r3/Workspace/instruments" ] || [ -z "$(ls "$WORK/r3/Workspace/instruments" 2>/dev/null)" ] && expect "R3 no workspace file" "yes" "yes" || expect "R3 no workspace file" "yes" "no"

# ===================================================================== R4 less than 1 s of sample memory free
run r4 "poolslot 0 5000; poolmax $((10000 + 88000)); $ENTER; $NOTE; wait 300; tap Rec; wait 300; recstate; $POP; poolmax $((10000 + 88400)); tap Rec; wait 300; recstate; midiono 1 72 100; wait 200; tap Rec; wait 200; recstate; $POP"
expect "R4 44,000 frames free (under 1 s): the popup and no take" "'Sample memory full.'|0 frames 0 cap 0" "$(popup r4 1)|$(rs r4 1)"
expect "R4 44,200 frames free: the take starts" "1" "$(rs r4 2 | awk '{print $1}')"
expect "R4 and the capacity is those frames in whole blocks (44,200 -> 44,160)" "44160" "$(rs r4 2 | awk '{print $5}')"

# ===================================================================== R5 the trailing silence is cut
run r5 "poolslot 0 5000; $ENTER; midiono 1 60 100; wait 200; audiomark; tap Rec; wait 500; midionoff 1 60; wait 4000; recstate; tap Rec; wait 1500; instrinfo 1; instrinfo 2; $POP"
F5=$(rframes r5 1); L5=$(ilen r5 1)
expect "R5 the instrument is shorter than the take: more than 1 s of the tail went" "yes" "$([ -n "$L5" ] && [ "$L5" -gt 0 ] && [ $((F5 - L5)) -gt 44100 ] && echo yes || echo no)"
if [ -f "$(pti r5 2)" ]; then
    expect "R5 the kept frames are the rendered audio, the last one is above -60 dBFS and every dropped one at or below it" "PASS" "$(python3 -I "$HERE/ab_rec_check.py" "$WORK/r5.raw" "$(mark r5 1)" "$(pti r5 2)" "$L5" "$F5" --min-rms 100 | cut -c1-4)"
else
    expect "R5 the workspace file instrument_02.pti exists" "yes" "no"
fi

# ===================================================================== R6 the pool end stops the take
run r6 "poolslot 0 5000; poolmax $((10000 + 120000)); $ENTER; $NOTE; wait 300; audiomark; tap Rec; wait 800; recstate; wait 600; $POP; wait 100; $POP; wait 100; $POP; wait 100; $POP; wait 100; $POP; wait 1600; recstate; instrinfo 1; pool; fm"
expect "R6 the take runs before the pool is full" "1" "$(rs r6 1 | awk '{print $1}')"
expect "R6 the pool end stopped it by itself (state 0, no second press)" "0" "$(rs r6 2 | awk '{print $1}')"
expect "R6 the instrument holds the whole capacity (60,000 frames in whole blocks = 59,904), the note was sustained so nothing was cut" "active 1 name AB_001 length 59904 offset 10000 ram 59904 stream 0" "$(ii r6 1)"
expect "R6 the memory limit holds: used 10,000 + 119,808 = 129,808 of 130,000" "used 129808 max 130000" "$(pooln r6 1 | awk '{print $1" "$2" "$3" "$4}')"
expect "R6 the popup says the memory is full and where the take went" "'Sample memory full.' 'AB_001 in slot 2'" "$(grep '^popuplines' "$WORK/r6.poke.out" | grep -v none | sed -n 1p | sed 's/^popuplines //')"
expect "R6 the file manager is idle" "fm status 0 op 0" "$(nth r6 '^fm ' 1)"
[ -f "$(pti r6 2)" ] && expect "R6 the take is the rendered audio" "PASS" "$(python3 -I "$HERE/ab_rec_check.py" "$WORK/r6.raw" "$(mark r6 1)" "$(pti r6 2)" 59904 59904 --min-rms 100 | cut -c1-4)" || expect "R6 the workspace file instrument_02.pti exists" "yes" "no"

# ===================================================================== R7 two takes, the samples below them untouched
run r7 "poolslot 0 3000; poolslot 1 2000; $ENTER; $NOTE; wait 300; tap Rec; wait 1200; tap Rec; wait 1500; instrinfo 2; pool snap; tap Rec; wait 1200; tap Rec; wait 1500; instrinfo 2; instrinfo 3; pool old"
expect "R7 the first take is slot 3 (index 2) named AB_001" "active 1 name AB_001" "$(ii r7 2 | cut -d' ' -f1-4)"
expect "R7 the second take is the next slot (index 3), named AB_002, right after the first (byte 10,000 + 2 x its length)" "active 1 name AB_002 offset $((10000 + 2 * $(ilen r7 2)))" "$(ii r7 3 | cut -d' ' -f1-4) offset $(ioff r7 3)"
expect "R7 the first take and the two samples below it are byte for byte as after the first take" "$(poolhash r7 1)" "$(poolhash r7 2)"

# ===================================================================== R8 nothing played
run r8 "poolslot 0 5000; pool; $ENTER; tap Rec; wait 800; recstate; tap Rec; wait 1000; recstate; instrinfo 1; pool; $POP; fm"
expect "R8 a take of digital silence lands nothing: the popup" "'Nothing recorded.'" "$(popup r8 1)"
expect "R8 no instrument, the memory as before, the take over" "active 0|$(pooln r8 1)|0" "active $(iact r8 1)|$(pooln r8 2)|$(rs r8 2 | awk '{print $1}')"

# ===================================================================== R9 a page key during a take
run r9 "poolslot 0 5000; $ENTER; $NOTE; wait 300; audiomark; tap Rec; wait 1000; tap Pattern; wait 3000; poke synthplay; recstate; instrinfo 1; $POP; fm; status"
expect "R9 the page key stopped the take, landed it and then left the page (mode 0, closed)" "mode 0 instr 64 budget 4 open 0 confirm 0" "$(grep '^synthplay' "$WORK/r9.poke.out" | sed -n 1p | sed 's/^synthplay //')"
expect "R9 the take is the instrument after the used one" "active 1 name AB_001" "$(ii r9 1 | cut -d' ' -f1-4)"
expect "R9 no take runs, the file manager is idle" "0|fm status 0 op 0" "$(rs r9 1 | awk '{print $1}')|$(nth r9 '^fm ' 1)"
[ -f "$(pti r9 2)" ] && expect "R9 the file is the rendered audio" "PASS" "$(python3 -I "$HERE/ab_rec_check.py" "$WORK/r9.raw" "$(mark r9 1)" "$(pti r9 2)" "$(ilen r9 1)" "$(ilen r9 1)" --min-rms 100 | cut -c1-4)" || expect "R9 the workspace file instrument_02.pti exists" "yes" "no"

# ===================================================================== R10 A/B mode
run r10 "poolslot 0 5000; $ENTER; tap F5; enc 1; wait 200; $NOTE; wait 300; audiomark; tap Rec; wait 1500; tap Rec; wait 1500; instrinfo 1; poke synthplay"
expect "R10 B is on (Synth B column)" "part 1" "$(grep '^synthplay' "$WORK/r10.poke.out" | grep -o 'part 1' | head -1)"
expect "R10 the take lands the same with B on" "active 1 name AB_001 length $(ilen r10 1) offset 10000 ram $(ilen r10 1) stream 0" "$(ii r10 1)"
[ -f "$(pti r10 2)" ] && expect "R10 and is the rendered audio of both parts" "PASS" "$(python3 -I "$HERE/ab_rec_check.py" "$WORK/r10.raw" "$(mark r10 1)" "$(pti r10 2)" "$(ilen r10 1)" "$(ilen r10 1)" --min-rms 100 | cut -c1-4)" || expect "R10 the workspace file exists" "yes" "no"

if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; exit 0; fi
echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1
