#!/bin/bash
# m8_modes_sim.sh [UI_SHOT_BIN [OUT_DIR]]
#
# The three M8 style sample play modes on the real audio engine of the host build (the firmware's own AudioPlayMemory, the sequencer and the
# master chain, no device). Every scenario renders a few notes to a WAV (the firmware's audio callback, 44,117.647 Hz clock) and
# m8_modes_check.py measures it:
#   rev    1-Shot Rev (enum 8) is 1-Shot read backwards: the level envelope of a crescendo sample is the time reverse of the 1-Shot one
#   ppr    Pingpong Rev (9) starts at the end of the loop side (loud first 10 ms for a crescendo), Pingpong (3) starts quiet
#   osc    Oscillator (10): a one cycle loop plays at the note pitch (C5 523.25 Hz, C6 1046.5 Hz), the Start point is ignored (a Loop Forward note
#          with the Start after the loop is refused and silent, the oscillator one sounds), the output is periodic
#   rep    Repitch (Steps in the high byte of wavetableCurrentWindow): a 0.5 s sample set to 4 steps lasts 0.5 s at 120 BPM, 0.25 s at 240, 1 s at 60,
#          8 steps at 120 last 1 s, and the pitch follows (441 Hz source: 441 Hz, 882 Hz, 220.5 Hz). Reverse and Pingpong also take it
#   mid    a tempo change while a Repitch note sounds: 220.5 Hz at 60 BPM, then 441 Hz after the change to 120 BPM, the note ends early
#   st     a stereo sample (L 441 Hz, R 882 Hz) in 1-Shot Rev, Pingpong Rev, Oscillator and Repitch: R keeps its own pitch ratio to L and its length
#   fxrev  the step FX Reverse on a 1-Shot Rev instrument plays forward (the XOR), on a 1-Shot one it plays backwards
# Ends with "RESULT: GREEN (n checks)" or "RESULT: RED (n failing of m)".  Needs the modes of the build in the binary (rows of absent modes fail).
#   ONLY="rev ppr osc rep mid st fxrev"  KEEP=DIR (keep the WAVs there)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-$ROOT/build/ui-shot-pm/ui_shot}"
WORK="${2:-$(mktemp -d "${TMPDIR:-/tmp}/m8_modes_sim.XXXXXX")}"
mkdir -p "$WORK"
[ -x "$BIN" ] || { echo "m8_modes_sim.sh: no ui_shot at $BIN (make -C synth-open/tools/ui_shot OUT=$ROOT/build/ui-shot-pm)" >&2; exit 2; }
ONLY="${ONLY:-rev ppr osc rep mid st fxrev}"
KEEP="${KEEP:-}"
CHK() { python3 -I "$HERE/m8_modes_check.py" "$@"; }
checks=0; fails=0
# the enum values of the modes (core/source/mtStructs.h)
SS=0; LF=1; PP=3; SSR=8; PPR=9; OSC=10

# render NAME TEMPO SETUP...: instrument 0 from SETUP, one note C5 at step 0 of a 64 step pattern, SECS of audio into $WORK/NAME.wav
STEPFX="-1 0 -1 0"
render() {
    local name="$1" tempo="$2" setup="$3" secs="${4:-1500}" note="${5:-60}" sd="$WORK/sd_$1"
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    UI_SHOT_FATFS_REOPEN=1 timeout 300 "$BIN" "$sd" "wait 500; $setup; tap Pattern; wait 300; tempo $tempo; plen 64; setstep 0 0 $note 0 $STEPFX; audio 1; wav $WORK/$name.wav; tap Play; ${MID:-wait 1}; wait $secs; tap Play; wav off" > "$WORK/$name.log" 2>&1 \
        || { echo "ui_shot failed ($name)"; tail -3 "$WORK/$name.log"; }
}
verdict() {   # verdict NAME RC OUTPUT
    checks=$((checks + 1))
    if [ "$2" -eq 0 ]; then echo "ok    $1: $3"; else echo "FAIL  $1: $3"; fails=$((fails + 1)); fi
    [ -n "$KEEP" ] && { mkdir -p "$KEEP"; cp "$WORK"/*.wav "$KEEP/" 2>/dev/null; }
}
has() { case " $ONLY " in *" $1 "*) return 0;; esac; return 1; }

CRES="samplecres 0 8000 441 1"
if has rev; then
    render rev-fwd 120 "$CRES"
    render rev-rev 120 "$CRES; instrmode 0 $SSR"
    out=$(CHK mirror "$WORK/rev-fwd.wav" "$WORK/rev-rev.wav"); verdict "1-Shot Rev mirrors 1-Shot" $? "$out"
    out=$(CHK startlevel "$WORK/rev-rev.wav" 0.7 1.01); verdict "1-Shot Rev starts at the loud end" $? "$out"
    out=$(CHK startlevel "$WORK/rev-fwd.wav" 0 0.25); verdict "1-Shot starts at the quiet end" $? "$out"
fi
if has ppr; then
    LOOPS="lp1=16384 lp2=49152"
    render ppr-fwd 120 "$CRES; instrmode 0 $PP $LOOPS"
    render ppr-rev 120 "$CRES; instrmode 0 $PPR $LOOPS"
    out=$(CHK startlevel "$WORK/ppr-rev.wav" 0.7 1.01); verdict "Pingpong Rev starts at the loud end" $? "$out"
    out=$(CHK startlevel "$WORK/ppr-fwd.wav" 0 0.25); verdict "Pingpong starts at the quiet end" $? "$out"
    # the loop runs on past the 0.18 s sample length: the sound lasts the whole 1.5 s capture
    out=$(CHK dur "$WORK/ppr-rev.wav" 1.5 12); verdict "Pingpong Rev keeps looping" $? "$out"
fi
if has osc; then
    # one cycle of 655 frames in a 65535 frame sample (one frame per point): 44100 / 655.35 Hz, flat level
    CYC="samplecres 0 65535 67.2958 0"
    render osc-c5 120 "$CYC; instrmode 0 $OSC lp1=0 lp2=655" 1200 60
    render osc-c6 120 "$CYC; instrmode 0 $OSC lp1=0 lp2=655" 1200 72
    render osc-start 120 "$CYC; instrmode 0 $OSC lp1=0 lp2=655 sp=40000" 1200 60
    render lf-start 120 "$CYC; instrmode 0 $LF lp1=0 lp2=655 sp=40000" 1200 60
    out=$(CHK freq "$WORK/osc-c5.wav" 523.25 1.5); verdict "Oscillator C5 is 523.25 Hz" $? "$out"
    out=$(CHK freq "$WORK/osc-c6.wav" 1046.5 1.5); verdict "Oscillator C6 is 1046.5 Hz" $? "$out"
    out=$(CHK freq "$WORK/osc-start.wav" 523.25 1.5); verdict "Oscillator ignores the Start point" $? "$out"
    out=$(CHK silent "$WORK/lf-start.wav"); verdict "Loop Forward with Start after the loop is refused (control)" $? "$out"
fi
if has rep; then
    HALF="samplecres 0 22050 441 0"
    CYC="samplecres 0 65535 67.2958 0"
    for t in 60 120 240; do
        case $t in 60) d=1.0; f=220.5;; 120) d=0.5; f=441;; 240) d=0.25; f=882;; esac
        render rep4-$t $t "$HALF; instrmode 0 $SS steps=4" 2200
        out=$(CHK dur "$WORK/rep4-$t.wav" $d 3); verdict "Repitch 4 steps at $t BPM lasts $d s" $? "$out"
        out=$(CHK freq "$WORK/rep4-$t.wav" $f 2); verdict "Repitch 4 steps at $t BPM plays $f Hz" $? "$out"
    done
    render rep8-120 120 "$HALF; instrmode 0 $SS steps=8" 2200
    out=$(CHK dur "$WORK/rep8-120.wav" 1.0 3); verdict "Repitch 8 steps at 120 BPM lasts 1 s" $? "$out"
    render rep-off 120 "$HALF; instrmode 0 $SS" 1500
    out=$(CHK dur "$WORK/rep-off.wav" 0.5 3); verdict "no Steps: the sample length (0.5 s)" $? "$out"
    render rep-rev 240 "$HALF; instrmode 0 $SSR steps=4" 1500
    out=$(CHK dur "$WORK/rep-rev.wav" 0.25 3); verdict "Repitch with 1-Shot Rev at 240 BPM lasts 0.25 s" $? "$out"
    render rep-osc 240 "$CYC; instrmode 0 $OSC lp1=0 lp2=655 steps=4" 1200 60
    out=$(CHK freq "$WORK/rep-osc.wav" 523.25 1.5); verdict "Oscillator ignores Steps" $? "$out"
fi
if has mid; then
    HALF="samplecres 0 22050 441 0"
    # 4 steps at 60 BPM = 1 s at 220.5 Hz, the tempo goes to 120 BPM 0.25 s in: the rest of the sample (0.75) lasts 0.375 s at 441 Hz
    MID="wait 250; tempo 120" render mid-up 60 "$HALF; instrmode 0 $SS steps=4" 2200
    out=$(CHK dur "$WORK/mid-up.wav" 0.625 15); verdict "tempo 60 -> 120 mid note: the note ends early (0.625 s)" $? "$out"
    out=$(CHK freqwin "$WORK/mid-up.wav" 0.02 0.2 220.5 2); verdict "mid note: 220.5 Hz before the change" $? "$out"
    out=$(CHK freqwin "$WORK/mid-up.wav" 0.35 0.55 441 2); verdict "mid note: 441 Hz after the change" $? "$out"
    # and down: 120 BPM = 0.5 s at 441 Hz, to 60 BPM 0.16 s in: the rest (0.68) lasts 0.68 s at 220.5 Hz
    MID="wait 160; tempo 60" render mid-down 120 "$HALF; instrmode 0 $SS steps=4" 2200
    out=$(CHK freqwin "$WORK/mid-down.wav" 0.02 0.12 441 2); verdict "tempo 120 -> 60 mid note: 441 Hz before the change" $? "$out"
    out=$(CHK freqwin "$WORK/mid-down.wav" 0.4 0.7 220.5 2); verdict "mid note: 220.5 Hz after the change down" $? "$out"
fi
if has st; then
    # stereo (a linked pair, as on the unit) 22050 frames: L 441 Hz, R 882 Hz. The right channel must keep the 2:1 pitch ratio and the left length in every new mode
    STE="pair 1; samplecres 0 22050 441 0 882"
    CYCST="pair 1; samplecres 0 65535 67.2958 0 134.5916"
    render st-ssr 120 "$STE; instrmode 0 $SSR" 1500
    out=$(CHK dur "$WORK/st-ssr.wav" 0.5 3); lo=$?; out2=$(M8CH=1 CHK dur "$WORK/st-ssr.wav" 0.5 3); ro=$?
    verdict "stereo 1-Shot Rev: L and R last 0.5 s" $((lo + ro)) "$out / $out2"
    out=$(M8CH=1 CHK freq "$WORK/st-ssr.wav" 882 2); verdict "stereo 1-Shot Rev: R plays 882 Hz" $? "$out"
    render st-ppr 120 "$STE; instrmode 0 $PPR lp1=16384 lp2=49152" 1500
    out=$(CHK freq "$WORK/st-ppr.wav" 441 3); lo=$?; out2=$(M8CH=1 CHK freq "$WORK/st-ppr.wav" 882 3); ro=$?
    verdict "stereo Pingpong Rev: L 441 Hz, R 882 Hz" $((lo + ro)) "$out / $out2"
    render st-osc 120 "$CYCST; instrmode 0 $OSC lp1=0 lp2=655" 1200 60
    out=$(CHK freq "$WORK/st-osc.wav" 523.25 1.5); lo=$?; out2=$(M8CH=1 CHK freq "$WORK/st-osc.wav" 1046.5 1.5); ro=$?
    verdict "stereo Oscillator: L 523.25 Hz, R (two cycles in the loop) 1046.5 Hz" $((lo + ro)) "$out / $out2"
    render st-rep 240 "$STE; instrmode 0 $SS steps=4" 1500
    out=$(CHK dur "$WORK/st-rep.wav" 0.25 4); lo=$?; out2=$(M8CH=1 CHK dur "$WORK/st-rep.wav" 0.25 4); ro=$?
    verdict "stereo Repitch 4 steps at 240 BPM: L and R last 0.25 s" $((lo + ro)) "$out / $out2"
    out=$(CHK freq "$WORK/st-rep.wav" 882 2); lo=$?; out2=$(M8CH=1 CHK freq "$WORK/st-rep.wav" 1764 2); ro=$?
    verdict "stereo Repitch: L 882 Hz, R 1764 Hz" $((lo + ro)) "$out / $out2"
fi
if has fxrev; then
    # step FX Reverse = FX type 26 value 1: 1-Shot plays backwards, 1-Shot Rev with the FX plays forward again
    STEPFX="26 1 -1 0" render fx-ss 120 "$CRES" 1500
    STEPFX="26 1 -1 0" render fx-ssr 120 "$CRES; instrmode 0 $SSR" 1500
    out=$(CHK startlevel "$WORK/fx-ss.wav" 0.7 1.01); verdict "1-Shot + FX Reverse starts at the loud end" $? "$out"
    out=$(CHK startlevel "$WORK/fx-ssr.wav" 0 0.25); verdict "1-Shot Rev + FX Reverse plays forward (quiet end first)" $? "$out"
fi
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
