#!/bin/bash
# recjoin_cursor_sim.sh
#
# The live record join with SEVERAL armed tracks. User decision
# 2026-11-12: with several tracks armed the join goes onto the track under the cursor when that track is armed, instead of
# silently doing nothing (getSingleArmedTrack() was -1, the join was never tried, the chord spread over tracks). Second user decision
# 2026-11-12: NO spread - a note that cannot join is never recorded to another armed track.
#
# The rule (mtSequencer.cpp handleNoteOn / handleNoteOff):
#   - exactly one track armed (any cursor)            old single-armed path, untouched
#   - several armed, cursor track armed               the cursor track records ALONE, exactly as if it were the only armed track: the first
#                                                     note goes onto it, a note within the join window of it while it is held joins it
#                                                     (chord in the MIDI Chord FX). A note that does not join (no chord, not a MIDI or synth
#                                                     step, FX slots full) is DROPPED: not written, not sounded, no other track gets it
#   - several armed, cursor track NOT armed           no join, stock spread (a lone other armed track does not count: several armed)
#   - pads that record instruments (Instr pads)       never join, stock spread over the armed tracks
# Everything is armed with the keys of the device run (Shift + Rec, F1..F8 on both track pages), never with the "arm" shortcut.
#
# Sections:  R rule (cursor armed, both pages, pads and MIDI)   N cursor track not armed   S single armed is the oracle   T dropped notes   P Instr pads
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)". SAN=address,undefined: the sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/recjoin_cursor_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the lines the script prints
    local sd="$WORK/$1"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(step|armed|cursor) '
}
# the written steps as "track:step=note[/fx]" (tracks 1 based, steps hex), empty steps left out
cells() { grep '^step' | grep -v 'note -1 instr 0 fx0 0:0 fx1 0:0' | awk '{printf "%s%d:%x=%s%s", (n++ ? " " : ""), $2 + 1, $3, $5, ($9 != "0:0" ? "/" $9 : "")} END {print ""}'; }
STEPS=""; for t in $(seq 0 15); do for i in $(seq 0 40); do STEPS="$STEPS step $t $i;"; done; done

# the device run: Pattern, cursor on track 3 (index 2), instrument 63 (MIDI 16) with Instr + encoder
GO="audio 1; tap Pattern; wait 1200; tap Right 2; wait 1200"
INST="hold Instr; wait 300; enc 64; wait 300; unhold Instr; wait 400"
CURSOR="$GO; $INST"          # MIDI instrument on the cursor
CURSOR_S="$GO"               # the default (sample) instrument
# Shift + Rec arming UI, then F keys toggle the tracks of the cursor's page (fPatternEdit.cpp:1451). Every track starts armed.
fkeys() { local k out="hold Shift; tap Rec; wait 400;"; for k in "$@"; do out="$out tap F$k; wait 200;"; done; printf '%s unhold Shift; wait 500' "$out"; }
PAGE_NEXT="tap Right 6; wait 300"; PAGE_PREV="tap Left 6; wait 300"
ARM_DEV="$(fkeys 1 2 4 5 6 7 8)"                                   # track 3 + the 8 MIDI tracks stay armed (the device run)
ARM_MIDI_OFF="$PAGE_NEXT; $(fkeys 1 2 3 4 5 6 7 8); $PAGE_PREV"    # the MIDI page disarmed
ARM_3_ONLY="$ARM_DEV; $ARM_MIDI_OFF"                               # track 3 alone
ARM_3_OFF="$(fkeys 3)"                                             # 15 armed, the cursor track not
ARM_5_ONLY="$(fkeys 1 2 3 4 6 7 8); $ARM_MIDI_OFF"                 # only track 5 armed, cursor on 3
START="tap Play; wait 200; hold Rec; tap Play; unhold Rec; wait 100"
STOP="wait 300; tap Play; wait 300"

midiseq() {   # "+60@0 -60@123 ...": the events at those ms (5 ms grid of the simulator loop) as midion / midioff with the loop run between
    local out="" w i
    local -a ev=($1)
    for ((i = 0; i < ${#ev[@]}; i++)); do
        local e="${ev[$i]}" next
        local note="${e:1}"; note="${note%@*}"
        if [ $((i + 1)) -lt ${#ev[@]} ]; then next="${ev[$((i + 1))]#*@}"; w=$(( (next - ${e#*@} + 2) / 5 * 5 )); else w=0; fi
        if [ "${e:0:1}" = "+" ]; then out="$out midion $note 100 $w;"; else out="$out midioff $note 100 $w;"; fi
    done
    printf '%s' "$out"
}
PADS="padon 12 5; padon 16 5; padon 19 300; padoff 12 5; padoff 16 5; padoff 19 150"          # C E G, 6 ms apart, C released first
OLDJ1="+72@0 +76@79 +79@153 -72@565 -76@591 -79@612"                                          # old midisend gaps (79 and 74 ms)
TSJ1="+72@0 +76@18 +79@36 -72@346 -76@351 -79@356"                                              # midisend_ts gaps
# scen NAME HEAD ARMING MOVE INPUT: the written cells of one run
scen() { run "$1" "$2; $3; $4; $START; $5; $STOP; $STEPS" | cells; }

# ---- R. the rule: several armed, the cursor track armed
echo "-- R: cursor track armed, several armed"
expect "R1 arming check: all 16 armed by default, F keys only toggle their page" "armed 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1" "$(run r1 "$CURSOR; armed" | sed -n 1p)"
expect "R2 pads, all 16 armed, cursor on 3: one step C with maj" "3:5=60/14:5 3:7=-4" "$(scen r2 "$CURSOR" "" "" "$PADS")"
expect "R3 pads, device arming (3 + MIDI page), cursor on 3: one step C with maj" "3:5=60/14:5 3:7=-4" "$(scen r3 "$CURSOR" "$ARM_DEV" "" "$PADS")"
expect "R4 pads, all armed, cursor on MIDI track 9: joins there" "9:5=60/14:5 9:7=-4" "$(scen r4 "$CURSOR" "" "$PAGE_NEXT" "$PADS")"
expect "R5 pads, sample page F keys only (MIDI page armed), cursor on 9: joins there" "9:5=60/14:5 9:7=-4" "$(scen r5 "$CURSOR" "$ARM_DEV" "$PAGE_NEXT" "$PADS")"
expect "R6 pads, all armed, cursor on MIDI track 12 (Right 9): joins there" "12:5=60/14:5 12:7=-4" "$(scen r6 "$CURSOR" "" "tap Right 9; wait 300" "$PADS")"
expect "R7 MIDI timestamped gaps, all armed, cursor on 3: maj" "3:5=48/14:5 3:8=-4" "$(scen r7 "$CURSOR" "" "" "$(midiseq "$TSJ1")")"
expect "R8 MIDI old gaps (79 and 74 ms), device arming, cursor on 3: maj, OFF 5 steps later" "3:5=48/14:5 3:a=-4" "$(scen r8 "$CURSOR" "$ARM_DEV" "" "$(midiseq "$OLDJ1")")"
expect "R9 MIDI timestamped gaps, all armed, cursor on 9: maj on the MIDI track" "9:5=48/14:5 9:8=-4" "$(scen r9 "$CURSOR" "" "$PAGE_NEXT" "$(midiseq "$TSJ1")")"
expect "R10 MIDI, first note released at 100 ms, last at 500 ms: the OFF waits for the last release (step 9)" "3:5=48/14:5 3:9=-4" "$(scen r10 "$CURSOR" "" "" "$(midiseq "+72@0 +76@18 +79@36 -72@100 -76@300 -79@500")")"

# ---- N. cursor track not armed: no join
echo "-- N: cursor track not armed"
N_PADS="3:5=60 3:7=-4"
expect "N1 pads, cursor on 3 disarmed, 15 armed: no join, the notes spread over the next armed tracks (stock)" "4:5=60 4:7=-4 5:5=64 5:7=-4 6:5=67 6:7=-4" "$(scen n1 "$CURSOR" "$ARM_3_OFF" "" "$PADS")"
expect "N2 MIDI, cursor on 3 disarmed, 15 armed: no join, spread (stock)" "4:5=48 4:8=-4 5:5=52 5:8=-4 6:5=55 6:8=-4" "$(scen n2 "$CURSOR" "$ARM_3_OFF" "" "$(midiseq "$TSJ1")")"
expect "N3 pads, cursor on 3 (not armed), track 5 the only armed: old single-armed rule, joins on 5" "5:5=60/14:5 5:7=-4" "$(scen n3 "$CURSOR" "$ARM_5_ONLY" "" "$PADS")"
expect "N4 pads, cursor on 3 not armed, MIDI page armed: no join, MIDI tracks take the notes (stock)" "9:5=60 9:7=-4 10:5=64 10:7=-4 11:5=67 11:7=-4" "$(scen n4 "$CURSOR" "$(fkeys 1 2 3 4 5 6 7 8)" "" "$PADS")"

# ---- S. the single armed path is the oracle for the rule: same input, same grid
echo "-- S: several armed with the cursor armed = single armed"
for t in "pads|$PADS" "ts|$(midiseq "$TSJ1")" "old|$(midiseq "$OLDJ1")"; do
    name="${t%%|*}"; inp="${t#*|}"
    one=$(scen "s1$name" "$CURSOR" "$ARM_3_ONLY" "" "$inp")
    many=$(scen "s2$name" "$CURSOR" "" "" "$inp")
    expect "S $name: all armed on 3 = track 3 alone" "$one" "$many"
done
# two chords and a lone note after them, then the same again on the MIDI track
SEQ="$PADS; padoff 19 300; padon 14 100; padoff 14 400; padon 12 5; padon 16 5; padon 19 300; padoff 19 5; padoff 16 5; padoff 12 150"
one=$(scen s3one "$CURSOR" "$ARM_3_ONLY" "" "$SEQ"); many=$(scen s3many "$CURSOR" "" "" "$SEQ")
expect "S seq: chord, lone note, chord: all armed = track 3 alone" "$one" "$many"
one=$(scen s4one "$CURSOR" "$(fkeys 1 2 3 4 5 6 7 8); $PAGE_NEXT; $(fkeys 2 3 4 5 6 7 8); $PAGE_PREV" "$PAGE_NEXT" "$SEQ")
many=$(scen s4many "$CURSOR" "" "$PAGE_NEXT" "$SEQ")
expect "S seq on the MIDI page: all armed = track 9 alone" "$one" "$many"

# ---- T. track types and notes that cannot join
echo "-- T: track and instrument types"
expect "T1 sample instrument on the cursor track 3 (not MIDI/synth): no join, E and G are dropped, tracks 4 and 5 stay empty" "3:5=60 3:7=-4" "$(scen t1 "$CURSOR_S" "" "" "$PADS")"
expect "T2 two notes that are no chord (pad 12 and pad 24, an octave apart, the matcher has no chord for it): the second is dropped, track 4 stays empty" "3:5=60 3:7=-4" "$(scen t2 "$CURSOR" "" "" "padon 12 5; padon 24 300; padoff 12 5; padoff 24 150")"
expect "T3 one lone note, all armed: written on the cursor track as before" "3:5=60 3:7=-4" "$(scen t3 "$CURSOR" "" "" "padon 12 300; padoff 12 150")"
T4="padon 12 5; padon 16 5; padon 19 5; padon 23 300; padoff 12 5; padoff 16 5; padoff 19 5; padoff 23 150"
one=$(scen t4one "$CURSOR" "$ARM_3_ONLY" "" "$T4"); many=$(scen t4many "$CURSOR" "" "" "$T4")
expect "T4 four pads (C E G and the next pad): all armed = track 3 alone" "$one" "$many"

# a note that comes after the join window while the first is still held: the cursor track behaves as the only armed track, nothing goes to track 4
LATE="padon 12 5; padon 16 300; padoff 12 5; padoff 16 150"
one=$(scen t5one "$CURSOR" "$ARM_3_ONLY" "" "$LATE"); many=$(scen t5many "$CURSOR" "" "" "$LATE")
expect "T5 second note after the join window, first still held: all armed = track 3 alone" "$one" "$many"
expect "T5b ... and no cell on any other track" "0" "$(echo "$many" | tr ' ' '\n' | awk '$0 != "" && $0 !~ /^3:/ {n++} END {print n + 0}')"
expect "T6 MIDI chord on a sample instrument cursor track 3: only the first note, E and G dropped" "3:5=48 3:8=-4" "$(scen t6 "$CURSOR_S" "" "" "$(midiseq "$TSJ1")")"
# the cursor track step is taken (a step of 50 on every step of track 3): nothing is written there, nothing goes to track 4
FULL3=""; for i in $(seq 0 40); do FULL3="$FULL3 setstep 2 $i 50 63 0 0 0 0;"; done
expect "T7 cursor track 3 has every step taken: the chord is dropped, tracks 4 and up stay empty" "$(run t7ref "$CURSOR; $FULL3 $START; $STOP; $STEPS" | cells)" "$(run t7 "$CURSOR; $FULL3 $START; $PADS; $STOP; $STEPS" | cells)"

# ---- P. Instr pads record instruments, they keep the stock spread (a different instrument per pad on the next armed track)
echo "-- P: Instr pads"
INSTR_PADS="tap Instr; wait 300"
expect "P1 Instr pads, all armed: stock, one instrument per track from the cursor, no join" "3:5=60 4:5=60 5:5=60" "$(scen p1 "$GO" "" "$INSTR_PADS" "padon 3 5; padon 4 5; padon 5 300; padoff 3 5; padoff 4 5; padoff 5 150")"

if [ -n "$SAN" ]; then   # the sanitizers print to stderr, which every run kept. Known and not ours: stock UBSan nan to short in mtWavetableEnvelopes.cpp:206
    bad=$(cat "$WORK"/*.err 2>/dev/null | grep -E 'runtime error|Sanitizer' | grep -vc 'mtWavetableEnvelopes.cpp:206')
    expect "new sanitizer reports in the $SAN runs (the stock mtWavetableEnvelopes.cpp:206 one is left out)" "0" "$bad"
fi

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
