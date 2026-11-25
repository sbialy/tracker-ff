#!/bin/bash
# microtune_fine_sim.sh [EVIDENCE_DIR]
#
# Microtuning phase 3, commit 3: Finetune in cents when a tuning is on (core/libs/Audio/play_memory.cpp fineShift, mtTuning.h fineCents). The tuning rows (Project Settings >
# Tuning) and the Instrument Parameters knobs Tune (F3) and Finetune (F4) are driven through the real UI (tap F4, enc). Pitch of the sample track = zero crossings of the voice
# tap (instrument 0 is a 441 Hz sine) judged as a RATIO to the same key with the knob at 0 (the tap reads about 0.8 % low, a ratio cancels it), tolerance 0.4 %.
# Before this commit Finetune +100 was one KEY step: in 31 EDO 38.7 cents (x1.0226), so every case below that expects cents also fails on the old code.
#   F1  31 EDO: Finetune +50 is x1.0293 (50 cents, old code x1.0112), +100 is x1.0595, -100 is x0.9439
#   F2  17 EDO +50 and +100, a Scala file (just 5-limit, 7 notes) +100: cents. 12 EDO (default tuning) keeps its old meaning, which is cents too
#   F3  Tune stays key steps: +3 is the pitch of key 60+3 played alone, in 31 EDO (116 cents) and in 17 EDO (212 cents), never three semitones
# Not run on the Mini (no encoder to turn a knob). Host only. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
NAME=microtune_fine_sim
. "$(cd "$(dirname "$0")" && pwd)/microtune_p3_lib.sh"
EVID="${1:-}"; [ -n "$EVID" ] && mkdir -p "$EVID"
TUN="$ROOT/releases/tunings"
knob() {   # F-KEY VALUE: the enc commands that turn a knob by VALUE
    local v=$2 t="tap $1;"
    while [ "$v" -ge 3 ]; do t="$t enc 3;"; v=$((v-3)); done
    while [ "$v" -le -3 ]; do t="$t enc -3;"; v=$((v+3)); done
    [ "$v" -ne 0 ] && t="$t enc $v;"; echo "$t"; }
fpitch() {   # SD X KNOBS KEY: Hz of key KEY after UI commands X on the Project Settings page and KNOBS on the Instrument Parameters page. Every run gets its own copy of SD,
    # a run saves its project there (Tune and the tuning would carry over into the next run)
    local c="$WORK/run.$RANDOM"; rm -rf "$c"; cp -R "$1" "$c"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$c" "$GO $2 tap Pattern; wait 300; sampletone 0; instr 0; tap Params; wait 800; $3 setstep 0 0 $4 0 0 0 0 0; audio 1; audiostat tap 0; audiostat 1; tap Play; wait 700; tap Play; audiostat 0; audiostat tapdump" \
        2>>"$WORK/f.err" | grep -E '^blk ' | awk '$4 > 500 {v++; if (v > 2) {z += $6; n++}} END {printf "%.2f", n ? z / 2 / (n * 128 / 44117.647) : 0}'; }
rcase() {   # LABEL SD X KNOBS WANTRATIO: Hz with the knobs over Hz without them
    local ref got
    ref=$(fpitch "$2" "$3" "" 60); got=$(fpitch "$2" "$3" "$4" 60)
    if near "$got" "$(awk -v r="$ref" -v w="$5" 'BEGIN {printf "%.4f", r * w}')" 0.004; then check "$1 plays x$5 ($got Hz over $ref Hz)" ok; else check "$1" "want x$5 of $ref Hz, got $got Hz"; fi; }
sd="$WORK/f1"; mkdir -p "$sd/Tunings"; cp "$TUN/just-major-5limit.scl" "$sd/Tunings/"
T31="$(ed 0 19)"; T17="$(ed 0 5)"
c() { awk -v x="$1" 'BEGIN {printf "%.4f", 2 ^ (x / 1200)}'; }
rcase "F1 31 EDO Finetune +50" "$sd" "$T31" "$(knob F4 50)" "$(c 50)"
rcase "F1 31 EDO Finetune +100" "$sd" "$T31" "$(knob F4 100)" "$(c 100)"
rcase "F1 31 EDO Finetune -100" "$sd" "$T31" "$(knob F4 -100)" "$(c -100)"
rcase "F2 17 EDO Finetune +50" "$sd" "$T17" "$(knob F4 50)" "$(c 50)"
rcase "F2 17 EDO Finetune +100" "$sd" "$T17" "$(knob F4 100)" "$(c 100)"
rcase "F2 Scala file (just 5-limit, 7 notes) Finetune +100" "$sd" "$(ed 12 1)" "$(knob F4 100)" "$(c 100)"
rcase "F2 12 EDO (default tuning) Finetune +50 (the old meaning, cents too)" "$sd" "" "$(knob F4 50)" "$(c 50)"
# F3: Tune is judged against the exact pitch of key 60+N (the tap reads about 0.8 % low, so the want is the 12 TET reading 437.32 Hz times 2^(N/EDO))
tcase() {   # LABEL SD X N EDO: Tune +N on key 60 plays key 60+N of the N-EDO tuning
    local a want
    a=$(fpitch "$2" "$3" "$(knob F3 $4)" 60); want=$(awk -v n="$4" -v e="$5" 'BEGIN {printf "%.2f", 437.32 * 2 ^ (n / e)}')
    if near "$a" "$want" 0.004; then check "$1 ($a Hz, want $want Hz)" ok; else check "$1" "Tune plays $a Hz, want $want Hz"; fi; }
tcase "F3 31 EDO Tune +3 is three keys (116 cents, not 300)" "$sd" "$T31" 3 31
tcase "F3 17 EDO Tune +3 is three keys (212 cents, not 300)" "$sd" "$T17" 3 17
finish
