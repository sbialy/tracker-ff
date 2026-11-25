#!/bin/bash
# seed_sim.sh [PNG_DIR]
#
# Seeded random takes (core/source/seqRandom.h, Sequencer::resetChanceState, File > Project Settings > Random seed / Reroll seed) on the
# real UI and sequencer code of this tree (ui_shot) on the Mac. Host only: no device, every scenario has its own SD card folder.
# The sequencer plays in the simulator ("audio 1": the firmware's own audio callback runs on the virtual clock) and the notes it sends
# to USB MIDI out are logged ("midilog 1"). A take is the list of those notes as STEP:NOTE:VELOCITY:CHANNEL, the step counted from the
# Play press, so a take is the outcome of every Chance, Random Note, Random Volume and Random Instrument decision of two pattern passes.
# The pattern: 4 tracks x 16 steps, track 0 Chance 50, track 1 Random Note 12 + Chance 70, track 2 Random Volume + Chance 60,
# track 3 Random Instrument 2 (the MIDI channel) + Chance 80, on the MIDI instruments 48 to 50 (channels 1 to 3).
# PNG_DIR: also writes the page rows. SAN=address,undefined: the sanitizer build (build/ui-shot-san), a report fails the run except
# the stock mtWavetableEnvelopes.cpp:206 (also allowlisted by the sim gate).
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/seed_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
differs() { checks=$((checks+1)); if [ "$2" != "$3" ] && [ -n "$2" ] && [ -n "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s (the two takes must differ and not be empty)\n      a: %s\n      b: %s\n' "$1" "${2:0:150}" "${3:0:150}"; fi; }
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 1"; }

# the pattern: setstep T S NOTE INSTR FX0T FX0V FX1T FX1V (fx ids: 4 Chance, 5 Random Note, 6 Random Instrument, 7 Random Volume)
PATTERN=""
for i in $(seq 0 15); do
    PATTERN="$PATTERN setstep 0 $i $((48+i)) 48 4 50 0 0;"
    PATTERN="$PATTERN setstep 1 $i 60 49 5 12 4 70;"
    PATTERN="$PATTERN setstep 2 $i 52 50 7 100 4 60;"
    PATTERN="$PATTERN setstep 3 $i 55 48 6 2 4 80;"
done
PASS_MS=3900   # two passes of 16 steps at 130 BPM (115.4 ms a step) and a little more

# the notes sent to MIDI out as STEP:NOTE:VELOCITY:CHANNEL, the step counted from the "now" line printed before the Play press: one
# line per "now" (one take per Play). Notes of a take that plays nothing leave an empty line out (never the case with this pattern)
fold() { awk '/^now /{t0=$2; if (n) printf "\n"; n=1} /^midi on /{printf "%d:%d:%d:%d ", ($8-t0)/115.385+0.5, $3, $4, $6} END{printf "\n"}' ; }
BASH_RUN='ASAN_OPTIONS=detect_leaks=0:halt_on_error=0'
# play NAME "script": a process on the card folder NAME, the script runs after the pattern is written and the audio and the MIDI log are on
play() { local sd="$WORK/$1"; env $BASH_RUN "$BIN" "$sd" "wait 500; $PATTERN audio 1; midilog 1; $2" 2>"$WORK/$1.err" | fold; }
# one take of two passes: seed first, then Play and Stop
TAKE='now; tap Play; wait '$PASS_MS'; tap Play; wait 300'
# lines of a command into the array NAME (bash 3.2 has no mapfile)
into() { local n=$1 l; shift; eval "$n=()"; while IFS= read -r l; do eval "$n+=(\"\$l\")"; done < <("$@"); }

# 1. the same seed twice in one process: Play, Stop, Play (and a take of another seed in between)
into t play s1 "seed 7; $TAKE; $TAKE; seed 8; $TAKE; seed 7; $TAKE"
expect "four takes came out" "4" "${#t[@]}"
expect "seed 7: the take has notes" "yes" "$([ "$(wc -w <<<"${t[0]}")" -gt 30 ] && echo yes || echo no)"
expect "seed 7: Play, Stop, Play gives the same take (every step outcome)" "${t[0]}" "${t[1]}"
differs "seed 8 gives another take than seed 7" "${t[0]}" "${t[2]}"
expect "seed 7 again after seed 8: the first take again" "${t[0]}" "${t[3]}"
echo "      take of seed 7: $(wc -w <<<"${t[0]}") notes, seed 8: $(wc -w <<<"${t[2]}") notes"

# 2. two processes from cold start, the same seed: the same take. The clock does not matter (another start up delay)
a=$(play cold1 "seed 7; $TAKE")
b=$(play cold2 "wait 777; seed 7; $TAKE")
expect "seed 7 from two cold starts: the same take" "$a" "$b"
expect "the cold start take is the in process take" "${t[0]}" "$a"

# 3. the second pass differs from the first (the stream goes on across the pattern loop, a pass is not a copy of the first)
first=$(tr ' ' '\n' <<<"$a" | awk -F: 'NF==4 && $1 < 16 {printf "%s ", $2":"$3":"$4}')
second=$(tr ' ' '\n' <<<"$a" | awk -F: 'NF==4 && $1 >= 16 {printf "%s ", $2":"$3":"$4}')
differs "pass 2 of a take is not a copy of pass 1 (random decisions go on)" "$first" "$second"

# 4. Off: every take differs (the clock seeds it), within one process. 3 takes
into o play off "seed 0; $TAKE; $TAKE; $TAKE"
differs "Off: take 1 and take 2 differ" "${o[0]}" "${o[1]}"
differs "Off: take 2 and take 3 differ" "${o[1]}" "${o[2]}"
differs "Off: take 1 and take 3 differ" "${o[0]}" "${o[2]}"

# 5. every FX of the pattern uses the private generator: Random Note (notes of track 1 differ across the 12 semitone range), Random
#    Volume (velocities vary) and Random Instrument (channels 1 to 3 on track 3) all show in a seeded take, and Chance drops steps
notes=$(tr ' ' '\n' <<<"$a" | awk -F: 'NF==4 && $4==2 {print $2}' | sort -un | wc -l | tr -d ' ')
vels=$(tr ' ' '\n' <<<"$a" | awk -F: 'NF==4 && $4==3 {print $3}' | sort -un | wc -l | tr -d ' ')
chans=$(tr ' ' '\n' <<<"$a" | awk -F: 'NF==4 {print $4}' | sort -un | tr '\n' ' ')
total=$(wc -w <<<"$a" | tr -d ' ')
expect "Random Note: track 1 plays several different notes" "yes" "$([ "$notes" -ge 4 ] && echo yes || echo "no ($notes)")"
expect "Random Volume: track 2 velocities vary" "yes" "$([ "$vels" -ge 4 ] && echo yes || echo "no ($vels)")"
expect "Random Instrument: the take uses the channels 1 2 3 and more" "yes" "$([ "$(wc -w <<<"$chans")" -ge 3 ] && echo yes || echo "no ($chans)")"
expect "Chance: fewer than the 128 steps of two passes play, more than a quarter" "yes" "$([ "$total" -lt 128 ] && [ "$total" -gt 32 ] && echo yes || echo "no ($total)")"

# 6. Reroll: a new seed 1..255 every time, never the one before
out=$(ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/reroll" "wait 500; seed 0; $(for i in $(seq 1 60); do echo -n 'seed reroll; '; done) seed 255; seed reroll; seed reroll" 2>/dev/null | grep '^seed ')
ok=yes; prev=0; ix=0
while read -r _ v; do   # line 1 is the Off the script sets, line 62 the 255 it sets: not rerolls
    ix=$((ix+1))
    if [ "$ix" -eq 1 ] || [ "$ix" -eq 62 ]; then prev=$v; continue; fi
    { [ "$v" -ge 1 ] && [ "$v" -le 255 ] && [ "$v" != "$prev" ]; } || ok="no ($prev -> $v)"
    prev=$v
done <<<"$out"
expect "Reroll: 62 rerolls (also from Off and from 255) are all 1..255 and never the current seed" "yes" "$ok"
rolls=$(echo "$out" | sed -n '2,61p' | sort -u | wc -l | tr -d ' ')
expect "Reroll: 60 rerolls give more than 30 different seeds" "yes" "$([ "$rolls" -gt 30 ] && echo yes || echo "no ($rolls)")"
# a rerolled seed is a take of its own and replays the same
into r play reroll2 "seed reroll; $TAKE; $TAKE"
expect "a rerolled seed plays the same take on the next Play" "${r[0]}" "${r[1]}"
differs "a rerolled seed (not 7) gives another take than seed 7" "${r[0]}" "${t[0]}"

# 7. the seed is saved with the project: Workspace/project.mtff "seed.take N" only while a seed is set, and it comes back after a restart
sd="$WORK/persist"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; seed 42; dirty; wait 12000" >/dev/null 2>&1
expect "seed 42 and no plugin: the Workspace project.mtff holds the seed key, no plugins.txt" "seed.take 42 absent" "$(grep '^seed' "$sd/Workspace/project.mtff" 2>/dev/null) $([ -e "$sd/Workspace/plugins.txt" ] && echo present || echo absent)"
out=$(ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; seed" 2>/dev/null | grep '^seed ')
expect "a restart loads the project's seed" "seed 42" "$out"
c=$(play persist "$TAKE")
d=$(play cold42 "seed 42; $TAKE")
expect "the restarted project plays the take of seed 42" "$d" "$c"
out=$(ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; seed 0; dirty; wait 12000; seed" 2>/dev/null | grep '^seed ' | tail -1)
expect "seed Off is the project's seed after Off" "seed 0" "$out"
expect "Off removes the key again" "absent" "$(grep -q '^seed' "$sd/Workspace/project.mtff" 2>/dev/null && echo present || echo absent)"
sd="$WORK/never"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; dirty; wait 12000" >/dev/null 2>&1
expect "a project that never set a seed writes no seed key and no plugins.txt" "absent absent" "$(grep -q '^seed' "$sd/Workspace/project.mtff" 2>/dev/null && echo present || echo absent) $([ -e "$sd/Workspace/plugins.txt" ] && echo present || echo absent)"

# 7b. the seed follows the project through Save, New and Open (the file manager's own operations, plug::sidecarToProject / sidecarOpen, project.mtff)
sd="$WORK/projs"
out=$(env $BASH_RUN "$BIN" "$sd" "wait 500; seed 77; dirty; wait 500; saveproj SeedSong; seed 0; saveproj PlainSong; newproj; seed; openproj SeedSong; seed; openproj PlainSong; seed; newproj; seed" 2>/dev/null | grep -E '^(seed|saveproj|openproj|newproj) ')
expect "save, new, open: the seed is saved, New is Off, Open brings it back, a project without a seed opens Off" \
  "seed 77|saveproj ok status 0|seed 0|saveproj ok status 0|newproj ok status 0|seed 0|openproj ok status 0|seed 77|openproj ok status 0|seed 0|newproj ok status 0|seed 0" "$(tr '\n' '|' <<<"$out" | sed 's/|$//')"
expect "the saved project holds the seed key, the one saved with Off has none" "seed.take 77 absent" \
  "$(grep '^seed' "$sd/Projects/SeedSong/project.mtff") $(grep -q '^seed' "$sd/Projects/PlainSong/project.mtff" 2>/dev/null && echo present || echo absent)"

# 8. a project.mtff with a hand made damaged seed key next to a plugins.txt with a plugin: the plugin reference stays, the damaged seed is Off
sd="$WORK/mixed"; mkdir -p "$sd/Workspace"
printf 'tffplug 1\nformat 2\nmixer 1\nslot 1 92b586d4 00010000 Dirt\np Drive=62\n' > "$sd/Workspace/plugins.txt"; printf 'tffproj 1\nseed.take 300\n' > "$sd/Workspace/project.mtff"
out=$(ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; seed" 2>/dev/null | grep '^seed ')
expect "seed.take 300 (out of range) in a file reads as Off" "seed 0" "$out"

# 9. the Project Settings page (File, F7): Random seed (a list: Off, 1..255, Apply) and Reroll seed (Enter), as a user presses them. The
#    encoder moves the list 3 steps a call at most (fConfigEdit.cpp), so the long way back to Off takes 90 calls
sd="$WORK/ui"
out=$(ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; tap File; wait 800; tap F7; wait 800; tap Down 3; wait 300; $(shot seed-row); tap F8; wait 300; $(shot seed-list); enc 3; enc 2; wait 200; tap F8; wait 300; seed; $(shot seed-set); tap Down; wait 200; tap F8; wait 300; seed; $(shot seed-reroll); tap Up; tap F8; wait 300; $(for i in $(seq 1 90); do echo -n "enc -3; "; done) wait 200; tap F8; wait 300; seed" 2>"$WORK/ui.err" | grep '^seed ')
expect "page: Random seed list, 5 steps down from Off, Apply: seed 5" "seed 5" "$(sed -n 1p <<<"$out")"
r1=$(sed -n 2p <<<"$out" | cut -d' ' -f2)
expect "page: Reroll seed (Enter) gives another seed 1..255" "yes" "$([ -n "$r1" ] && [ "$r1" -ge 1 ] && [ "$r1" -le 255 ] && [ "$r1" != 5 ] && echo yes || echo "no ($r1)")"
expect "page: Random seed list, all the way up, Off, Apply: seed 0" "seed 0" "$(sed -n 3p <<<"$out")"
# the row marks the project changed: the sidecar follows without any other edit
sd="$WORK/ui2"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; tap File; wait 800; tap F7; wait 800; tap Down 3; tap F8; wait 300; enc 3; enc 3; enc 3; wait 200; tap F8; wait 12000" >/dev/null 2>&1
expect "page: a seed set on the page is written to project.mtff by the Workspace save" "seed.take 9" "$(grep '^seed' "$sd/Workspace/project.mtff" 2>/dev/null)"
sd="$WORK/ui3"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; tap File; wait 800; tap F7; wait 800; tap Down 4; tap F8; wait 12000" >/dev/null 2>&1
expect "page: Reroll seed on the page is written to project.mtff by the Workspace save" "yes" "$(grep -q '^seed.take [0-9]' "$sd/Workspace/project.mtff" 2>/dev/null && echo yes || echo no)"

# 10. the sanitizer build reports (SAN=...): nothing besides the stock one
if [ -n "$SAN" ]; then
    bad=$(cat "$WORK"/*.err 2>/dev/null | grep -E "runtime error|ERROR: AddressSanitizer" | grep -v "mtWavetableEnvelopes.cpp:206" | head -3)
    expect "sanitizer: no report in these scenarios (but the stock mtWavetableEnvelopes.cpp:206)" "" "$bad"
fi

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
