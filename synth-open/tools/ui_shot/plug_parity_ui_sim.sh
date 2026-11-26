#!/bin/bash
# plug_parity_ui_sim.sh: plugin synth engines on Tracker+ and Mini through the real core (sequencer, Synth, MidiAdapter) of the plus or mini
# sim, the closed libPolySynth and PlugSynthVoice.cpp in its Unicorn guest. The rows play one pattern step of synth part 1 and compare WAVs.
# PRODUCT: plus (default) or mini. PLG_DIR: the .plg files (ju6.plg alco.plg, build/tests/modules of run_tests). BIN: a ui_shot to run
# instead of building one (a mutant build). DEVTOOLS_DIR: the console checkout the sim builds need (as plus_smoke.sh).
# Rows (step = note 60 of synth part 1, instr 64, MIDI Chord FX 14 value 22):
#   U1  JU6  Chord Mode Para, MIDI Chord step == Para patch with Chord 22 and a plain step, bit equal (one paraphonic note on)
#   U2  JU6  Chord Mode Poly, the same MIDI Chord step != U1 (the stock path: one note on per chord note)
#   U3  ALCO the U1 row
#   U4  JU6  Para, MIDI Chord 22 + Strum FX (47) 10 == Para patch with Chord 22 and Strum 10, plain step, bit equal
# New note action, NNA step FX (54) and note ids: ALCO on part 1 with 4 voices and Amp Sustain 1, note 60 on step 1 and 64 on step 5
#   U5  part New note Cut (Amp Release 2 s): two blocks after step 5 (the WAV is not block aligned) bit equal to step 5's note alone, Release is not (the tail)
#   U6  Fade: bit equal to the note alone 42 blocks (41 fade blocks, 120 ms, + 1) after step 5, not at 20 blocks
#   U7  NNA step FX Cut on step 1 of a Release part: as U5
#   U8  Continue (Amp Release 20 ms): 100-200 ms after step 5 the old note still sounds (rms over 1.3 x the Release run)
#   U9  ACD (a stock engine) at Cut plays as Release, bit equal: the stock engines keep their behaviour
#   U10 two tracks on pitch 60 (track 1 step 1, track 2 step 3), an Off on track 1 step 5: track 2's note sounds on (rms within 5 % of track 2 alone)
# MPE in on the Synth play page (zone 15 members, raw USB MIDI bytes, "poke plugmpe 0": note:chan:gate:raw bend:pressure of the part's plugin voices),
# the rows of imx_midi_mpe_sim.sh I1 I2 I4 on the original Tracker:
#   M1  member bends at the note on (2048 / -1024 / 1234 on channels 2 / 3 / 4), one more bend on channel 3 moves that note only
#   M2  the released note of a channel is a tail that keeps its bend, the channel's next note starts at the last bend and alone follows the next
#   M3  member pressure reaches its note, the master channel's every note, the member again its own note
#   M4  the render takes the pressure: member pressure 0 at Press 50 % is half the rms
#   M5  the render takes the bend: member bend 2048 at the 48 st range is one octave (the spectrum peak doubles)
#   M6  ACD (a stock engine): member bend 2048 and pressure 0 == bend 0 and pressure 127 (the same messages), bit equal: the stock parts stay part wide
# The Scala mirror: a project.mtff with a Scala table (tune.cents, cfg.file) reaches the guest through the plus sim's copy of tuning::cfg and degMc
#   U11 ALCO, the table with degree 2 at 150 cents: key 62 == key 63 of 24 notes per octave (both 61.5 semitones), bit equal, not with degree 2 at 200 cents
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PRODUCT="${PRODUCT:-plus}"
PLG_DIR="${PLG_DIR:?PLG_DIR=<dir with ju6.plg and alco.plg>}"
# the console checkout the plus and mini sims build with, found as plus_ttype_vel_sim.sh finds it (the same build dir and flags, so run_tests builds it once)
if [ -z "${BIN:-}" ] && [ -z "${DEVTOOLS_DIR:-}" ]; then
  for d in "/Volumes/StoreCI/prj/poly-devtools/.wt/rel-200-int" "$HOME/prj/poly-devtools/.wt/rel-200-int" "$HOME/prj/poly-devtools/.wt/main" "$HOME/prj/poly-devtools"; do
    grep -q UISHOT_PLUS "$d/firmware/include/UsbTest.h" 2>/dev/null && { DEVTOOLS_DIR="$d"; break; }
  done
  [ -n "${DEVTOOLS_DIR:-}" ] || { echo "plug_parity_ui_sim: SKIP (no devtools checkout with the UISHOT_PLUS guards, set DEVTOOLS_DIR)"; exit 0; }
fi
W="${OUT:-$ROOT/build/plug_parity_ui_$PRODUCT}"
rm -rf "$W"; mkdir -p "$W/card/Plugins/Synth"
cp "$PLG_DIR/ju6.plg" "$PLG_DIR/alco.plg" "$W/card/Plugins/Synth/" || exit 2
if [ -z "${BIN:-}" ]; then
  . "$HERE/bridge_env.sh"
  bridge_env "$ROOT" > "$W/bridge_env.log" 2>&1 || { echo "bridge_env failed ($W/bridge_env.log)"; exit 2; }
  P=(); B="$ROOT/build/ui-shot-plus-console-bridge"
  [ "$PRODUCT" = mini ] && { P=(PRODUCT=MINI); B="$ROOT/build/ui-shot-mini-console-bridge"; }
  # OUT on the command line: the caller's OUT (this script's work dir, run_tests sets it) must not reach the Makefile's OUT
  make -C "$HERE" -j"${JOBS:-8}" PLATFORM=IMX "${P[@]}" CONSOLE=1 "${BRIDGE_ARGS[@]}" DEVTOOLS_DIR="${DEVTOOLS_DIR:-}" OUT="$B" > "$W/make.log" 2>&1 \
    || { echo "build failed ($W/make.log)"; exit 2; }
  BIN="$B/ui_shot"
fi
pass=0; fail=0
row() { if [ "$2" = 0 ]; then echo "PASS $1"; pass=$((pass + 1)); else echo "FAIL $1"; fail=$((fail + 1)); fi; }
# run NAME ENGINE "PRE" "STEP ARGS": pick the engine on synth part 1, PRE pokes, the step, play 800 ms into NAME.wav
run() {
  rm -rf "$W/card_$1"; cp -R "$W/card" "$W/card_$1"
  local s="wait 500; instr 64; tap Params; wait 1500; pickmodel $2; wait 300; tap Pattern; wait 300; $3; wait 300; setstep 0 0 $4; audio 1; wav $W/$1.wav; tap Play; wait 800; tap Play; wait 400; wav off"
  (cd "$W" && UISHOT_POKE="$W/poke_$1" ASAN_OPTIONS=detect_leaks=0 timeout 240 "$BIN" "$W/card_$1" "$s" > "$W/$1.log" 2>&1)
  grep -q "^pickmodel $2 ok" "$W/$1.log" || echo "  $1: pickmodel $2 did not pass ($W/$1.log)"
}
same() { [ -s "$W/$1.wav" ] && cmp -s "$W/$1.wav" "$W/$2.wav"; }
run ju6_para JU6 "poke synthparam 0 Chord_Mode 1" "60 64 14 22 0 0" &
run ju6_ref JU6 "poke synthparam 0 Chord_Mode 1; poke synthparam 0 Chord 22" "60 64 0 0 0 0" &
run ju6_poly JU6 "poke synthparam 0 Chord 22" "60 64 14 22 0 0" &
run alco_para ALCO "poke synthparam 0 Chord_Mode 1" "60 64 14 22 0 0" &
wait
run alco_ref ALCO "poke synthparam 0 Chord_Mode 1; poke synthparam 0 Chord 22" "60 64 0 0 0 0" &
run ju6_strum JU6 "poke synthparam 0 Chord_Mode 1" "60 64 14 22 47 10" &
run ju6_sref JU6 "poke synthparam 0 Chord_Mode 1; poke synthparam 0 Chord 22; poke synthparam 0 Strum 10" "60 64 0 0 0 0" &
wait
same ju6_para ju6_ref; row "U1 $PRODUCT ju6 Para MIDI Chord step == patch Chord 22 bit equal" $?
! same ju6_poly ju6_para && [ -s "$W/ju6_poly.wav" ]; row "U2 $PRODUCT ju6 Poly MIDI Chord step differs (stock separate notes)" $?
same alco_para alco_ref; row "U3 $PRODUCT alco Para MIDI Chord step == patch Chord 22 bit equal" $?
same ju6_strum ju6_sref && ! same ju6_strum ju6_para; row "U4 $PRODUCT ju6 Para chord + Strum FX 10 == patch Chord 22 Strum 10 bit equal" $?
# ---- step 5 and 6: New note action, NNA step FX, note ids (ALCO on part 1, 4 voices, Amp Sustain 1; 120 BPM, a step is 125 ms)
win() { "$PY" -I - "$@" <<'EOF'
import sys, wave, numpy as np
def rd(n):
    w = wave.open(n); return np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(float).reshape(-1, 2)
op = sys.argv[1]
if op == 'onset':   # the first block (128 frames) with sound
    a = rd(sys.argv[2]); nz = np.nonzero(np.abs(a).max(axis=1))[0]; print((int(nz[0]) // 128) * 128 if len(nz) else -1)
elif op == 'rms':   # rms FILE F0 F1 (frames)
    a = rd(sys.argv[2]); print('%.2f' % np.sqrt((a[int(sys.argv[3]):int(sys.argv[4])] ** 2).mean()))
elif op == 'peak':  # the fundamental of the mono mix, Hz: the first autocorrelation maximum after the first zero crossing of it
    a = rd(sys.argv[2]).mean(axis=1)[:8820]; a = a - a.mean()
    r = np.correlate(a, a, 'full')[len(a) - 1:]; z = int(np.argmax(r < 0)); print('%.2f' % (44100.0 / (z + int(np.argmax(r[z:z + 2000]))) if z else 0))
else:               # from A B FRAME: equal from FRAME on
    a, b, f = rd(sys.argv[2]), rd(sys.argv[3]), int(sys.argv[4]); sys.exit(0 if len(a) == len(b) and np.array_equal(a[f:], b[f:]) else 1)
EOF
}
PY="${PY:-$HOME/prj/poly/build/pyenv/bin/python3}"
V4="poke voicecount 0 4; poke synthparam 0 Amp_Sustain 1"
run2() {   # run2 NAME ENGINE "PRE" "SETSTEPS": like run with any number of steps, plays 1.2 s
  rm -rf "$W/card_$1"; cp -R "$W/card" "$W/card_$1"
  local s="wait 500; instr 64; tap Params; wait 1500; pickmodel $2; wait 300; tap Pattern; wait 300; $3; wait 300; $4; audio 1; wav $W/$1.wav; tap Play; wait 1200; tap Play; wait 400; wav off"
  (cd "$W" && UISHOT_POKE="$W/poke_$1" ASAN_OPTIONS=detect_leaks=0 timeout 240 "$BIN" "$W/card_$1" "$s" > "$W/$1.log" 2>&1)
  grep -q "^pickmodel $2 ok" "$W/$1.log" || echo "  $1: pickmodel $2 did not pass ($W/$1.log)"
}
TWO="setstep 0 0 60 64 0 0 0 0; setstep 0 4 64 64 0 0 0 0"
ONE="setstep 0 4 64 64 0 0 0 0"
run2 k_alone ALCO "$V4; poke synthparam 0 Amp_Release 2" "$ONE" &
run2 k_rel ALCO "$V4; poke synthparam 0 Amp_Release 2" "$TWO" &
run2 k_cut ALCO "$V4; poke synthparam 0 Amp_Release 2; poke nna 0 2" "$TWO" &
run2 k_fade ALCO "$V4; poke synthparam 0 Amp_Release 2; poke nna 0 3" "$TWO" &
wait
run2 k_fx ALCO "$V4; poke synthparam 0 Amp_Release 2" "setstep 0 0 60 64 54 2 0 0; setstep 0 4 64 64 0 0 0 0" &
run2 c_cont ALCO "$V4; poke synthparam 0 Amp_Release 0.02; poke nna 0 1" "$TWO" &
run2 c_rel ALCO "$V4; poke synthparam 0 Amp_Release 0.02" "$TWO" &
run2 a_rel ACD "poke voicecount 0 4" "$TWO" &
wait
run2 a_cut ACD "poke voicecount 0 4; poke nna 0 2" "$TWO" &
run2 i_two ALCO "$V4; poke synthparam 0 Amp_Release 0.02" "setstep 0 0 60 64 0 0 0 0; setstep 1 2 60 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0" &
run2 i_one ALCO "$V4; poke synthparam 0 Amp_Release 0.02" "setstep 1 2 60 64 0 0 0 0" &
wait
S4=$(win onset "$W/k_alone.wav")
win from "$W/k_cut.wav" "$W/k_alone.wav" $((S4 + 256)) && ! win from "$W/k_rel.wav" "$W/k_alone.wav" $((S4 + 256))
row "U5 $PRODUCT alco New note Cut: == step 5 note alone within two blocks (onset frame $S4, WAV blocks are not 128 aligned), Release is not" $?
win from "$W/k_fade.wav" "$W/k_alone.wav" $((S4 + 42 * 128)) && ! win from "$W/k_fade.wav" "$W/k_alone.wav" $((S4 + 20 * 128))
row "U6 $PRODUCT alco New note Fade: == the note alone 42 blocks after it (41 fade + 1 alignment), not at 20" $?
win from "$W/k_fx.wav" "$W/k_alone.wav" $((S4 + 256)); row "U7 $PRODUCT alco NNA step FX Cut on a Release part: == the note alone within two blocks" $?
w0=$((S4 + 4410)); w1=$((S4 + 8820))   # 100 to 200 ms after step 5
rc=$(win rms "$W/c_cont.wav" $w0 $w1); rr=$(win rms "$W/c_rel.wav" $w0 $w1); ra=$(win rms "$W/k_alone.wav" $w0 $w1)
python3 -c "import sys; sys.exit(0 if $rc > 1.3 * $rr else 1)"; row "U8 $PRODUCT alco New note Continue: the old note sounds on (rms $rc vs Release $rr)" $?
cmp -s "$W/a_cut.wav" "$W/a_rel.wav" && [ -s "$W/a_cut.wav" ]; row "U9 $PRODUCT ACD (stock engine) New note Cut plays as Release, bit equal" $?
S2=$(win onset "$W/i_one.wav"); x0=$((S2 + 11025 + 2646)); x1=$((x0 + 4410))   # step 5 (2 steps after step 3) + 60 ms, 100 ms long
ri=$(win rms "$W/i_two.wav" $x0 $x1); r1=$(win rms "$W/i_one.wav" $x0 $x1)
python3 -c "import sys; sys.exit(0 if $r1 > 0 and abs($ri / $r1 - 1) < 0.05 else 1)"
row "U10 $PRODUCT alco note ids: the Off of track 1 leaves track 2's note 60 sounding (rms $ri, alone $r1)" $?
# ---- step 7: MPE in per note on the Synth play page (zone 15 members, raw MIDI bytes through the real USB MIDI driver, "poke plugmpe 0" prints
# the part's plugin voices note:chan:gate:raw bend:pressure)
b2() { local v=$(( $1 + 8192 )); echo "$(( v & 127 )) $(( (v >> 7) & 127 ))"; }
case "$PRODUCT" in mini) ENTER="hold Menu; tap Up; tap Left; unhold Menu; wait 800";; *) ENTER="hold Shift; tap Performance; unhold Shift; wait 500";; esac
run3() {   # run3 NAME ENGINE "SCRIPT": the engine on part 1, the Synth play page open with the MPE zone on
  rm -rf "$W/card_$1"; cp -R "$W/card" "$W/card_$1"
  local s="wait 500; instr 64; audio 1; tap Params; wait 1500; pickmodel $2; wait 300; tap Pattern; wait 300; $ENTER; poke synthset 14 15; $3"
  (cd "$W" && UISHOT_POKE="$W/poke_$1" ASAN_OPTIONS=detect_leaks=0 timeout 240 "$BIN" "$W/card_$1" "$s" > "$W/$1.log" 2>&1)
  grep -q "^pickmodel $2 ok" "$W/$1.log" || echo "  $1: pickmodel $2 did not pass ($W/$1.log)"
}
pm() { grep '^plugmpe 0' "$W/poke_$1.out" | sed -n "$2p" | cut -d' ' -f3-; }
CH="usbmidi 0xE1 $(b2 2048); usbmidi 0xE2 $(b2 -1024); usbmidi 0xE3 $(b2 1234); usbmidi 0x91 60 100; usbmidi 0x92 64 100; usbmidi 0x93 67 100; wait 100"
run3 m_bend ALCO "$CH; poke plugmpe 0; usbmidi 0xE2 $(b2 4096); wait 100; poke plugmpe 0; usbmidi 0x82 64 0; usbmidi 0x92 72 100; wait 100; poke plugmpe 0; usbmidi 0xE2 $(b2 100); wait 100; poke plugmpe 0" &
run3 m_press ALCO "poke synthset 16 50; usbmidi 0x91 60 100; usbmidi 0x92 64 100; usbmidi 0x93 67 100; wait 100; poke plugmpe 0; usbmidi 0xD2 32; wait 100; poke plugmpe 0; usbmidi 0xD0 0; wait 100; poke plugmpe 0; usbmidi 0xD2 127; wait 100; poke plugmpe 0" &
run3 m_p0 ALCO "poke synthset 16 50; usbmidi 0x91 60 100; wait 300; wav $W/m_p0.wav; wait 300; wav off" &
run3 m_p1 ALCO "poke synthset 16 50; usbmidi 0x91 60 100; usbmidi 0xD1 0; wait 300; wav $W/m_p1.wav; wait 300; wav off" &
wait
run3 m_b0 ALCO "usbmidi 0x91 60 100; wait 300; wav $W/m_b0.wav; wait 400; wav off" &
run3 m_b1 ALCO "usbmidi 0x91 60 100; usbmidi 0xE1 $(b2 2048); wait 300; wav $W/m_b1.wav; wait 400; wav off" &
run3 m_acd0 ACD "poke synthset 16 50; usbmidi 0xE1 $(b2 0); usbmidi 0x91 60 100; usbmidi 0x92 64 100; usbmidi 0xD2 127; wait 200; wav $W/m_acd0.wav; wait 300; wav off" &
run3 m_acd1 ACD "poke synthset 16 50; usbmidi 0xE1 $(b2 2048); usbmidi 0x91 60 100; usbmidi 0x92 64 100; usbmidi 0xD2 0; wait 200; wav $W/m_acd1.wav; wait 300; wav off" &
wait
[ "$(pm m_bend 1)" = "60:2:1:2048:127 64:3:1:-1024:127 67:4:1:1234:127" ] && [ "$(pm m_bend 2)" = "60:2:1:2048:127 64:3:1:4096:127 67:4:1:1234:127" ]
rc=$?; got=$(pm m_bend 2)   # the message's command substitution would reset $?
row "M1 $PRODUCT alco member bends 2048 / -1024 / 1234 start the notes of channels 2 / 3 / 4, one more bend on channel 3 moves that note only ($got)" $rc
[ "$(pm m_bend 3)" = "60:2:1:2048:127 64:0:0:4096:127 67:4:1:1234:127 72:3:1:4096:127" ] && [ "$(pm m_bend 4)" = "60:2:1:2048:127 64:0:0:4096:127 67:4:1:1234:127 72:3:1:100:127" ]
row "M2 $PRODUCT alco the released note of channel 3 is a tail (its bend kept), the channel's next note starts at its last bend and alone follows the next one" $?
[ "$(pm m_press 2)" = "60:2:1:0:127 64:3:1:0:32 67:4:1:0:127" ] && [ "$(pm m_press 3)" = "60:2:1:0:0 64:3:1:0:0 67:4:1:0:0" ] && [ "$(pm m_press 4)" = "60:2:1:0:0 64:3:1:0:127 67:4:1:0:0" ]
rc=$?; got=$(pm m_press 4)
row "M3 $PRODUCT alco pressure 32 on member 3 reaches that note only, the master channel's 0 every note, member 3 at 127 again its own note ($got)" $rc
r0=$(win rms "$W/m_p0.wav" 0 11025); r1=$(win rms "$W/m_p1.wav" 0 11025)
python3 -c "import sys; sys.exit(0 if $r0 > 0 and abs($r1 / $r0 - 0.5) < 0.03 else 1)"
row "M4 $PRODUCT alco member pressure 0 at Press 50 % sounds at half the level (rms $r1 vs $r0)" $?
f0=$(win peak "$W/m_b0.wav"); f1=$(win peak "$W/m_b1.wav")
python3 -c "import sys; sys.exit(0 if $f0 > 0 and abs($f1 / $f0 - 2) < 0.03 else 1)"
row "M5 $PRODUCT alco member bend 2048 at the 48 st range sounds an octave up (spectrum peak $f1 Hz vs $f0 Hz)" $?
cmp -s "$W/m_acd0.wav" "$W/m_acd1.wav" && [ -s "$W/m_acd0.wav" ]
row "M6 $PRODUCT ACD (stock engine): member bends and pressure leave the part as before, bit equal" $?
# ---- the Scala mirror: a project whose project.mtff holds a Scala table (tune.cents) reaches the guest through the plus sim's tuning mirror
# (host_plusproxy.cpp syncTuning copies tuning::cfg and tuning::degMc before every event)
mkws() {   # NAME "mtff text": the plugin card with a saved default project whose project.mtff is this text
  local sd="$W/card_$1"; rm -rf "$sd"; cp -R "$W/card" "$sd"
  (cd "$W" && UISHOT_POKE="$W/poke_$1" ASAN_OPTIONS=detect_leaks=0 timeout 120 "$BIN" "$sd" "wait 200; poke projsave" > /dev/null 2>&1)
  printf '%b' "$2" > "$sd/Workspace/project.mtff"
}
run4() {   # run4 NAME KEY: ALCO on part 1 of the card NAME (mkws), one step of KEY
  local s="wait 500; instr 64; tap Params; wait 1500; pickmodel ALCO; wait 300; tap Pattern; wait 300; setstep 0 0 $2 64 0 0 0 0; audio 1; wav $W/$1.wav; tap Play; wait 600; tap Play; wait 300; wav off"
  (cd "$W" && UISHOT_POKE="$W/poke_$1" ASAN_OPTIONS=detect_leaks=0 timeout 240 "$BIN" "$W/card_$1" "$s" > "$W/$1.log" 2>&1)
}
mkws s_scl 'tffproj 1\ntune.n 12\ntune.file q.scl\ntune.cents 100000 150000 300000 400000 500000 600000 700000 800000 900000 1000000 1100000\n' &
mkws s_scl2 'tffproj 1\ntune.n 12\ntune.file w.scl\ntune.cents 100000 200000 300000 400000 500000 600000 700000 800000 900000 1000000 1100000\n' &
mkws s_t24 'tffproj 1\ntune.n 24\n' &
wait
run4 s_scl 62 & run4 s_scl2 62 & run4 s_t24 63 &
wait
cmp -s "$W/s_scl.wav" "$W/s_t24.wav" && [ -s "$W/s_scl.wav" ] && ! cmp -s "$W/s_scl2.wav" "$W/s_t24.wav"
row "U11 $PRODUCT alco Scala table of the project (degree 2 at 150 c) key 62 == 24 EDO key 63 bit equal, degree 2 at 200 c is not" $?
echo "RESULT pass $pass fail $fail"
[ $fail = 0 ]
