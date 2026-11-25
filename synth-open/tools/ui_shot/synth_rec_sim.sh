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
# The rows run on the original Tracker, the Tracker+ and the Mini simulators (names pr1.. and mr1.. for the IMX ones, labelled "plus" and "mini").
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)". SAN=address,undefined: the sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
WORK="$(mktemp -d "${TMPDIR:-/tmp}/synth_rec_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }

# run NAME "script": the project starts empty, the page is entered with Shift + Perform from the Pattern page, audio on
run() {
    local n="$1"
    rm -rf "$WORK/$n"; mkdir -p "$WORK/$n"
    UISHOT_AUDIO_DUMP="$WORK/$n.raw" UISHOT_POKE="$WORK/$n.poke" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$n" "wait 500; instr $INS; audio 1; $2" >"$WORK/$n.out" 2>"$WORK/$n.err"
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
# per simulator: the entry instrument (Synth 1 of the open stack on the original, Synth 2 = ACD of the stock parts, a sustained engine), the poke line of
# the closed page (the original prints "synthplay mode 0 instr N budget B open 0 confirm 0", the stock page "splay open 0 mode 0 part 1 partb 0 voices 0 0 0 vel ...")
# and what shows that Synth B is on
INS=64; SPCLOSED="mode 0 instr 64 budget 4 open 0 confirm 0"; BONRE='part 1'
POP="poke popuplines"
# W MS: a wait of MS ms. RETRIG=1 (the stock parts of Tracker+ and Mini, whose default patches decay in half a second with the key held) plays the
# chord again every 250 ms of it, so the take always has sound. The original (sustained engine) just waits
RETRIG=0
W() {
    local ms=$1 out="" sep=""
    if [ "$RETRIG" = 1 ]; then
        while [ "$ms" -ge 250 ]; do out="${out}${sep}wait 250; $NOTE"; sep="; "; ms=$((ms-250)); done
    fi
    [ "$ms" -gt 0 ] && out="${out}${sep}wait $ms"
    printf '%s' "$out"
}
# a note held for the whole take (sustained by the engine)
NOTE="midiono 1 60 100; midiono 1 64 100; midiono 1 67 100"

# rows FLAVOUR-LABEL PREFIX: R1 to R10 of one simulator (T = label of the check names, P = prefix of the run names, BIN and ENTER set by the caller)
rows() {
local T="$1" P="$2"
# ===================================================================== R1 start and stop
run ${P}r1 "poolslot 0 5000; poolslot 1 3000; pool 16000; $ENTER; $NOTE; wait 300; recstate; audiomark; tap Rec; $(W 400); recstate; $(W 600); recstate; midionoff 1 60; midionoff 1 64; midionoff 1 67; wait 3000; recstate; tap Rec; wait 1500; recstate; $POP; instrinfo 2; pool 16000; pool; fm; poke synthplay"
expect "${T}R1 before the press: no take" "0 frames 0 cap 0" "$(rs ${P}r1 1)"
expect "${T}R1 the take runs after the press (state 1)" "1" "$(rs ${P}r1 2 | awk '{print $1}')"
expect "${T}R1 and grows while it runs" "yes" "$([ "$(rframes ${P}r1 3)" -gt "$(rframes ${P}r1 2)" ] && [ "$(rframes ${P}r1 2)" -gt 0 ] && echo yes || echo no)"
expect "${T}R1 the capacity is the free pool: (max - 16,000 B) / 2 in whole blocks" "$(( ($(pooln ${P}r1 1 | awk '{print $4}') - 16000) / 2 / 128 * 128 ))" "$(rs ${P}r1 2 | awk '{print $5}')"
expect "${T}R1 the second press stops it (state 0)" "0" "$(rs ${P}r1 5 | awk '{print $1}')"
expect "${T}R1 the instrument is the first slot after the last used one (slot 3), named AB_001, mono, in RAM, at the packed place (byte 16000)" "active 1 name AB_001 length $(ilen ${P}r1 2) offset 16000 ram $(ilen ${P}r1 2) stream 0" "$(ii ${P}r1 2)"
expect "${T}R1 the instrument is not empty" "yes" "$([ "$(ilen ${P}r1 2)" -gt 44100 ] && echo yes || echo no)"
expect "${T}R1 the saved popup" "'Take saved.' 'AB_001 in slot 3'" "$(popup ${P}r1 1)"
expect "${T}R1 the file manager is idle again" "fm status 0 op 0" "$(nth ${P}r1 '^fm ' 1)"
expect "${T}R1 the used memory grew by the take: 16,000 + 2 x frames" "$((16000 + 2 * $(ilen ${P}r1 2)))" "$(pooln ${P}r1 2 | awk '{print $2}')"
if [ -f "$(pti ${P}r1 3)" ]; then
    expect "${T}R1 the workspace file is the rendered audio sample for sample (trailing silence cut)" "PASS" "$(python3 -I "$HERE/ab_rec_check.py" "$WORK/${P}r1.raw" "$(mark ${P}r1 1)" "$(pti ${P}r1 3)" "$(ilen ${P}r1 2)" "$(rframes ${P}r1 4)" --min-rms 100 | cut -c1-4)"
else
    expect "${T}R1 the workspace file instrument_03.pti exists" "yes" "no"
fi
expect "${T}R1 the samples of the used slots are byte for byte as before (hash of the first 16,000 bytes)" "$(poolhash ${P}r1 1)" "$(poolhash ${P}r1 2)"
expect "${T}R1 the page is still open" "open 1" "$(grep -E '^(synthplay|splay) ' "$WORK/${P}r1.poke.out" | sed -n 1p | grep -o 'open [01]')"

# ===================================================================== R2 the slot after the last used one
run ${P}r2 "poolslot 0 5000; poolslot 5 3000; pool 16000; $ENTER; $NOTE; wait 300; tap Rec; $(W 1200); tap Rec; wait 1500; instrinfo 0; instrinfo 1; instrinfo 2; instrinfo 3; instrinfo 4; instrinfo 5; instrinfo 6; instrinfo 7; pool 16000; $POP"
expect "${T}R2 slots 0 and 5 used: the take is slot 6 (index 6), AB_001, at byte 16000" "active 1 name AB_001 length $(ilen ${P}r2 6) offset 16000 ram $(ilen ${P}r2 6) stream 0" "$(ii ${P}r2 6)"
expect "${T}R2 the free slots below the last used one stay free (slots 1 to 4)" "active 0|active 0|active 0|active 0" "$(for k in 1 2 3 4; do echo "active $(iact ${P}r2 $k)"; done | paste -sd'|' -)"
expect "${T}R2 the slot after the take is free" "active 0" "active $(iact ${P}r2 7)"
expect "${T}R2 the two used slots are as they were (slot 0: 5000 frames at 0, slot 5: 3000 frames at 10000)" "5000 0|3000 10000" "$(ilen ${P}r2 0) $(ioff ${P}r2 0)|$(ilen ${P}r2 5) $(ioff ${P}r2 5)"
expect "${T}R2 the pool below the take is byte for byte as before" "$(poolhash ${P}r2 1)" "$(poolhash ${P}r2 2)"
[ -f "$(pti ${P}r2 7)" ] && expect "${T}R2 the file is instrument_07.pti" "yes" "yes" || expect "${T}R2 the file is instrument_07.pti" "yes" "no"

# ===================================================================== R3 no free slot after the last instrument
run ${P}r3 "poolslot 47 1000; pool; $ENTER; $NOTE; wait 300; tap Rec; wait 300; recstate; $POP; wait 500; recstate; pool; fm"
expect "${T}R3 slot 47 used: the popup" "'No free slot after' 'the last instrument.'" "$(popup ${P}r3 1)"
expect "${T}R3 no take runs, nothing written" "0 frames 0 cap 0|0 frames 0 cap 0" "$(rs ${P}r3 1)|$(rs ${P}r3 2)"
expect "${T}R3 the memory is as before" "$(pooln ${P}r3 1)" "$(pooln ${P}r3 2)"
expect "${T}R3 the file manager was not touched" "fm status 0 op 0" "$(nth ${P}r3 '^fm ' 1)"
[ ! -e "$WORK/${P}r3/Workspace/instruments" ] || [ -z "$(ls "$WORK/${P}r3/Workspace/instruments" 2>/dev/null)" ] && expect "${T}R3 no workspace file" "yes" "yes" || expect "${T}R3 no workspace file" "yes" "no"

# ===================================================================== R4 less than 1 s of sample memory free
run ${P}r4 "poolslot 0 5000; poolmax $((10000 + 88000)); $ENTER; $NOTE; wait 300; tap Rec; wait 300; recstate; $POP; poolmax $((10000 + 88400)); tap Rec; wait 300; recstate; midiono 1 72 100; wait 200; tap Rec; wait 200; recstate; $POP"
expect "${T}R4 44,000 frames free (under 1 s): the popup and no take" "'Sample memory full.'|0 frames 0 cap 0" "$(popup ${P}r4 1)|$(rs ${P}r4 1)"
expect "${T}R4 44,200 frames free: the take starts" "1" "$(rs ${P}r4 2 | awk '{print $1}')"
expect "${T}R4 and the capacity is those frames in whole blocks (44,200 -> 44,160)" "44160" "$(rs ${P}r4 2 | awk '{print $5}')"

# ===================================================================== R5 the trailing silence is cut
run ${P}r5 "poolslot 0 5000; $ENTER; midiono 1 60 100; wait 200; audiomark; tap Rec; wait 500; midionoff 1 60; wait 4000; recstate; tap Rec; wait 1500; instrinfo 1; instrinfo 2; $POP"
F5=$(rframes ${P}r5 1); L5=$(ilen ${P}r5 1)
expect "${T}R5 the instrument is shorter than the take: more than 1 s of the tail went" "yes" "$([ -n "$L5" ] && [ "$L5" -gt 0 ] && [ $((F5 - L5)) -gt 44100 ] && echo yes || echo no)"
if [ -f "$(pti ${P}r5 2)" ]; then
    expect "${T}R5 the kept frames are the rendered audio, the last one is above -60 dBFS and every dropped one at or below it" "PASS" "$(python3 -I "$HERE/ab_rec_check.py" "$WORK/${P}r5.raw" "$(mark ${P}r5 1)" "$(pti ${P}r5 2)" "$L5" "$F5" --min-rms 100 | cut -c1-4)"
else
    expect "${T}R5 the workspace file instrument_02.pti exists" "yes" "no"
fi

# ===================================================================== R6 the pool end stops the take
run ${P}r6 "poolslot 0 5000; poolmax $((10000 + 120000)); $ENTER; $NOTE; wait 300; audiomark; tap Rec; $(W 800); recstate; $(W 600); $POP; wait 100; $POP; wait 100; $POP; wait 100; $POP; wait 100; $POP; wait 1600; recstate; instrinfo 1; pool; fm"
expect "${T}R6 the take runs before the pool is full" "1" "$(rs ${P}r6 1 | awk '{print $1}')"
expect "${T}R6 the pool end stopped it by itself (state 0, no second press)" "0" "$(rs ${P}r6 2 | awk '{print $1}')"
expect "${T}R6 the instrument holds the whole capacity (60,000 frames in whole blocks = 59,904), the note was sustained so nothing was cut" "active 1 name AB_001 length 59904 offset 10000 ram 59904 stream 0" "$(ii ${P}r6 1)"
expect "${T}R6 the memory limit holds: used 10,000 + 119,808 = 129,808 of 130,000" "used 129808 max 130000" "$(pooln ${P}r6 1 | awk '{print $1" "$2" "$3" "$4}')"
expect "${T}R6 the popup says the memory is full and where the take went" "'Sample memory full.' 'AB_001 in slot 2'" "$(grep '^popuplines' "$WORK/${P}r6.poke.out" | grep -v none | sed -n 1p | sed 's/^popuplines //')"
expect "${T}R6 the file manager is idle" "fm status 0 op 0" "$(nth ${P}r6 '^fm ' 1)"
[ -f "$(pti ${P}r6 2)" ] && expect "${T}R6 the take is the rendered audio" "PASS" "$(python3 -I "$HERE/ab_rec_check.py" "$WORK/${P}r6.raw" "$(mark ${P}r6 1)" "$(pti ${P}r6 2)" 59904 59904 --min-rms 100 | cut -c1-4)" || expect "${T}R6 the workspace file instrument_02.pti exists" "yes" "no"

# ===================================================================== R7 two takes, the samples below them untouched
run ${P}r7 "poolslot 0 3000; poolslot 1 2000; $ENTER; $NOTE; wait 300; tap Rec; $(W 1200); tap Rec; wait 1500; instrinfo 2; pool snap; tap Rec; $(W 1200); tap Rec; wait 1500; instrinfo 2; instrinfo 3; pool old"
expect "${T}R7 the first take is slot 3 (index 2) named AB_001" "active 1 name AB_001" "$(ii ${P}r7 2 | cut -d' ' -f1-4)"
expect "${T}R7 the second take is the next slot (index 3), named AB_002, right after the first (byte 10,000 + 2 x its length)" "active 1 name AB_002 offset $((10000 + 2 * $(ilen ${P}r7 2)))" "$(ii ${P}r7 3 | cut -d' ' -f1-4) offset $(ioff ${P}r7 3)"
expect "${T}R7 the first take and the two samples below it are byte for byte as after the first take" "$(poolhash ${P}r7 1)" "$(poolhash ${P}r7 2)"

# ===================================================================== R8 nothing played
run ${P}r8 "poolslot 0 5000; pool; $ENTER; tap Rec; wait 800; recstate; tap Rec; wait 1000; recstate; instrinfo 1; pool; $POP; fm"
expect "${T}R8 a take of digital silence lands nothing: the popup" "'Nothing recorded.'" "$(popup ${P}r8 1)"
expect "${T}R8 no instrument, the memory as before, the take over" "active 0|$(pooln ${P}r8 1)|0" "active $(iact ${P}r8 1)|$(pooln ${P}r8 2)|$(rs ${P}r8 2 | awk '{print $1}')"

# ===================================================================== R9 a page key during a take
run ${P}r9 "poolslot 0 5000; $ENTER; $NOTE; wait 300; audiomark; tap Rec; $(W 1000); tap Pattern; wait 3000; poke synthplay; recstate; instrinfo 1; $POP; fm; status"
expect "${T}R9 the page key stopped the take, landed it and then left the page (mode 0, closed)" "$SPCLOSED" "$(grep -E '^(synthplay|splay) ' "$WORK/${P}r9.poke.out" | sed -n 1p | sed -e 's/^synthplay //' -e 's/^splay //' -e 's/ vel .*//')"
expect "${T}R9 the take is the instrument after the used one" "active 1 name AB_001" "$(ii ${P}r9 1 | cut -d' ' -f1-4)"
expect "${T}R9 no take runs, the file manager is idle" "0|fm status 0 op 0" "$(rs ${P}r9 1 | awk '{print $1}')|$(nth ${P}r9 '^fm ' 1)"
[ -f "$(pti ${P}r9 2)" ] && expect "${T}R9 the file is the rendered audio" "PASS" "$(python3 -I "$HERE/ab_rec_check.py" "$WORK/${P}r9.raw" "$(mark ${P}r9 1)" "$(pti ${P}r9 2)" "$(ilen ${P}r9 1)" "$(ilen ${P}r9 1)" --min-rms 100 | cut -c1-4)" || expect "${T}R9 the workspace file instrument_02.pti exists" "yes" "no"

# ===================================================================== R10 A/B mode
run ${P}r10 "poolslot 0 5000; $ENTER; tap F5; enc 1; wait 200; $NOTE; wait 300; audiomark; tap Rec; $(W 1500); tap Rec; wait 1500; instrinfo 1; poke synthplay"
expect "${T}R10 B is on (Synth B column)" "$BONRE" "$(grep -E '^(synthplay|splay) ' "$WORK/${P}r10.poke.out" | grep -o "$BONRE" | head -1)"
expect "${T}R10 the take lands the same with B on" "active 1 name AB_001 length $(ilen ${P}r10 1) offset 10000 ram $(ilen ${P}r10 1) stream 0" "$(ii ${P}r10 1)"
[ -f "$(pti ${P}r10 2)" ] && expect "${T}R10 and is the rendered audio of both parts" "PASS" "$(python3 -I "$HERE/ab_rec_check.py" "$WORK/${P}r10.raw" "$(mark ${P}r10 1)" "$(pti ${P}r10 2)" "$(ilen ${P}r10 1)" "$(ilen ${P}r10 1)" --min-rms 100 | cut -c1-4)" || expect "${T}R10 the workspace file exists" "yes" "no"

}

# ===================================================================== the three simulators
# SYNTH_REC_FLAVOURS: which ones (default "og plus mini"). The original runs UI_SHOT_BIN (or build/ui-shot). Tracker+ and Mini run the STOCK
# synth stack as the images do (PLATFORM=IMX BRIDGE=1: no FEATURE_SYNTH_OPEN, the closed libPolySynth under Unicorn, the page's FEATURE_SYNTH_PATCHFX
# half, so FEATURE_SYNTH_REC comes from that flag alone): UI_SHOT_BIN_PLUS / UI_SHOT_BIN_MINI, else build/ui-shot-plus-console-bridge and
# build/ui-shot-mini-console-bridge (the smokes' builds, DEVTOOLS_DIR as theirs) made here (bridge_env.sh, host simulator only, never a firmware target), UISHOT_PLUS_SYNTH the closed engine harness
# (else plus_synth/build.sh into build/plus-synth). REC records the master block both sims hand to ffHookAudioBlockOut, the same 128 frames as
# the unit. The Mini has no Shift + Perform: the page is box 12 of the Menu popup (Up then Left from the Pattern box, interfaceMenuPopup.cpp)
FLAV=" ${SYNTH_REC_FLAVOURS:-og plus mini} "
case "$FLAV" in *" og "*) rows "" "" ;; esac
case "$FLAV" in *" plus "*" mini "*|*" plus "*|*" mini "*)
    if [ -z "${UISHOT_PLUS_SYNTH:-}" ]; then
        bash "$HERE/../plus_synth/build.sh" "$ROOT/build/plus-synth" >"$BUILD.harness.log" 2>&1 || { tail -20 "$BUILD.harness.log"; exit 2; }
        UISHOT_PLUS_SYNTH="$ROOT/build/plus-synth/harness.elf"
    fi
    export UISHOT_PLUS_SYNTH ;; esac
imx_bin() {   # imx_bin FLAVOUR PRODUCT: BIN of the bridge build, made unless the caller gave one
    local given="UI_SHOT_BIN_$(echo "$1" | tr a-z A-Z)"
    if [ -n "${!given:-}" ]; then BIN="${!given}"; return; fi
    . "$HERE/bridge_env.sh"; bridge_env "$ROOT" || exit 2
    if [ -z "${DEVTOOLS_DIR:-}" ]; then   # the same search as plus_ttype_vel_sim.sh
        for d in "/Volumes/StoreCI/prj/poly-devtools/.wt/rel-200-int" "$HOME/prj/poly-devtools/.wt/rel-200-int" "$HOME/prj/poly-devtools/.wt/main" "$HOME/prj/poly-devtools"; do
            grep -q UISHOT_PLUS "$d/firmware/include/UsbTest.h" 2>/dev/null && { DEVTOOLS_DIR="$d"; break; }
        done
    fi
    [ -n "${DEVTOOLS_DIR:-}" ] || { echo "synth_rec_sim: the Tracker+ and Mini rows need DEVTOOLS_DIR (a console checkout with the UISHOT_PLUS guards, devtools rel-2.0.0-int or main) or UI_SHOT_BIN_PLUS / UI_SHOT_BIN_MINI" >&2; exit 2; }
    make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" PLATFORM=IMX PRODUCT="$2" CONSOLE=1 "${BRIDGE_ARGS[@]}" DEVTOOLS_DIR="$DEVTOOLS_DIR" >"$BUILD.$1.make.log" 2>&1 || { tail -30 "$BUILD.$1.make.log"; exit 2; }
    BIN="$ROOT/build/ui-shot-$1-console-bridge/ui_shot"
}
case "$FLAV" in *" plus "*)
    imx_bin plus PLUS
    ENTER="hold Shift; tap Performance; unhold Shift; wait 300"
    INS=65; RETRIG=1; SPCLOSED="open 0 mode 0 part 1 partb 0 voices 0 0 0"; BONRE='partb 1'
    rows "plus " "p" ;; esac
case "$FLAV" in *" mini "*)
    imx_bin mini MINI
    ENTER="hold Menu; tap Up; tap Left; unhold Menu; wait 300"
    INS=65; RETRIG=1; SPCLOSED="open 0 mode 0 part 1 partb 0 voices 0 0 0"; BONRE='partb 1'
    rows "mini " "m" ;; esac
RETRIG=0

if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; exit 0; fi
echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1
