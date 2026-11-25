#!/bin/bash
# synth_mpe_sim.sh
#
# MPE in, phase 1 (ref/mpe-in-p1.md): the Synth play page with the lower zone, on the real UI, the real mtMidi.cpp handlers, the real router, the real
# shim and the ACD engine of this tree (ui_shot, host only, nothing here touches a device). The MIDI is injected with the script commands midiono /
# midionoff / midibend / midicc / midipress (ui_shot_main.cpp): the functions the USB driver's callbacks call, never a poke into voice state. The pokes
# only READ ("poke mpevoices": slot:note:chan:gate:raw bend:pressure:gain x 1000 of every voice that is on, "poke synthplay": bend and the router's table).
# Pitch is read the way microtune_synth_sim.sh does it, by the zero crossings of the master mix. To hear ONE voice of a chord, the Press > Vol row is
# 100 % and every other member's pressure is 0 (that silences it), so the three voices keep sounding with their own bends while one is measured.
#   M1  3 note chord (C E G on members 2 3 4) with bends +12 st, -6 st and 0 (range 48): each voice at its own pitch. M1d range 24 st (not the default):
#       the same raw bend is +6 st. M1e zone of 3 members: a note on channel 6 is outside it, its bend is the part's wheel (2 st range)
#   M2  pressure: Press > Vol 50 % (not the default 0): pressure 32 on member 3 lowers that voice only, the master channel's pressure 0 halves every voice,
#       member 3 back to full leaves it alone at full. M2b amount 0 (the default): the same messages change no level
#   M3  steal: 15 member notes and a 16th on the master channel, 8 voices: the oldest 8 are gone, the router's 8 cells and the part's voices are the same
#       notes. M3b Voices 4 (not the default 8): the 4 newest
#   M4  note off after stealing: a member's note off releases its voice only, the note off of a stolen channel changes nothing, a member channel that is
#       struck again after its note off leaves the older one as a tail with its own bend (not the channel's), CC 123 on the master channel releases all
#   M5  master channel bend: the part's wheel adds to a member's bend on every voice (Bend 4 st, not the default 2: +2 st master +6 st member), the wheel
#       alone moves a voice with no member bend
#   M6  zone Off is today's firmware: the master mix of a script of notes, bends and CCs on channels 1..3 hashes to goldens/mpe_zone_off.txt, written by the
#       ui_shot of release-2.0.0-int 6f6b9cd4 (UPDATE=1 BIN=<that binary>). Zone 15 (not the off) is a different mix
#   M7  event burst: 1,500 events a second for 2 s (bend, pressure, slide round 5 held member notes), every voice holds the last value sent on its channel,
#       the audio blocks of the 2 s are all there, and the notes end with their note offs
#   M8  RPN 6 zone message and RPN 0 member range as a controller sends them: the page's zone and range follow, a message that does not change the zone
#       lets go of nothing, one that does releases the notes
# MPE_MUTANT=1 switches the zone off after entering the page in every row (and on in M6): the rows must go RED, run_tests asserts it.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/synth_mpe_sim.XXXXXX")"
trap 'rm -rf "${WORK:?}"' EXIT
if [ -z "${BIN:-}" ] && [ -z "${UI_SHOT_BIN:-}" ]; then
    mkdir -p "$BUILD"
    make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }
fi
BIN="${BIN:-${UI_SHOT_BIN:-$BUILD/ui_shot}}"
[ -x "$BIN" ] || { echo "synth_mpe_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
GOLD="$HERE/goldens/mpe_zone_off.txt"
MUT="${MPE_MUTANT:-}"

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
near() { awk -v a="$1" -v b="$2" -v tol="${3:-0.01}" 'BEGIN {d = a - b; if (d < 0) d = -d; exit !(b > 0 && d <= b * tol)}'; }
# expect_near NAME WANT GOT TOL
expect_near() { checks=$((checks+1)); if near "$3" "$2" "${4:-0.01}"; then printf 'ok    %s (%s, want %s)\n' "$1" "$3" "$2"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s (+-%s)\n      got:      %s\n' "$1" "$2" "${4:-0.01}" "$3"; fi; }

ENTER="hold Shift; tap Performance; unhold Shift; wait 300"
# the zone switch of the mutant: every row except M6 runs with the zone off, M6 with it on
ZMUT=""; [ -n "$MUT" ] && ZMUT="poke synthset 14 0;"
# run NAME "script": the project starts empty, part 1 is instrument 64 (ACD), the page entered, audio on. $WORK/NAME.poke.out holds what the pokes printed,
# $WORK/NAME.out the audiostat lines
run() {
    local n="${1:?}"
    rm -rf "${WORK:?}/${n:?}"; mkdir -p "$WORK/$n"
    UISHOT_POKE="$WORK/$n.poke" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$n" "wait 500; instr 64; audio 1; $ENTER; $ZMUT $2" >"$WORK/$n.out" 2>"$WORK/$n.err"
    [ -f "$WORK/$n.poke.out" ] || : >"$WORK/$n.poke.out"
}
MV='poke mpevoices 0'; PS='poke synthplay'
# the Hz of the master mix from the audiostat dump of a run (zero crossings of the left channel of the loud blocks)
hz() { awk '$1 == "lr" && $4 > 300 {v++; if (v > 5) {z += $8; n++}} END {printf "%.2f", n ? z / 2 / (n * 128 / 44117.647) : 0}' "$WORK/$1.out"; }
MEAS="wait 150; audiostat 1; wait 400; audiostat 0; audiostat lrdump"
# vf NAME K CHAN FIELD: field (1 slot 2 note 3 chan 4 gate 5 raw bend 6 pressure 7 gain) of the voice of channel CHAN in the Kth "poke mpevoices" print
vf() { grep '^mpevoices 0 ' "$WORK/$1.poke.out" | sed -n "$2p" | tr ' ' '\n' | awk -F: -v c="$3" -v f="$4" 'NF == 7 && $3 == c {print $f; exit}'; }
# vfn NAME K NOTE FIELD: the same for the voice of a NOTE (a tail has channel 0)
vfn() { grep '^mpevoices 0 ' "$WORK/$1.poke.out" | sed -n "$2p" | tr ' ' '\n' | awk -F: -v c="$3" -v f="$4" 'NF == 7 && $2 == c {print $f; exit}'; }
# vnotes NAME K: the notes of the voices that are on, sorted, space separated. vgates: the gate flags of the voices in note order "note:gate"
vnotes() { grep '^mpevoices 0 ' "$WORK/$1.poke.out" | sed -n "$2p" | tr ' ' '\n' | awk -F: 'NF == 7 {print $2}' | sort -n | tr '\n' ' ' | sed 's/ $//'; }
vgates() { grep '^mpevoices 0 ' "$WORK/$1.poke.out" | sed -n "$2p" | tr ' ' '\n' | awk -F: 'NF == 7 {print $2 ":" $4}' | sort -n | tr '\n' ' ' | sed 's/ $//'; }
# the router's cells in the Kth "poke synthplay" print, notes sorted
cellnotes() { grep '^synthplay cells' "$WORK/$1.poke.out" | sed -n "$2p" | sed 's/^synthplay cells *//' | tr ' ' '\n' | sed 's/:.*//' | sort -n | tr '\n' ' ' | sed 's/ $//'; }
bendv() { grep '^synthplay part 0 ' "$WORK/$1.poke.out" | sed -n "$2p" | awk '{print $5}'; }

# ============================================================= M1 pitch of the voices of a chord
# chord C4 E4 G4 on members 2 3 4, raw bends 2048 (+12 st at range 48), -1024 (-6 st), 0. KEEP is the member whose pressure stays full
chord() {   # chord NAME KEEP
    local keep="$2" t="" ch
    for ch in 2 3 4; do t="$t midipress $ch $([ "$ch" = "$keep" ] && echo 127 || echo 0);"; done
    run "$1" "poke synthset 16 100; midibend 2 2048; midibend 3 -1024; midibend 4 0; midiono 2 60 100; midiono 3 64 100; midiono 4 67 100; $t $MEAS"
}
chord m1a 2; chord m1b 3; chord m1c 4
expect_near "M1 voice of channel 2 (C4 +12 st, member bend 2048 of 8192 x 48): C5" 523.25 "$(hz m1a)" 0.015
expect_near "M1 voice of channel 3 (E4 -6 st, member bend -1024): the tritone under E4" 233.08 "$(hz m1b)" 0.015
expect_near "M1 voice of channel 4 (G4, no bend)" 392.0 "$(hz m1c)" 0.015
run m1d "poke synthset 15 24; poke synthset 16 100; midibend 2 2048; midiono 2 60 100; midiono 3 64 100; midipress 3 0; $MEAS"
expect_near "M1d member range 24 st (not 48): the raw bend 2048 is +6 st, C4 becomes F#4" 370.0 "$(hz m1d)" 0.015
run m1e "poke synthset 14 3; midibend 6 2048; midiono 6 60 100; $MEAS"
expect_near "M1e a zone of 3 members: channel 6 is outside it, its bend is the part's wheel (range 2 st: 2048 is +0.5 st)" 269.29 "$(hz m1e)" 0.015
run m1f "midibend 2 2048; midiono 2 60 100; wait 100; $MV; midibend 2 -4096; wait 100; $MV"
expect "M1 the raw bend is stored per voice as received and follows the channel while the key is down (2048 then -4096)" "2048 -4096" "$(vf m1f 1 2 5) $(vf m1f 2 2 5)"

# ============================================================= M2 pressure
run m2 "poke synthset 16 50; midiono 2 60 100; midiono 3 64 100; midiono 4 67 100; wait 100; $MV; midipress 3 32; wait 100; $MV; midipress 1 0; wait 100; $MV; midipress 3 127; wait 100; $MV"
g0=$(vf m2 1 3 7)
expect "M2 three voices of the same velocity start at the same level" "$g0 $g0" "$(vf m2 1 2 7) $(vf m2 1 4 7)"
want=$(awk -v g="${g0:-0}" 'BEGIN {printf "%d", g * (1 + 0.5 * (32 / 127 - 1))}')
expect_near "M2 pressure 32 on member 3 at 50 %: that voice is 1 - 0.5 x (1 - 32/127) of its level" "$want" "$(vf m2 2 3 7)" 0.02
expect "M2 ... and the other two voices keep their level (pressure of one channel is one voice's)" "$g0 $g0" "$(vf m2 2 2 7) $(vf m2 2 4 7)"
expect_near "M2 the master channel's pressure 0 puts the voice of channel 2 at half (50 %)" "$((g0 / 2))" "$(vf m2 3 2 7)" 0.02
expect_near "M2 ... member 3 at full pressure again is back at its full level" "$g0" "$(vf m2 4 3 7)" 0.02
expect_near "M2 ... while channel 4 stays at half" "$((g0 / 2))" "$(vf m2 4 4 7)" 0.02
run m2b "midiono 2 60 100; midiono 3 64 100; wait 100; midipress 3 10; midipress 1 0; wait 100; $MV"
expect "M2b Press > Vol 0 (the default): pressure is stored but no voice level changes" "$(vf m2b 1 2 7)" "$(vf m2b 1 3 7)"

# ============================================================= M3 stealing
steal16() {   # steal16 NAME "setup"
    local t="" ch
    for ch in 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do t="$t midiono $ch $((46 + ch)) 100; wait 10;"; done
    run "$1" "$2 $t midiono 1 72 100; wait 100; $MV; $PS"
}
steal16 m3 ""
expect "M3 8 voices: the 16th note (master channel) and the 7 before it sound, the 8 oldest are stolen" "56 57 58 59 60 61 62 72" "$(vnotes m3 1)"
expect "M3 the router's 8 cells are the same 8 notes as the voices" "$(vnotes m3 1)" "$(cellnotes m3 1)"
steal16 m3b "tap F1; enc -4; wait 100;"
expect "M3b Voices 4 (not the default 8): the 4 newest" "60 61 62 72" "$(vnotes m3b 1)"
expect "M3b ... the router still lists 8 notes (it does not know the page cap, as before MPE): the 4 voices are among them" "yes" "$(c=" $(cellnotes m3b 1) "; ok=yes; for n in $(vnotes m3b 1); do case "$c" in *" $n "*) ;; *) ok=no;; esac; done; echo $ok)"

# ============================================================= M4 note off after a steal
t=""; for ch in 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do t="$t midiono $ch $((46 + ch)) 100; wait 10;"; done
run m4 "$t midiono 1 72 100; wait 60; $MV; midionoff 12 58; wait 60; $MV; midionoff 2 48; midionoff 5 51; wait 60; $MV; midiono 12 70; midibend 12 1024; wait 60; $MV; midionoff 12 70; wait 60; $MV; midicc 1 123 0; wait 60; $MV; $PS"
expect "M4 before: 8 voices, all keys down" "56:1 57:1 58:1 59:1 60:1 61:1 62:1 72:1" "$(vgates m4 1)"
expect "M4 the note off of channel 12 releases the voice of channel 12 only (note 58)" "56:1 57:1 58:0 59:1 60:1 61:1 62:1 72:1" "$(vgates m4 2)"
expect "M4 the note offs of channels 2 and 5 (stolen long ago) change nothing" "56:1 57:1 58:0 59:1 60:1 61:1 62:1 72:1" "$(vgates m4 3)"
expect "M4 channel 12 struck again (note 70): the voice of the channel is the new note and has the bend 1024" "70 1024" "$(vf m4 4 12 2) $(vf m4 4 12 5)"
expect "M4 ... all 8 voices were busy: the new note took the released voice of note 58, the 7 held ones stay" "56 57 59 60 61 62 70 72" "$(vnotes m4 4)"
expect "M4 the note off of the new note 70 on channel 12 releases it and nothing else" "56:1 57:1 59:1 60:1 61:1 62:1 70:0 72:1" "$(vgates m4 5)"
expect "M4 CC 123 on the master channel: every gate off, the router's table empty" "56:0 57:0 59:0 60:0 61:0 62:0 70:0 72:0|" "$(vgates m4 6)|$(cellnotes m4 2)"

# M4b a channel struck again while its earlier note still rings (a release tail): the older voice stays as a tail with its own bend, the new note has the
# channel's, and the note off of the new note leaves the tail alone
run m4b "midibend 2 1024; midiono 2 60 100; midiono 3 64 100; wait 30; midionoff 2 60; wait 30; midibend 2 -512; midiono 2 62 100; wait 30; $MV; midibend 2 -1024; wait 30; $MV; midionoff 2 62; wait 30; $MV"
expect "M4b the tail (note 60, channel 0, released) keeps the bend of its finger (1024), the new note 62 on channel 2 has -512" "0 0 1024|2 1 -512" "$(vfn m4b 1 60 3) $(vfn m4b 1 60 4) $(vfn m4b 1 60 5)|$(vfn m4b 1 62 3) $(vfn m4b 1 62 4) $(vfn m4b 1 62 5)"
expect "M4b a later bend on channel 2 moves the new note only (-1024), the tail stays at 1024" "-1024 1024" "$(vfn m4b 2 62 5) $(vfn m4b 2 60 5)"
expect "M4b the note off of note 62 on channel 2 releases it, the tail and the other channel's note are untouched" "60:0 62:0 64:1" "$(vgates m4b 3)"

# ============================================================= M5 master bend
run m5a "tap F7; enc 2; wait 50; poke synthset 16 100; midibend 2 1024; midiono 2 60 100; midiono 3 64 100; midipress 3 0; midibend 1 4096; wait 100; $PS; $MEAS"
expect "M5 master wheel 4096 at Bend 4 st (not the default 2): the part's bend is 2 st" "2.000" "$(bendv m5a 1)"
expect_near "M5 voice of channel 2: C4 + 6 st (member 1024 at 48) + 2 st (master) = G#4" 415.30 "$(hz m5a)" 0.015
run m5b "poke synthset 16 100; midiono 2 60 100; midiono 3 64 100; midipress 2 0; midibend 1 4096; $MEAS"
expect_near "M5 the master wheel alone moves a voice that has no member bend (E4 + 1 st at Bend 2)" 349.23 "$(hz m5b)" 0.015

# ============================================================= M6 zone off is today's firmware
sixes() {   # sixes NAME "setup": the master blocks of a script on channels 1..3, as the base firmware understands it (no pressure command there)
    run "$1" "$2 midiono 1 60 100; midiono 2 64 90; midibend 2 3000; midiono 3 67 100; midicc 2 74 90; midibend 1 -2000; midicc 1 1 64; wait 100; audiostat 1; wait 300; midionoff 2 64; wait 200; midicc 1 123 0; wait 100; audiostat 0; audiostat lrdump"
    grep -E '^lr ' "$WORK/$1.out" | cksum | cut -d' ' -f1
}
if [ -n "${UPDATE:-}" ]; then sixes m6w "poke synthset 14 0;" > "$GOLD"; echo "wrote $GOLD"; fi
off=$(sixes m6 "poke synthset 14 $([ -n "$MUT" ] && echo 15 || echo 0);")
on=$(sixes m6on "poke synthset 14 15;")
expect "M6 zone Off: the master blocks hash to the firmware before MPE (golden of 6f6b9cd4)" "$(cat "$GOLD" 2>/dev/null)" "$off"
expect "M6 zone 15 (not the off): the same script is a different mix" "differs" "$([ "$on" != "$(cat "$GOLD" 2>/dev/null)" ] && echo differs || echo same)"
run m6p "poke synthset 14 0; poke synthset 16 100; midiono 2 60 100; midipress 2 0; midipress 1 0; wait 100; $MV"
expect "M6 zone Off: channel pressure is dropped (voice pressure still 127)" "127" "$(vf m6p 1 0 6)"

# ============================================================= M7 event burst
run m7 "poke synthset 16 100; midiono 2 60 100; midiono 3 62 100; midiono 4 64 100; midiono 5 65 100; midiono 6 67 100; wait 100; audiostat 1; mpeburst 2 1500 2 6; audiostat 0; $MV; midionoff 2 60; midionoff 3 62; midionoff 4 64; midionoff 5 65; midionoff 6 67; wait 60; $MV; $PS; audiostat lrdump"
sent=$(grep '^mpeburst sent' "$WORK/m7.out" | awk '{print $3}')
expect "M7 1,500 events a second for 2 s all went through the handlers" "3000" "$sent"
exp_b=""; got_b=""; exp_p=""; got_p=""
for ch in 2 3 4 5 6; do
    exp_b="$exp_b $(grep "^mpeburst last ch $ch " "$WORK/m7.out" | awk '{print $6}')"; got_b="$got_b $(vf m7 1 $ch 5)"
    exp_p="$exp_p $(grep "^mpeburst last ch $ch " "$WORK/m7.out" | awk '{print $8}')"; got_p="$got_p $(vf m7 1 $ch 6)"
done
expect "M7 every voice holds the last bend sent on its channel (no event lost or applied to the wrong voice)" "$exp_b" "$got_b"
expect "M7 ... and the last pressure" "$exp_p" "$got_p"
nb=$(grep -c '^lr ' "$WORK/m7.out")
expect "M7 the audio kept running through the burst: no block missing (2 s = 689 blocks of 128 frames, at most 2 off)" "ok" "$(awk -v n="$nb" 'BEGIN {print (n >= 686 && n <= 692) ? "ok" : "blocks " n}')"
expect "M7 the notes end with their note offs: every gate off, no cell left" "0|" "$(vgates m7 2 | grep -o ':1' | wc -l | tr -d ' ')|$(cellnotes m7 1)"
printf 'info  M7 %s\n' "$(grep '^mpeburst sent' "$WORK/m7.out")"

# ============================================================= M8 zone message and member range from a controller
run m8 "midiono 2 60 100; midiono 3 64 100; wait 60; $MV; midicc 1 101 0; midicc 1 100 6; midicc 1 6 15; wait 40; $MV; poke mpestate; midicc 1 6 4; wait 40; $MV; poke mpestate; midiono 5 70 100; wait 60; $MV; midicc 3 101 0; midicc 3 100 0; midicc 3 6 12; poke mpestate; midicc 1 101 0; midicc 1 100 6; midicc 1 6 0; poke mpestate"
expect "M8 two notes sound" "60 64" "$(vnotes m8 1)"
expect "M8 the zone message that repeats the zone (15, the default) lets go of nothing" "60:1 64:1" "$(vgates m8 2)"
expect "M8 RPN 6 with 4 members: the notes that sounded under the old zone are released" "60:0 64:0" "$(vgates m8 3)"
expect "M8 ... and the page's zone is 4, range 48" "mpestate zone 4 range 48 press 0 chg 1" "$(grep '^mpestate' "$WORK/m8.poke.out" | sed -n 2p)"
expect "M8 channel 5 is the last member of a zone of 4: note 70 on it is a member note" "70" "$(vf m8 4 5 2)"
expect "M8 RPN 0 on member channel 3 with 12 semitones: the range is 12" "12" "$(grep '^mpestate' "$WORK/m8.poke.out" | sed -n 3p | awk '{print $5}')"
expect "M8 RPN 6 with 0 members: the zone is off" "0" "$(grep '^mpestate' "$WORK/m8.poke.out" | sed -n 4p | awk '{print $3}')"

# ============================================================= M9 tuned project (phase 4): the member bend is in key steps of the tuning
# raw 2048 at range 48 is 12 member "semitones": 12 key steps of the tuning, so C4 (the 1/1 key, not moved by the tuning) lands 12 degrees up, 2^(12/N) of
# 261.63 Hz. In 12 notes that is the octave (M1). M9_MUTANT=1 drops the tuning right after it was set, the unscaled 12 semitones then play the octave
edo() {   # edo NAME N: one member note C4 with the bend 2048 in an N EDO project
    run "$1" "tune $2; $([ -n "${M9_MUTANT:-}" ] && echo 'tune reset;') poke synthset 16 100; midibend 2 2048; midiono 2 60 100; $MEAS"
}
for n in 19 31; do
    edo m9_$n $n
    expect_near "M9 $n EDO: member bend 2048 (range 48) moves C4 up 12 degrees (2^(12/$n) of 261.63 Hz), not 12 semitones" "$(awk -v n=$n 'BEGIN {printf "%.2f", 261.6256 * 2 ^ (12 / n)}')" "$(hz m9_$n)" 0.015
done
run m9_late "tune 19; $([ -n "${M9_MUTANT:-}" ] && echo 'tune reset;') poke synthset 16 100; midiono 2 60 100; wait 100; midibend 2 -2048; $MEAS"
expect_near "M9 19 EDO: a bend that arrives after the note on moves it by degrees too (-12 degrees, the octave down)" "$(awk 'BEGIN {printf "%.2f", 261.6256 * 2 ^ (-12 / 19)}')" "$(hz m9_late)" 0.015
# 12 notes: the zone 15 mix of the same script is the one before phase 4, to the bit (golden of the tree before the scaling, UPDATE_P4=1 BIN=<that binary>)
GOLD4="$HERE/goldens/mpe_zone_on_12.txt"
if [ -n "${UPDATE_P4:-}" ]; then sixes m9w "poke synthset 14 15;" > "$GOLD4"; echo "wrote $GOLD4"; fi
expect "M9 12 notes: a zone 15 script of notes and member bends renders the same bytes as before the tuning scale (golden of 57177935)" "$(cat "$GOLD4" 2>/dev/null)" "$(sixes m9on "poke synthset 14 15;")"

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
