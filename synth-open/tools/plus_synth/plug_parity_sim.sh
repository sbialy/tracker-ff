#!/bin/bash
# plug_parity_sim.sh: plugin synth engines on Tracker+ / Mini (PlugSynthVoice.cpp under the closed libPolySynth, run.py under Unicorn).
# The guest is the same lib and the same PlugSynthVoice for the Tracker+ and the Mini (one harness, the products differ only in the UI).
# PLG_DIR: the .plg files (pluck.plg alco.plg ju6.plg, build/tests/modules of run_tests). MUTANT_SRC: a PlugSynthVoice.cpp to build instead (the rows of the fix it lacks go RED).
# Rows:
#   P0  probe: the MidiEvent types the lib hands PlugVoice::onMidiEvent (bend yes, poly and channel aftertouch: printed, not asserted)
#   B1  PLK  bend -8192 before the note == note 58 without bend, bit equal (2 semitone range, the lib's own)
#   B2  ALCO bend -8192 before the note == note 58 without bend, bit equal
#   T1  PLK  project tuning 24 notes per octave (root 60): key 62 == note 61 untuned, bit equal (pitch = 60 + rel(62), open_synth.cpp noteOn)
#   T2  ALCO the same
#   S1  PLK  a Scala file tuning (cfg.file, degMc): 12 degrees with degree 2 at 150 cents, key 62 == key 63 of 24 notes per octave, bit equal (both 61.5
#       semitones), and not with degree 2 at 200 cents: the guest reads the Scala degrees the plus sim mirrors
#   S2  ALCO the same
#   B3  PLK  a bend of +8191 after the note on moves the held note from C4 to D4 (spectral peak 200-350 Hz within 0.3 % of note 62)
#   C0  probe: an instrument on MIDI channel 2 (synth part 3, Synth.cpp setMidiChannel(part)): PlugVoice sees event.channel 2 at note on
#   C1  JU6  PLUG_EXT_CHORD answer: Chord 24, Chord Mode 23, Strum 26, last chord 76. PLK answers none (-1)
#   C2  JU6  Para, a pending MIDI Chord 22 (noteChord) == Para with the patch's Chord at 22, bit equal (the chord for that note on only)
#   C3  ALCO the same (Chord 66, Chord Mode 69)
#   C4  JU6  Para, pending chord 22 and Strum FX 10 == the patch's Chord 22 and Strum 10, bit equal
#   C5  JU6  Para, a chord pending on part 2 (channel 1) does not reach the part on channel 0: == Para without a chord, bit equal
#   C6  JU6  Para, a chord pending for note 62 does not reach a note on of 60: == Para without a chord, bit equal
#   N0  probe: the lib hands a note on to the voice inside the send (the note id set around h_note is the one the voice sees)
#   N1  probe: two note ons of one pitch take two voices and ONE note off of that pitch reaches both (offs 2, both gated)
#   I1  ALCO 4 voices, Amp Sustain 1, Amp Release 20 ms: note 60 id 1, note 60 id 2 at 100 ms, note off 60 id 1 at 200 ms. 370-520 ms
#       is the level of the id 2 note alone (within 5 %), the same events without ids release both (below 5 %)
#   K1  ALCO 4 voices, Amp Release 2 s: note 60, at 200 ms its note off with Cut and note 62 on. From the block after the cut on, bit
#       equal to note 62 alone at 200 ms (the old note is silent), Release (end 0) is not (its tail sounds)
#   K2  the same with Fade: bit equal to note 62 alone from 41 blocks (120 ms) after the off, not at 20 blocks
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PY="${PY:-$HOME/prj/poly/build/pyenv/bin/python3}"
PLG_DIR="${PLG_DIR:?PLG_DIR=<dir with pluck.plg and alco.plg>}"
W="${OUT:-$ROOT/build/plug_parity_sim}"
mkdir -p "$W"
PLUGVOICE_SRC="${MUTANT_SRC:-$ROOT/core/synth/engines/PlugSynthVoice.cpp}" bash "$HERE/build.sh" "$W/h" > "$W/build.log" 2>&1 || { echo "build failed, $W/build.log"; exit 2; }
run() { "$PY" -I "$HERE/run.py" "$W/h/harness.elf" "$W/h/harness.nm" x "$W/$1.wav" --plg "$PLG_DIR/$2.plg" --on-ms 300 --off-ms 50 "${@:3}"; }
pass=0; fail=0
row() { if [ "$2" = 0 ]; then echo "PASS $1"; pass=$((pass + 1)); else echo "FAIL $1"; fail=$((fail + 1)); fi; }
f0() { "$PY" -I - "$1" <<'EOF'
import sys, wave, numpy as np
w = wave.open(sys.argv[1]); r = w.getframerate(); ch = w.getnchannels()
a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(float)[0::ch]
s = a[int(0.1 * r):int(0.3 * r)]; s = (s - s.mean()) * np.hanning(len(s))
n = 1 << 18; m = np.abs(np.fft.rfft(s, n)); f = np.arange(len(m)) * r / n
b = (f > 200) & (f < 350); k = int(np.argmax(np.where(b, m, 0)))   # the fundamental band of C4 to D4
y0, y1, y2 = np.log(m[k - 1:k + 2]); print('%.3f' % ((k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)) * r / n))
EOF
}
run probe pluck --evseen --event 8,255,31 --event 9,60,100 --event 10,100,0 | grep evseen | sed 's/^/P0 /'
for e in pluck alco; do
  run ${e}_bend $e --bend -8192 > /dev/null
  run ${e}_n58 $e --note 58 > /dev/null
  cmp -s "$W/${e}_bend.wav" "$W/${e}_n58.wav"; rc=$?
  n=2; [ $e = pluck ] && n=1
  row "B$n $e bend -8192 == note 58 bit equal" $rc
done
for e in pluck alco; do
  run ${e}_t24 $e --note 62 --tune 24,60,1200000,2616256 > /dev/null
  run ${e}_n61 $e --note 61 > /dev/null
  cmp -s "$W/${e}_t24.wav" "$W/${e}_n61.wav"; rc=$?
  n=2; [ $e = pluck ] && n=1
  row "T$n $e 24 EDO key 62 == note 61 bit equal" $rc
done
for e in pluck alco; do
  # a Scala file of 12 degrees with degree 2 at 150 cents: key 62 is 61.5 semitones, as key 63 of 24 equal steps (rel: 150000 mc / 1e5 = 3 x 0.5)
  run ${e}_scl $e --note 62 --scala 60,1200000,100000,150000,300000,400000,500000,600000,700000,800000,900000,1000000,1100000 > /dev/null
  run ${e}_t63 $e --note 63 --tune 24,60,1200000,2616256 > /dev/null
  run ${e}_scl2 $e --note 62 --scala 60,1200000,100000,200000,300000,400000,500000,600000,700000,800000,900000,1000000,1100000 > /dev/null
  cmp -s "$W/${e}_scl.wav" "$W/${e}_t63.wav" && ! cmp -s "$W/${e}_scl2.wav" "$W/${e}_t63.wav"; rc=$?
  n=1; [ $e = alco ] && n=2
  row "S$n $e Scala file (degree 2 at 150 c) key 62 == 24 EDO key 63 bit equal, degree 2 at 200 c is not" $rc
done
run pluck_n62 pluck --note 62 > /dev/null
run pluck_late pluck --event 8,255,31 > /dev/null
a=$(f0 "$W/pluck_late.wav"); b=$(f0 "$W/pluck_n62.wav")
python3 -c "import sys; a,b=$a,$b; sys.exit(0 if abs(a/b-1)<0.003 else 1)"; row "B3 pluck bend +8191 after note on: f0 $a Hz vs note 62 $b Hz" $?
ev=$(run chan alco --evseen --chan 2 | grep evchan); [ "${ev%% (*}" = "evchan 2" ]; row "C0 part channel at note on: $ev" $?
ci=$(run ci ju6 --chordinfo | grep chordinfo); cp=$(run cip pluck --chordinfo | grep chordinfo)
[ "$ci" = "chordinfo chord 24 mode 23 strum 26 last 76" ] && [ "$cp" = "chordinfo -1" ]; row "C1 ju6 $ci, pluck $cp" $?
same() { cmp -s "$W/$1.wav" "$W/$2.wav"; }
run ju6_pc ju6 --param 23=1 --chord 22 > /dev/null; run ju6_pp ju6 --param 23=1 --param 24=22 > /dev/null
same ju6_pc ju6_pp; row "C2 ju6 Para pending chord 22 == patch Chord 22 bit equal" $?
run alco_pc alco --param 69=1 --chord 22 > /dev/null; run alco_pp alco --param 69=1 --param 66=22 > /dev/null
same alco_pc alco_pp; row "C3 alco Para pending chord 22 == patch Chord 22 bit equal" $?
run ju6_ps ju6 --param 23=1 --chord 22,10 > /dev/null; run ju6_pps ju6 --param 23=1 --param 24=22 --param 26=10 > /dev/null
same ju6_ps ju6_pps && ! same ju6_ps ju6_pc; row "C4 ju6 pending chord 22 strum 10 == patch Chord 22 Strum 10 bit equal (and != unstrummed)" $?
run ju6_p0 ju6 --param 23=1 > /dev/null; run ju6_po ju6 --param 23=1 --chord 22,0,1 > /dev/null
same ju6_po ju6_p0 && ! same ju6_pc ju6_p0; row "C5 ju6 chord pending on part 2 leaves part 1 plain (and a chord differs)" $?
run ju6_pn ju6 --param 23=1 --chord 22,0,0,62 > /dev/null
same ju6_pn ju6_p0; row "C6 ju6 chord pending for note 62 leaves note 60 plain" $?
win() { "$PY" -I - "$@" <<'EOF'
import sys, wave, numpy as np
def rd(n):
    w = wave.open(n); return np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(float).reshape(-1, 2)
op = sys.argv[1]
if op == 'rms':   # rms FILE MS0 MS1
    a = rd(sys.argv[2]); r = 44100 / 1000; print('%.2f' % np.sqrt((a[int(float(sys.argv[3]) * r):int(float(sys.argv[4]) * r)] ** 2).mean()))
else:             # from A B FRAME: equal from FRAME on (exit 0)
    a, b, f = rd(sys.argv[2]), rd(sys.argv[3]), int(sys.argv[4]); sys.exit(0 if len(a) == len(b) and np.array_equal(a[f:], b[f:]) else 1)
EOF
}
t=$(run tag alco --tag 77 | grep evtag); [ "${t%% (*}" = "evtag 77" ]; row "N0 the voice sees the note id set around the send: $t" $?
r=$(run route alco --voices 4 --on-ms 400 --seq 100:on:60,200:off:60 | grep routing)
[ "$r" = "routing offs 2 offhit 2 ons 2 reused 0" ]; row "N1 one note off reaches both voices of the pitch: $r" $?
SUS="--plg $PLG_DIR/alco.plg --voices 4 --param 27=1 --param 28=0.02 --on-ms 500 --off-ms 20"
rr() { "$PY" -I "$HERE/run.py" "$W/h/harness.elf" "$W/h/harness.nm" x "$W/$1.wav" "${@:2}" > /dev/null; }
rr id $SUS --tag 1 --seq 100:on:60:2,200:off:60:1
rr noid $SUS --seq 100:on:60,200:off:60
rr one $SUS --no-first --seq 100:on:60:2
ri=$(win rms "$W/id.wav" 370 520); rn=$(win rms "$W/noid.wav" 370 520); r1=$(win rms "$W/one.wav" 370 520)
python3 -c "import sys; sys.exit(0 if $r1 > 0 and abs($ri / $r1 - 1) < 0.05 and $rn < 0.05 * $r1 else 1)"
row "I1 alco same pitch ids: id 2 sustains rms $ri (alone $r1), without ids both end ($rn)" $?
LONG="--plg $PLG_DIR/alco.plg --voices 4 --param 28=2 --on-ms 500 --off-ms 20"
rr cut $LONG --tag 1 --seq 200:off:60:1:2,200:on:62:2
rr fade $LONG --tag 1 --seq 200:off:60:1:3,200:on:62:2
rr rel $LONG --tag 1 --seq 200:off:60:1:0,200:on:62:2
rr alone $LONG --no-first --seq 200:on:62:2
B0=$(( (6 + 200 * 44100 / 1000 / 128) * 128 ))   # the frame of the 200 ms events (20 ms lead in, whole blocks)
win from "$W/cut.wav" "$W/alone.wav" $((B0 + 128)) && ! win from "$W/rel.wav" "$W/alone.wav" $((B0 + 128))
row "K1 alco Cut: == note 62 alone from the block after the off, Release is not" $?
win from "$W/fade.wav" "$W/alone.wav" $((B0 + 41 * 128)) && ! win from "$W/fade.wav" "$W/alone.wav" $((B0 + 20 * 128))
row "K2 alco Fade: == note 62 alone 41 blocks after the off, not at 20" $?
echo "RESULT pass $pass fail $fail"
[ $fail = 0 ]
