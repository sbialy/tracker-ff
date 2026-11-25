#!/bin/bash
# tables_sim.sh [EVIDENCE_DIR]
#
# Tables (Table lite, core/source/tables.h, tables_format.h): per instrument sub-step rows of transpose (N), volume (V) and one FX, played by
# the sequencer's per track runner, on the real firmware code of this tree (ui_shot, the plugin host configuration) on the Mac. Host only,
# no device. The sim plays a pattern with the firmware's own audio callback ("audio 1") and reads the audio back block by block:
#   sample track: "audiostat tap T" (the sample voice of track T, a 441 Hz sine in instrument 0, "sampletone 0": pitch from zero
#                 crossings) and "audiostat tapamp T" (the same voice times the gain of the track's amplifier: the level the step volume,
#                 the V column and a Volume FX set; the host's sample chain after the voice is silent, kb runbook)
#   synth track:  the master mix ("audiostat dump"), the Poly part (instrument 64) playing note 60
# Tempo 120: one step is 125 ms = 43.07 blocks of 128 frames, one uStep (1/48 step) 0.897 blocks. Tic 24 = half a step = 21.5 blocks per
# row, tic 12 = 10.8 blocks. Windows are in blocks from the onset (the first block with rms > 500) and leave 3 blocks at each row edge.
# Pitch within 3 % of the expected note (a semitone is 5.9 %), levels as ratios to row 0 of the same run.
#   1 N column on a sample track, Loop: 0 +12 +7 -5 then row 0 again, the row edges at k x tic
#   2 tic 12: the same rows twice as fast (timing of the runner)
#   3 Once: the last row holds
#   4 V column and the row FX on a sample track (tapamp): V 50 is half the voice gain (linear, user 2026-11-20), an empty V is the note's
#     volume again, a row FX (Volume 20) stays on through the next rows (an empty FX cell keeps the last FX)
#   5 TBL step FX (id 53): with a note and no binding, 0 with a note overrides the binding, on an empty step starts on the sounding note,
#     0 on an empty step stops and restores the voice
#   6 per track position: the same instrument on tracks 1 and 2, notes one step apart, each track runs its own rows
#   7 synth track: N and V through the part (transpose, gain), a row FX the synth does not take (Tune) changes nothing
#   8 project.mtff: tbl. and instr.N.table lines are loaded and play, an autosave writes them back byte identical, a project without
#     tables writes no table line
#  10 an Off step leaves a Loop table running through the release, a Once table ends on its own (runner state, "table run")
#  11 live notes: a pad and a USB MIDI note start the bound table with the sequencer stopped, the rows advance (runner state and pitch), the
#     release (pad or MIDI note off) ends a Loop table once its voice is silent (a synth part runs through its release first)
#  14 a project open (console open, File screen) binds the tables like the boot load, also for a project folder of another name and path
#  15 the FX picker (a held Fx, Vol or F8 key lists every FX a row takes: arrows, encoder, pads, steps from None, Delete, the release sets it)
#  12 the Table page (instrument editor, Params pressed three times): screens byte equal to goldens/tables/, edits through the encoder,
#     copy, paste and clear of rows and of a whole table ("table dump"), the synth part's page, the edits reach project.mtff
#   9 no sanitizer report (run last)
# TABLES_UPDATE=1 writes the goldens, TABLES_PNG_DIR=DIR copies every screenshot.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)". SAN=address,undefined: the sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
EVID="${1:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/tables_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
BIN="${BIN:-${UI_SHOT_BIN:-}}"   # UI_SHOT_BIN (or BIN): a prebuilt binary of this flavour, no make (run_tests)
if [ -z "${BIN:-}" ]; then
    BIN="$ROOT/build/ui-shot${SAN:+-san}/ui_shot"
    make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "tables_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
[ -n "$EVID" ] && mkdir -p "$EVID"
KEEP="${EVID:-$WORK}"

fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
# play NAME "setup" SRC [MS] [SD]: SRC tap T | tapamp T | mix. Blocks to $KEEP/NAME.blk, the rest of the output to $KEEP/NAME.out
play() {
    local sd="${5:-$WORK/$1}"; [ -z "${5:-}" ] && rm -rf "$sd"
    local stat="audiostat $3; audiostat 1;" dump=tapdump
    [ "$3" = mix ] && { stat="audiostat 1;"; dump=dump; }
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; tempo 120; $2 audio 1; $stat tap Play; wait ${4:-800}; tap Play; audiostat 0; audiostat $dump" \
        2>"$WORK/$1.err" >"$WORK/$1.raw"
    grep -E '^blk ' "$WORK/$1.raw" >"$KEEP/$1.blk"; grep -vE '^blk |^audiostat blocks|^wav ' "$WORK/$1.raw" >"$KEEP/$1.out"
}
# win NAME A B: "hz rms" of blocks onset+A .. onset+B (A, B may be fractional: rounded inward)
win() { python3 -I - "$KEEP/$1.blk" "$2" "$3" <<'PY'
import math, sys
rows = [l.split() for l in open(sys.argv[1])]
rms = [float(r[3]) for r in rows]; zc = [int(r[5]) for r in rows]
on = next((i for i, v in enumerate(rms) if v > 500), None)
if on is None:
    print("0 0"); sys.exit()
a, b = on + math.ceil(float(sys.argv[2])), on + math.floor(float(sys.argv[3]))
w = range(a, min(b, len(rms) - 1) + 1)
n = len(w)
print("%.1f %.1f" % (sum(zc[i] for i in w) / 2 / (n * 128 / 44117.647) if n else 0, sum(rms[i] for i in w) / n if n else 0))
PY
}
hz() { win "$@" | cut -d' ' -f1; }
lv() { win "$@" | cut -d' ' -f2; }
want() { awk -v n="$1" -v f="${2:-441}" 'BEGIN {printf "%.1f", f * 2 ^ (n / 12)}'; }
near() { awk -v a="$1" -v b="$2" -v t="${3:-0.03}" 'BEGIN {d = a - b; if (d < 0) d = -d; exit !(b > 0 && d <= b * t)}'; }
# row NAME K TIC SEMIS LABEL [TOL]: row K of a table with that tic (uSteps) sounds SEMIS above 441 Hz (TOL: relative, default 3 %; the rows of
# a test are 5 semitones or more apart, so the 8 % of the short tic 12 windows cannot take a wrong row)
row() {
    local blk; blk=$(awk -v t="$3" 'BEGIN {printf "%.3f", t * 0.8973}')
    local a b got w; a=$(awk -v k="$2" -v r="$blk" 'BEGIN {print k * r + 3}'); b=$(awk -v k="$2" -v r="$blk" 'BEGIN {print (k + 1) * r - 3}')
    got=$(hz "$1" "$a" "$b"); w=$(want "$4")
    [ -n "$EVID" ] && printf '%-24s row %2s  blocks %6s..%-6s want %7s Hz  got %7s Hz\n' "$1" "$2" "$a" "$b" "$w" "$got" >> "$EVID/table.txt"
    if near "$got" "$w" "${6:-0.03}"; then check "$5 (row $2: ${4} st, $w Hz)" ok; else check "$5 (row $2)" "want $w Hz, got $got Hz over blocks $a..$b"; fi
}
ratio() { awk -v a="$1" -v b="$2" 'BEGIN {printf "%.4f", (b > 0) ? a / b : 0}'; }
inr() { awk -v x="$1" -v lo="$2" -v hi="$3" 'BEGIN {exit !(x >= lo && x <= hi)}'; }
[ -n "$EVID" ] && : > "$EVID/table.txt"

S="sampletone 0; instr 0;"
# table N tic loop len, then its rows: table row N R NOTE VOL FXTYPE FXVAL (NOTE -128 / VOL 255 empty)
T1="table 1 24 1 4; table row 1 0 0 255 0 0; table row 1 1 12 255 0 0; table row 1 2 7 255 0 0; table row 1 3 -5 255 0 0;"

echo "### 1 N column, sample track, tic 24 (half a step), Loop, rows 0 +12 +7 -5"
play n24 "$S $T1 table bind 0 1; setstep 0 0 60 0 0 0 0 0;" "tap 0" 1000
for k in "0 0" "1 12" "2 7" "3 -5" "4 0" "5 12"; do set -- $k; row n24 "$1" 24 "$2" "sample N, Loop"; done
# the edge of row 1: the first block (after the onset) whose crossings say 882 Hz, expect 21.5 blocks after the onset (+-2)
edge=$(python3 -I - "$KEEP/n24.blk" <<'PY'
import sys
rows = [l.split() for l in open(sys.argv[1])]
rms = [float(r[3]) for r in rows]; zc = [int(r[5]) for r in rows]
on = next(i for i, v in enumerate(rms) if v > 500)
print(next((i - on for i in range(on + 3, len(zc)) if zc[i] >= 5), -1))
PY
)
check "row 1 starts 24 uSteps (21.5 blocks) after the note (+-2 blocks)" "$(inr "$edge" 19.5 23.5 && echo ok || echo "first 882 Hz block at onset+$edge")"

echo "### 2 tic 12: the same rows at a quarter step"
play n12 "$S ${T1//table 1 24 1 4/table 1 12 1 4} table bind 0 1; setstep 0 0 60 0 0 0 0 0;" "tap 0" 600
for k in "0 0" "1 12" "2 7" "3 -5" "4 0"; do set -- $k; row n12 "$1" 12 "$2" "sample N, tic 12" 0.08; done

echo "### 3 Once: rows 0 +12, then +12 holds"
play once "$S table 1 24 0 2; table row 1 0 0 255 0 0; table row 1 1 12 255 0 0; table bind 0 1; setstep 0 0 60 0 0 0 0 0;" "tap 0" 800
row once 1 24 12 "Once"
row once 2 24 12 "Once: the last row holds"
row once 3 24 12 "Once: the last row holds"

echo "### 4 V column and the row FX (tapamp: level after the track amplifier)"
play vol "$S table 1 24 1 4; table row 1 0 -128 255 0 0; table row 1 1 -128 50 0 0; table row 1 2 -128 255 0 0; table row 1 3 -128 255 18 20; table bind 0 1; setstep 0 0 60 0 0 0 0 0;" "tapamp 0" 1000
l0=$(lv vol 3 18.5); l1=$(lv vol 24.5 40); l2=$(lv vol 46 61.5); l3=$(lv vol 67.5 83); l4=$(lv vol 89 104.5)
[ -n "$EVID" ] && printf 'vol levels row0 %s row1 %s row2 %s row3 %s row4 %s\n' "$l0" "$l1" "$l2" "$l3" "$l4" >> "$EVID/table.txt"
r1=$(ratio "$l1" "$l0"); r2=$(ratio "$l2" "$l0"); r3=$(ratio "$l3" "$l0"); r4=$(ratio "$l4" "$l0")
check "V 50 is half the voice gain (linear)" "$(inr "$r1" 0.45 0.55 && echo ok || echo "row1/row0 $r1")"
check "an empty V is the note's volume again" "$(inr "$r2" 0.95 1.05 && echo ok || echo "row2/row0 $r2")"
check "row FX Volume 20 lowers the level" "$(inr "$r3" 0.005 0.2 && echo ok || echo "row3/row0 $r3")"
check "the row FX stays on through the next row (row 0 again, an empty FX cell keeps it)" "$(inr "$(ratio "$l4" "$l3")" 0.95 1.05 && echo ok || echo "row4/row0 $r4 row4/row3 $(ratio "$l4" "$l3")")"
# same note without any table: the level stays
play vol0 "$S setstep 0 0 60 0 0 0 0 0;" "tapamp 0" 600
check "control: without a table the level is the same over the rows" "$(inr "$(ratio "$(lv vol0 24.5 40)" "$(lv vol0 3 18.5)")" 0.98 1.02 && echo ok || echo "$(lv vol0 24.5 40) vs $(lv vol0 3 18.5)")"
check "control: row 0 of a table with no N, V or FX has the level of the plain note" "$(inr "$(ratio "$l0" "$(lv vol0 3 18.5)")" 0.98 1.02 && echo ok || echo "$l0 vs $(lv vol0 3 18.5)")"

echo "### 5 TBL step FX (id 53)"
play tblnote "$S $T1 setstep 0 0 60 0 53 1 0 0;" "tap 0" 600
row tblnote 1 24 12 "TBL 1 on a note step, no binding: the table runs"
row tblnote 2 24 7 "TBL 1 on a note step, no binding: the table runs"
play tbl0 "$S $T1 table bind 0 1; setstep 0 0 60 0 53 0 0 0;" "tap 0" 600
row tbl0 1 24 0 "TBL 0 on a note step overrides the binding: no table"
row tbl0 2 24 0 "TBL 0 on a note step overrides the binding: no table"
# TBL 1 on the empty step 2 (86.1 blocks after the note): rows from there
play tblempty "$S $T1 setstep 0 0 60 0 0 0 0 0; setstep 0 2 -1 0 53 1 0 0;" "tap 0" 1100
check "TBL on an empty step: before it the note is plain" "$(near "$(hz tblempty 3 80)" 441 && echo ok || echo "$(hz tblempty 3 80) Hz")"
check "TBL on an empty step: the sounding note gets row 1 (+12)" "$(near "$(hz tblempty 110.6 125.6)" 882 && echo ok || echo "$(hz tblempty 110.6 125.6) Hz")"
check "TBL on an empty step: then row 2 (+7)" "$(near "$(hz tblempty 132.1 147.1)" "$(want 7)" && echo ok || echo "$(hz tblempty 132.1 147.1) Hz")"
# a bound table of one row +12, TBL 0 on the empty step 2 stops it and puts the instrument's tune back
play tblstop "$S table 1 24 1 1; table row 1 0 12 255 0 0; table bind 0 1; setstep 0 0 60 0 0 0 0 0; setstep 0 2 -1 0 53 0 0 0;" "tap 0" 1000
check "TBL 0 on an empty step: before it the table sounds (+12)" "$(near "$(hz tblstop 3 80)" 882 && echo ok || echo "$(hz tblstop 3 80) Hz")"
check "TBL 0 on an empty step: after it the voice is back at its own tune" "$(near "$(hz tblstop 92 120)" 441 && echo ok || echo "$(hz tblstop 92 120) Hz")"

echo "### 6 each track keeps its own position"
play trk2 "$S $T1 table bind 0 1; setstep 0 0 60 0 0 0 0 0; setstep 1 1 60 0 0 0 0 0;" "tap 1" 800
row trk2 0 24 0 "track 2, note one step later: its own row 0"
row trk2 1 24 12 "track 2: its own row 1"
row trk2 2 24 7 "track 2: its own row 2"

echo "### 7 synth track (Poly part, instrument 64): N and V through the part"
TS="table 1 24 1 4; table row 1 0 0 255 0 0; table row 1 1 12 255 0 0; table row 1 2 7 50 0 0; table row 1 3 0 255 40 48;"
play syn "instr 64; $TS table bind 64 1; setstep 0 0 60 64 0 0 0 0;" mix 1000
play syn0 "instr 64; setstep 0 0 60 64 0 0 0 0;" mix 1000
f0=$(hz syn 3 18.5); f1=$(hz syn 24.5 40); f2=$(hz syn 46 61.5); f3=$(hz syn 67.5 83); c0=$(hz syn0 3 18.5); c3=$(hz syn0 67.5 83)
L0=$(lv syn 3 18.5); L2=$(lv syn 46 61.5); C2=$(lv syn0 46 61.5); C0=$(lv syn0 3 18.5)
[ -n "$EVID" ] && printf 'synth hz row0 %s row1 %s row2 %s row3 %s | plain %s %s | rms row0 %s row2 %s plain %s %s\n' "$f0" "$f1" "$f2" "$f3" "$c0" "$c3" "$L0" "$L2" "$C0" "$C2" >> "$EVID/table.txt"
check "synth row 0 (N 0) plays the plain note" "$(near "$f0" "$c0" 0.04 && echo ok || echo "$f0 vs $c0 Hz")"
check "synth row 1 (+12) doubles the pitch" "$(inr "$(ratio "$f1" "$f0")" 1.92 2.08 && echo ok || echo "ratio $(ratio "$f1" "$f0")")"
check "synth row 2 (+7) is a fifth up" "$(inr "$(ratio "$f2" "$f0")" 1.44 1.56 && echo ok || echo "ratio $(ratio "$f2" "$f0")")"
check "synth row 2 (V 50) is about half the level of the plain note there" "$(inr "$(ratio "$L2" "$C2")" 0.4 0.6 && echo ok || echo "ratio $(ratio "$L2" "$C2")")"
check "synth row 3: a Tune FX (not a synth FX) is ignored, the pitch is the plain note" "$(near "$f3" "$c3" 0.04 && echo ok || echo "$f3 vs $c3 Hz")"

echo "### 8 project.mtff: the tables load, play and are written back byte identical"
sd="$WORK/mt"; rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
"$BIN" "$sd" "wait 500" >/dev/null 2>&1
crc_of() { od -An -tx1 -j "$(( $(stat -f %z "$1") - 4 ))" -N4 "$1" | awk '{printf "%s%s%s%s", $4, $3, $2, $1}'; }
check "a new project writes no table line" "$([ -f "$sd/Workspace/project.mtff" ] && [ "$(grep -cE '^(tbl|instr)\.' "$sd/Workspace/project.mtff")" = 0 ] && echo ok || cat "$sd/Workspace/project.mtff")"
HDR="$(head -1 "$sd/Workspace/project.mtff")"
printf '%s\n' "$HDR" "mt $(crc_of "$sd/Workspace/project.mt") $(stat -f %z "$sd/Workspace/project.mt")" "strum.ramp 100" \
    "tbl.1 24 1 4 00ff00000cff000007ff0000fbff0000" "instr.1.table 1" >"$sd/Workspace/project.mtff"
cp "$sd/Workspace/project.mtff" "$WORK/mtff.before"
play mtffplay "$S setstep 0 0 60 0 0 0 0 0;" "tap 0" 1000 "$sd"
for k in "0 0" "1 12" "2 7" "3 -5"; do set -- $k; row mtffplay "$1" 24 "$2" "from project.mtff"; done
"$BIN" "$sd" "wait 500; dirty; wait 4000" >/dev/null 2>&1
check "an autosave writes the table lines back unchanged" "$([ "$(grep -cE '^(tbl|instr)\.' "$WORK/mtff.before")" = 2 ] && diff <(grep -E '^(tbl|instr)\.' "$WORK/mtff.before") <(grep -E '^(tbl|instr)\.' "$sd/Workspace/project.mtff") >/dev/null && echo ok || { echo; cat "$sd/Workspace/project.mtff"; })"

echo "### 10 an Off step: Loop runs on, Once ends on its own"
OFFST="setstep 0 0 60 0 0 0 0 0; setstep 0 1 -4 0 0 0 0 0;"
runst() { ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "wait 500; tempo 120; $2" 2>"$WORK/$1.err" | grep -E '^table(run|dump|bind)'; }
o=$(runst offloop "$S $T1 table bind 0 1; $OFFST audio 1; tap Play; wait 400; table run; wait 100; table run; tap Play; wait 100; table run")
l1=$(echo "$o" | sed -n 1p | awk '{print $4}'); l2=$(echo "$o" | sed -n 2p | awk '{print $4}'); l3=$(echo "$o" | sed -n 3p | awk '{print $3}')
check "Loop table: after the Off step (step 2) the runner still plays table 1" "$([ "${l1%%/*}" = 0:1 ] && [ "$(echo "$o" | sed -n 1p | awk '{print $3}')" = 0001 ] && echo ok || echo "$o")"
check "Loop table: its rows still advance after the Off step" "$([ "$l1" != "$l2" ] && echo ok || echo "$l1 then $l2")"
check "Stop still stops the table" "$([ "$l3" = 0000 ] && echo ok || echo "$o")"
o=$(runst offonce "$S table 1 24 0 2; table row 1 0 0 255 0 0; table row 1 1 12 255 0 0; table bind 0 1; $OFFST audio 1; tap Play; wait 400; table run")
check "Once table: holds its last row and stops advancing (mask clear, table kept)" "$(echo "$o" | grep -q 'mask 0000 0:1/1 ' && echo ok || echo "$o")"

echo "### 11 live notes start the bound table (sequencer stopped)"
o=$(runst livepad "$S $T1 table bind 0 1; audio 1; padon 0 50; table run; wait 70; table run; padoff 0 50")
check "pad note: the bound table starts on track 1" "$(echo "$o" | sed -n 1p | grep -q 'mask 0001 0:1/0 ' && echo ok || echo "$o")"
check "pad note: its rows advance with the sequencer stopped" "$(echo "$o" | sed -n 2p | grep -q 'mask 0001 0:1/1 ' && echo ok || echo "$o")"
o=$(runst livemidi "$S $T1 table bind 0 1; audio 1; midion 60 100 50; table run; midioff 60 0 50")
check "USB MIDI note: the bound table starts" "$(echo "$o" | sed -n 1p | grep -q 'mask 0001 0:1/' && echo ok || echo "$o")"
o=$(runst livenone "$S $T1 audio 1; padon 0 50; table run; padoff 0 50")
check "pad note of an instrument without a table: no runner" "$(echo "$o" | grep -q 'mask 0000 0:0/' && echo ok || echo "$o")"
# a release ends a live note's Loop table once the voice is silent (user 2026-11-21), also with the sequencer stopped. Held: it keeps running
o=$(runst liverel "$S $T1 table bind 0 1; audio 1; padon 0 50; table run; wait 400; table run; padoff 0 50; wait 100; table run")
check "pad held: the Loop table keeps advancing (no end before the release)" "$(echo "$o" | sed -n 1p | grep -q 'mask 0001 0:1/' && echo "$o" | sed -n 2p | grep -q 'mask 0001 0:1/' && echo ok || echo "$o")"
check "pad release (sample): the Loop table ends, the runner is empty again" "$(echo "$o" | sed -n 3p | grep -q 'mask 0000 0:0/' && echo ok || echo "$o")"
o=$(runst liverelsyn "instr 64; $T1 table bind 64 1; audio 1; padon 0 50; table run; padoff 0 50; wait 3000; table run")
check "pad release (synth part): the Loop table runs through the release, then ends" "$(echo "$o" | sed -n 1p | grep -q 'mask 0001 0:1/' && echo "$o" | sed -n 2p | grep -q 'mask 0000 0:0/' && echo ok || echo "$o")"
o=$(runst livereladd "$S $T1 table bind 0 1; audio 1; padon 0 50; padoff 0 50; padon 0 50; table run; wait 200; table run")
check "pad struck again after a release: the table starts anew and keeps running while held" "$(echo "$o" | sed -n 1p | grep -q 'mask 0001 0:1/' && echo "$o" | sed -n 2p | grep -q 'mask 0001 0:1/' && echo ok || echo "$o")"
o=$(runst livemidirel "$S $T1 table bind 0 1; audio 1; midion 60 100 50; table run; midioff 60 0 50; wait 100; table run")
check "USB MIDI note off: the Loop table ends like a pad release" "$(echo "$o" | sed -n 1p | grep -q 'mask 0001 0:1/' && echo "$o" | sed -n 2p | grep -q 'mask 0000 0:0/' && echo ok || echo "$o")"
# pitch of the pad note on the voice tap: row 1 (+12) is twice row 0
ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/livepitch" "wait 500; tempo 120; $S $T1 table bind 0 1; audio 1; audiostat tap 0; audiostat 1; padon 0 600; audiostat 0; audiostat tapdump" \
    2>"$WORK/livepitch.err" | grep -E '^blk ' >"$KEEP/livepitch.blk"
p0=$(hz livepitch 3 18.5); p1=$(hz livepitch 24.5 40)
[ -n "$EVID" ] && printf 'live pad hz row0 %s row1 %s\n' "$p0" "$p1" >> "$EVID/table.txt"
check "pad note: row 1 (+12) doubles the pitch of row 0 with the sequencer stopped" "$(inr "$(ratio "$p1" "$p0")" 1.94 2.06 && echo ok || echo "row0 $p0 row1 $p1")"

echo "### 13 stopped on an external clock with no pulses (tableIdleFallback)"
# midiclk 0 only selects the USB clock in: no pulse ever arrives, so only the fallback (last known tempo, after 0.5 s without a pulse) can advance
o=$(runst extidle "$S $T1 table bind 0 1; midiclk 0; audio 1; padon 0 50; table run; wait 300; table run; wait 2000; table run; padoff 0 50")
e1=$(echo "$o" | sed -n 1p | awk '{print $4}'); e2=$(echo "$o" | sed -n 2p | awk '{print $4}'); e3=$(echo "$o" | sed -n 3p | awk '{print $4}')
check "external clock, no pulses: the pad note starts the bound table" "$(echo "$o" | sed -n 1p | grep -q 'mask 0001 0:1/0 ' && echo ok || echo "$o")"
check "external clock, no pulses: the table holds row 0 for the first 0.5 s" "$([ "$e2" = "$e1" ] && echo ok || echo "$e1 then $e2")"
check "external clock, no pulses: the rows advance after 0.5 s without a pulse" "$([ "$e3" != "$e1" ] && echo ok || echo "$e1 then $e3 (held)")"
o=$(runst intidle "$S $T1 table bind 0 1; audio 1; padon 0 50; table run; wait 2300; table run; padoff 0 50")
check "internal clock: unchanged, the rows advance at once" "$([ "$(echo "$o" | sed -n 1p | awk '{print $4}')" != "$(echo "$o" | sed -n 2p | awk '{print $4}')" ] && echo ok || echo "$o")"

echo "### 12 the Table page"
GOLD="$HERE/goldens/tables"; SH="$WORK/shots"; mkdir -p "$SH"
golden() {
    [ -s "$SH/$1.png" ] || { check "golden $1" "no screenshot"; return; }
    [ -z "${TABLES_PNG_DIR:-}" ] || { mkdir -p "$TABLES_PNG_DIR"; cp "$SH/$1.png" "$TABLES_PNG_DIR/$1.png"; }
    if [ -n "${TABLES_UPDATE:-}" ]; then mkdir -p "$GOLD"; cp "$SH/$1.png" "$GOLD/$1.png"; check "golden $1 written" ok; return; fi
    check "golden $1: byte equal to goldens/tables/$1.png" "$(cmp -s "$SH/$1.png" "$GOLD/$1.png" && echo ok || echo "differs (TABLES_UPDATE=1 rewrites it)")"
}
PAGE="instr 0; tap Params; wait 400; tap Params; wait 400; tap Params; wait 400;"
sd="$WORK/page"; rm -rf "$sd"
o=$(runst page "$PAGE shot $SH/page-empty.png; enc 3; wait 100; shot $SH/page-bound.png; tap F2; enc 12; tap F3; enc -12; tap F4; enc -1; \
tap Down; enc 12; tap Down; enc -5; tap Right; enc -40; tap Down; tap Right; enc 2; tap Right; enc 20; tap Up; tap Up; tap Left; tap Left; wait 100; shot $SH/page-edit.png; \
table dump bind 0; table dump 3; \
tap Left; hold Shift; tap Down; unhold Shift; wait 100; shot $SH/page-select.png; tap F5; tap Down 8; tap F6; wait 100; shot $SH/page-paste.png; table dump 3; \
tap Down; hold Shift; tap Up; unhold Shift; tap F7; table dump 3; \
tap F1; tap F5; enc 1; tap F6; table dump 4; tap F7; table dump 4; wait 100; shot $SH/page-cleared.png; dirty; wait 4000")
echo "$o" > "$KEEP/page.out"
d1=$(echo "$o" | grep '^tabledump 3' | sed -n 1p); d2=$(echo "$o" | grep '^tabledump 3' | sed -n 2p); d3=$(echo "$o" | grep '^tabledump 3' | sed -n 3p)
check "F1 + encoder binds table 3 to instrument 1" "$(echo "$o" | grep -q '^tablebind 0 3$' && echo ok || echo "$o")"
# rows: 0 N+12, 1 N-5 V60, 2 FX Panning(2nd) 20, tic 24, Once, len 4
R0=0cff0000; R1=fb3c0000
check "encoder edits: Tic 24, Once, Len 4, row 1 +12, row 2 -5 at 60 %, row 3 an FX at 20" \
    "$(echo "$d1" | grep -qE "^tabledump 3 24 0 4 $R0 $R1 80ff[0-9a-f]{2}14 80ff0000" && echo ok || echo "$d1")"
check "copy rows 1-2 (Shift+Down), paste at row 10" "$(echo "$d2" | awk -v a=$R0 -v b=$R1 '{exit !($15 == a && $16 == b && $6 == a && $7 == b)}' && echo ok || echo "$d2")"
check "clear rows 10-11 (Shift+Up selection, F7), rows 1-2 stay" "$(echo "$d3" | awk -v a=$R0 -v b=$R1 '{exit !($15 == "80ff0000" && $16 == "80ff0000" && $6 == a && $7 == b)}' && echo ok || echo "$d3")"
d4=$(echo "$o" | grep '^tabledump 4' | sed -n 1p); d5=$(echo "$o" | grep '^tabledump 4' | sed -n 2p)
check "copy the whole table (header), bind table 4, paste: table 4 = table 3" "$([ "${d4#tabledump 4}" = "${d3#tabledump 3}" ] && echo ok || echo "$d4 vs $d3")"
check "clear the whole table (header): table 4 is empty again" "$(echo "$d5" | grep -q '^tabledump 4 12 1 16 80ff0000' && echo ok || echo "$d5")"
check "the page's edits reach project.mtff (tbl.3 and instr.1.table 4)" \
    "$(grep -q '^tbl\.3 24 0 4 ' "$WORK/page/Workspace/project.mtff" && grep -q '^instr\.1\.table 4$' "$WORK/page/Workspace/project.mtff" && echo ok || cat "$WORK/page/Workspace/project.mtff")"
runst syn "instr 64; tap Params; wait 400; tap Params; wait 400; tap Params; wait 400; tap Params; wait 400; shot $SH/page-synth.png; tap Params; wait 400; shot $SH/page-synth-back.png" >/dev/null
# live playhead: the mark moves with no key press (pads start the table, the page only waits)
o=$(runst live "$PAGE audio 1; enc 3; wait 100; padon 0 50; wait 80; shot $SH/live-a.png; wait 330; shot $SH/live-b.png; padoff 0 50; wait 100")
check "the Table page playhead moves with no key press (two screens apart differ)" "$([ -s "$SH/live-a.png" ] && [ -s "$SH/live-b.png" ] && ! cmp -s "$SH/live-a.png" "$SH/live-b.png" && echo ok || echo "no change (or no shot)")"
for n in page-empty page-bound page-edit page-select page-paste page-cleared page-synth; do golden "$n"; done
check "Params on the synth Table page goes back to Synth Parameters 1/4" "$([ -s "$SH/page-synth-back.png" ] && ! cmp -s "$SH/page-synth.png" "$SH/page-synth-back.png" && echo ok || echo "no change")"
# The page always opens with the cursor on the Table field (user decision 2026-11-21), whatever field the last visit left it on: after F2 the cursor
# stands on Tic, and the first encoder move after the return changes the bound table (Table 3 to 6) and leaves Tic alone
o=$(runst pagecur "$PAGE enc 3; wait 150; table dump bind 0; table dump 3; tap F2; wait 150; tap Params; wait 400; tap Params; wait 400; tap Params; wait 400; enc 3; wait 150; table dump bind 0; table dump 3; table dump 6")
check "Table page shown again: the cursor is back on the Table field (the first encoder move changes the bound table 3 to 6), Tic is not edited" \
    "$([ "$(echo "$o" | grep '^tablebind' | sed -n 2p)" = "tablebind 0 6" ] && echo "$o" | grep '^tabledump 3' | sed -n 2p | grep -q '^tabledump 3 12 1 16 ' && echo ok || echo "$o")"

echo "### 14 a project open binds the tables like the boot load (project folder with another name and path)"
# A project saved as /Projects/Tbl/Tp and found as /TblTest/P1 (copied, uploaded by the console): the open writes the new name and path into the
# Workspace project.mt, so its CRC is no longer the one project.mtff was stamped with. The open used to take that for a project.mt changed
# outside FF and parked instr.N.table (0 table calls on the unit after `open`, only a boot load of the same files bound them)
OP="Projects/Tbl"
sd="$WORK/opensrc"; rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth" "$sd/$OP"
"$BIN" "$sd" "wait 500; $T1 table bind 0 1; dirty; wait 3000; poke fmsave $OP Tp; wait 5000" >/dev/null 2>"$WORK/opensrc.err"
check "the saved project folder holds the table lines" "$(grep -q '^tbl\.1 24 1 4 ' "$sd/$OP/Tp/project.mtff" && grep -q '^instr\.1\.table 1$' "$sd/$OP/Tp/project.mtff" && echo ok || cat "$sd/$OP/Tp/project.mtff")"
sd="$WORK/opendst"; rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth" "$sd/TblTest"; cp -R "$WORK/opensrc/$OP/Tp" "$sd/TblTest/P1"
o=$("$BIN" "$sd" "wait 500; table dump bind 0; poke fmopen /TblTest P1; wait 8000; table dump 1; table dump bind 0" 2>"$WORK/opendst.err" | grep '^table')
check "open: before it the instrument has no table" "$(echo "$o" | sed -n 1p | grep -q '^tablebind 0 0$' && echo ok || echo "$o")"
check "open: the table rows of project.mtff load" "$(echo "$o" | grep -q '^tabledump 1 24 1 4 00ff0000 0cff0000 07ff0000 fbff0000' && echo ok || echo "$o")"
check "open: instrument 1 is bound to table 1 (a project of another name and path)" "$(echo "$o" | grep -q '^tablebind 0 1$' && echo ok || echo "$o")"
check "open: no 'parked' line in the Workspace project.mtff, the binding is kept" "$(! grep -q 'parked' "$sd/Workspace/project.mtff" && grep -q '^instr\.1\.table 1$' "$sd/Workspace/project.mtff" && echo ok || cat "$sd/Workspace/project.mtff")"
o=$("$BIN" "$sd" "wait 3000; table dump bind 0" 2>>"$WORK/opendst.err" | grep '^table')
check "start after the open: the Workspace project.mtff still binds instrument 1 (its stamp follows the Workspace project.mt)" "$(echo "$o" | grep -q '^tablebind 0 1$' && echo ok || echo "$o")"
# a project.mtff written for another project.mt still parks the binding (the stamp check is not switched off)
sd="$WORK/openstale"; rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth" "$sd/TblTest"; cp -R "$WORK/opensrc/$OP/Tp" "$sd/TblTest/P1"
sed -i '' 's/^mt [0-9a-f]* /mt 00000001 /' "$sd/TblTest/P1/project.mtff"   # a stamp of some other project.mt
o=$("$BIN" "$sd" "wait 500; poke fmopen /TblTest P1; wait 8000; table dump 1; table dump bind 0" 2>>"$WORK/openstale.err" | grep '^table')
check "open of a project.mtff with another stamp: the table rows load, the binding stays parked" "$(echo "$o" | grep -q '^tabledump 1 24 1 4 00ff' && echo "$o" | grep -q '^tablebind 0 0$' && echo ok || echo "$o")"

fresh() { rm -rf "$WORK/$1"; echo "$WORK/$1"; }
echo "### 15 the FX picker (a held Fx key shows every FX of a row at once)"
# The ids the encoder gave before the picker (the pattern editor's list order, the FX a row takes), in that order: Volume Panning Micro-tune
# Glide Rnd Volume Rev Playback Position Slice Vol/Pan/Filt/Pos/F.tune LFO Overdrive Low-pass Band-pass High-pass Delay Reverb Bit Depth Tune
# Slide up/down WT Vol Env WT Filt Env Gran Length CC A-F. The picker has the same FX in another order (four columns filled down)
OLDFX="12 1f 22 13 07 1a 16 19 17 18 23 24 21 26 1b 1d 1c 1e 20 27 28 29 2a 2b 2c 2d 08 09 0a 0b 0c 25"
FXROW="$PAGE enc 3; wait 100; tap Down; tap Right; tap Right;"      # table 3 bound, the cursor on row 1, the FX cell
rowtype() { echo "$1" | grep '^tabledump 3' | tail -1 | awk '{print substr($6, 5, 2)}'; }
# the encoder: 33 clicks from None, one FX each, no id twice, the same set as before, the last click stays on the last FX
cmd="wait 500; $FXROW"; for i in $(seq 1 34); do cmd="$cmd enc 1; table dump 3;"; done
sweep=$("$BIN" "$(fresh pksweep)" "$cmd" 2>"$WORK/pksweep.err" | grep '^tabledump 3' | awk '{printf "%s ", substr($6, 5, 2)}')
n=$(echo $sweep | wc -w | tr -d ' ')
check "encoder: 34 clicks from None, the last three stay on the last FX" \
    "$([ "$n" = 34 ] && [ "$(echo $sweep | cut -d' ' -f32)" = "$(echo $sweep | cut -d' ' -f34)" ] && echo ok || echo "$sweep")"
check "encoder: the picker order holds the same 32 FX as the row FX list (no id twice, none missing)" \
    "$([ "$(echo $sweep | tr ' ' '\n' | head -32 | sort | tr '\n' ' ')" = "$(echo $OLDFX | tr ' ' '\n' | sort | tr '\n' ' ')" ] && echo ok || echo "$sweep")"
cmd="wait 300;"; for i in $(seq 1 255); do cmd="$cmd table fxok $i;"; done
okset=$("$BIN" "$(fresh pkok)" "$cmd" 2>"$WORK/pkok.err" | awk '/^tablefxok/ && $3 == 1 {printf "%02x\n", $2}' | sort | tr '\n' ' ')
check "tables::rowFxAllowed takes exactly the FX of the picker (every id 1..255 asked)" \
    "$([ "$okset" = "$(echo $OLDFX | tr ' ' '\n' | sort | tr '\n' ' ')" ] && echo ok || echo "allowed: $okset")"
# steps from an empty FX cell to an FX: encoder clicks before and after, key presses with the picker (arrows, or hold + one pad)
[ -n "$EVID" ] && printf '%-12s %-3s %14s %14s %12s %10s\n' FX id "enc before" "enc after" "picker arrows" "picker pad" > "$EVID/steps.txt"
for c in "Volume|12|1|1|Down|1|1" "Panning|1f|2|2|Down 2|2|2" "Delay Send|1e|18|9|Right|1|9" "Reverb Send|20|19|10|Right;Down|2|10" "Low-pass|1b|15|11|Right;Down 2|3|11" "CC A|08|27|27|Right 3|3|27"; do
    IFS='|' read -r nm id before after arrows presses pad <<<"$c"
    keys=""; IFS=';' read -ra parts <<<"$arrows"; for q in "${parts[@]}"; do keys="$keys tap $q;"; done
    o=$("$BIN" "$(fresh pkarr)" "wait 500; $FXROW hold Fx; wait 60;$keys unhold Fx; wait 100; table dump 3" 2>"$WORK/pkarr.err" | grep '^table'); got=$(rowtype "$o")
    check "picker: $nm in $presses arrow press(es) after the held Fx key (the encoder took $before clicks)" "$([ "$got" = "$id" ] && echo ok || echo "got $got: $o")"
    o=$("$BIN" "$(fresh pkpad)" "wait 500; $FXROW hold Fx; wait 60; pad $pad; unhold Fx; wait 100; table dump 3" 2>"$WORK/pkpad.err" | grep '^table'); got=$(rowtype "$o")
    check "picker: $nm with one pad press (pad $pad) after the held Fx key" "$([ "$got" = "$id" ] && echo ok || echo "got $got: $o")"
    o=$("$BIN" "$(fresh pkenc)" "wait 500; $FXROW hold Fx; wait 60; enc $after; unhold Fx; wait 100; table dump 3" 2>"$WORK/pkenc.err" | grep '^table'); got=$(rowtype "$o")
    check "picker: $nm after $after encoder clicks in the picker" "$([ "$got" = "$id" ] && echo ok || echo "got $got: $o")"
    [ -n "$EVID" ] && printf '%-12s %-3s %14s %14s %12s %10s\n' "$nm" "$id" "$before" "$after" "$presses" 1 >> "$EVID/steps.txt"
done
# the other keys that open it, Delete, the release, a table-less instrument
o=$("$BIN" "$(fresh pkf8)" "wait 500; $FXROW hold F8; wait 60; tap Right; unhold F8; wait 100; table dump 3; hold Vol; wait 60; tap Down; unhold Vol; wait 100; table dump 3" 2>"$WORK/pkf8.err" | grep '^table')
check "F8 held opens the picker (Right: Delay Send), Vol held too (the Down from Delay Send: Reverb Send)" \
    "$([ "$(rowtype "$(echo "$o" | sed -n 1p)")" = 1e ] && [ "$(rowtype "$(echo "$o" | sed -n 2p)")" = 20 ] && echo ok || echo "$o")"
o=$("$BIN" "$(fresh pkdel)" "wait 500; $FXROW enc 2; hold Fx; wait 60; tap Right; tap Delete; unhold Fx; wait 100; table dump 3" 2>"$WORK/pkdel.err" | grep '^table')
check "Delete in the picker picks none: the row's Panning goes" "$([ "$(rowtype "$o")" = 00 ] && echo ok || echo "$o")"
o=$("$BIN" "$(fresh pkkeep)" "wait 500; $FXROW enc 2; enc 5; table dump 3; hold Fx; wait 60; unhold Fx; wait 100; table dump 3" 2>"$WORK/pkkeep.err" | grep '^table')
check "a tap on the Fx key changes nothing (the FX and its value stay)" "$([ "$(echo "$o" | sed -n 1p)" = "$(echo "$o" | sed -n 2p)" ] && echo ok || echo "$o")"
o=$("$BIN" "$(fresh pkoff)" "wait 500; $PAGE hold Fx; wait 60; tap Down; unhold Fx; wait 100; table dump bind 0; table dump 3" 2>"$WORK/pkoff.err" | grep '^table')
check "no table on the instrument: the held Fx key opens nothing" "$(echo "$o" | grep -q '^tablebind 0 0$' && echo "$o" | grep -q '^tabledump 3 12 1 16 80ff0000' && echo ok || echo "$o")"
# screens: the picker open on an empty FX cell, moved, set, and the cells with the long names
runst pkshot "$FXROW tap Down; tap Down; hold Fx; wait 100; shot $SH/page-pick.png; tap Right; tap Down; wait 100; shot $SH/page-pick-sel.png; unhold Fx; wait 100; shot $SH/page-pick-set.png" >/dev/null
runst pkname "$PAGE enc 3; wait 100; table row 3 0 -128 255 32 5; table row 3 1 -128 255 30 5; table row 3 2 -128 255 26 5; table row 3 3 -128 255 8 5; table row 3 4 -128 255 45 5; table row 3 5 -128 255 44 5; table row 3 6 -128 255 31 5; table row 3 7 -128 255 18 64; tap Params; wait 400; tap Params; wait 400; tap Params; wait 400; shot $SH/page-fxnames.png" >/dev/null
for n in page-pick page-pick-sel page-pick-set page-fxnames; do golden "$n"; done

echo "### 9 sanitizer"
errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
check "no sanitizer report in any run" "$([ "$errs" = 0 ] && echo ok || echo "$errs reports")"

echo
if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; exit 0; fi
echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1
