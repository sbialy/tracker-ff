#!/bin/bash
# follow_chord_sim.sh [PNG_DIR]
#
# Follow chord (core/source/mtFollowChord.h) on the real UI and sequencer code of this tree (ui_shot) on the Mac. Host only: no
# device. A chord track (MIDI instrument 48, channel 1) plays note steps with MIDI Chord FX, a follower track (MIDI instrument 49,
# channel 2) plays plain notes, the follower's notes are read from the USB MIDI log ("midilog", the step note + 24 goes out). The
# setting is made with the real keys: the pattern editor's third action page (F8 twice), F4 Follow mode, F5 Chord track
# (features-140: F3 of that page is the Groove of idea-groove, Follow chord moved from F3/F4 to F4/F5).
# PNG_DIR: also writes the screen of the third action page. SAN=address,undefined: the sanitizer build (a report fails the run, except
# the stock mtWavetableEnvelopes.cpp:206).
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/follow_chord_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the lines the checks read. The same NAME again is the same card (a restart of the unit)
    local sd="$WORK/$1"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; $2" 2>>"$WORK/$1.err" | grep -E '^(follow |midi on |proj|track\.|step )'
}
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 1"; }
ch2() { grep '^midi on' | grep ' ch 2 ' | awk '{print $3"@"$8}' | paste -sd' ' -; }   # the follower's notes: note@ms
PAGE3='tap F8; tap F8'
PLAY='audio 1; midilog 1; tap Play; wait 1900; tap Play; wait 100'
# chord track 1: C maj, A m, F (no Chord FX: keeps m, so F m), G maj on steps 1, 5, 9, 13
CHORDS1="setstep 0 0 48 48 14 5 0 0; setstep 0 4 57 48 14 4 0 0; setstep 0 8 53 48 0 0 0 0; setstep 0 12 55 48 14 5 0 0"
# the same chords on track 2 (the follower is track 1, before its chord track in the sequencer's loop)
CHORDS2="setstep 1 0 48 48 14 5 0 0; setstep 1 4 57 48 14 4 0 0; setstep 1 8 53 48 0 0 0 0; setstep 1 12 55 48 14 5 0 0"
fol() { echo "setstep $1 0 $2 49 0 0 0 0; setstep $1 4 $3 49 0 0 0 0; setstep $1 8 $4 49 0 0 0 0; setstep $1 12 $5 49 0 0 0 0"; }

# 1. no setting (no track.N.follow key in project.mtff, as every project before): the follower plays what is written (D, D#, A, C)
out=$(run none "$CHORDS1; $(fol 1 50 51 57 60); $PLAY")
expect "no setting: the notes as written" "74@0.0 75@461.3 81@922.6 84@1386.8" "$(ch2 <<<"$out")"
expect "no setting: no follow key in the Workspace project.mtff" "no" "$(grep -q '^track\..*\.follow ' "$WORK/none/Workspace/project.mtff" 2>/dev/null && echo yes || echo no)"

# 2. Snap via the keys (track 2, F4 once): D -> C (tie goes down), D# -> E, A -> Ab (F m, the chord type carried over), C -> B
out=$(run snap "$CHORDS1; $(fol 1 50 51 57 60); tap Right; $PAGE3; follow; tap F4; follow; $(shot follow-page3-snap); dirty; wait 4000; cat Workspace/project.mtff; $PLAY; step 1 0; step 1 4; step 1 8; step 1 12")
expect "snap: page 3 of track 2 shows Off and no chord track" "follow page 2 labels Off|-" "$(grep '^follow' <<<"$out" | sed -n 1p)"
expect "snap: F4 turns Snap on with track 1 as the chord track" "follow page 2 labels Snap|Track 1 t2<-t1:Snap" "$(grep '^follow' <<<"$out" | sed -n 2p)"
expect "snap: the Workspace sidecar" "track.2.follow 1 1" "$(grep -E '^track\.' <<<"$out" | paste -sd'|' -)"
expect "snap: C maj D->C, A m D#->E, F m A->Ab, G maj C->B" "72@0.0 76@461.3 80@922.6 83@1386.8" "$(ch2 <<<"$out")"
expect "snap: the pattern keeps the written notes" "50 51 57 60" "$(awk '/^step/{print $5}' <<<"$out" | paste -sd' ' -)"

# 3. Transp (F4 twice): C written, the root of each chord counted from C (A -3, F +5, G -5)
out=$(run transp "$CHORDS1; $(fol 1 48 48 48 48); tap Right; $PAGE3; tap F4; tap F4; follow; $PLAY")
expect "transp: label" "follow page 2 labels Transp|Track 1 t2<-t1:Transp" "$(grep '^follow' <<<"$out")"
expect "transp: C A F G roots" "72@0.0 69@461.3 77@922.6 67@1386.8" "$(ch2 <<<"$out")"

# 4. Tr+Snap (F4 three times): E written. C maj E, A m 49 -> C, F m 57 -> Ab, G maj 47 B stays
out=$(run trsnap "$CHORDS1; $(fol 1 52 52 52 52); tap Right; $PAGE3; tap F4; tap F4; tap F4; follow; $PLAY")
expect "tr+snap: label" "follow page 2 labels Tr+Snap|Track 1 t2<-t1:Tr+Snap" "$(grep '^follow' <<<"$out")"
expect "tr+snap: E -> E, C, Ab, B" "76@0.0 72@461.3 80@922.6 71@1386.8" "$(ch2 <<<"$out")"

# 5. The follower before its chord track (track 1 follows track 2, F4 on track 1 picks track 2): the chord of the same step counts
out=$(run order "$CHORDS2; $(fol 0 50 51 57 60); $PAGE3; tap F4; follow; $PLAY")
expect "order: track 1 picks track 2" "follow page 2 labels Snap|Track 2 t1<-t2:Snap" "$(grep '^follow' <<<"$out")"
expect "order: same notes as with the chord track first" "72@0.0 76@461.3 80@922.6 83@1386.8" "$(ch2 <<<"$out")"

# 6. Play with no chord step yet at the start: the last chord step of the track counts (wraps round to G maj on step 13)
out=$(run wrap "setstep 0 12 55 48 14 5 0 0; setstep 1 0 60 49 0 0 0 0; tap Right; $PAGE3; tap F4; $PLAY")
expect "wrap: step 1 hears the G maj of step 13 (C -> B)" "83@0.0" "$(ch2 <<<"$out")"

# 7. An inversion transposes by its root, not its bass: E with C maj inv1 (30) is C, so Transp keeps C
out=$(run inv "setstep 0 0 52 48 14 30 0 0; setstep 1 0 48 49 0 0 0 0; tap Right; $PAGE3; tap F4; tap F4; $PLAY")
expect "inversion: root C, no move" "72@0.0" "$(ch2 <<<"$out")"

# 8. F5 picks the chord track (skips the track itself, turns Snap on from Off)
out=$(run src "tap Right; $PAGE3; tap F5; follow; tap F5; follow; tap F5; follow")
expect "F5 from Off: track 3, Snap" "follow page 2 labels Snap|Track 3 t2<-t3:Snap" "$(grep '^follow' <<<"$out" | sed -n 1p)"
expect "F5 again: track 4" "follow page 2 labels Snap|Track 4 t2<-t4:Snap" "$(grep '^follow' <<<"$out" | sed -n 2p)"
expect "F5 again: track 5" "follow page 2 labels Snap|Track 5 t2<-t5:Snap" "$(grep '^follow' <<<"$out" | sed -n 3p)"

# 9. The setting is in the project: a restart reads the Workspace file, project save copies it, Off removes the Workspace file,
#    project open brings the project's back. The cursor moving to another track shows that track's setting
out=$(run proj "tap Right; $PAGE3; tap F4; tap F4; dirty; wait 4000")
out=$(run proj "follow; $PAGE3; follow; tap Right; follow; tap Left; follow")
expect "restart: the setting is back (labels before page 3 are empty)" "follow page 0 labels | t2<-t1:Transp" "$(grep '^follow' <<<"$out" | sed -n 1p)"
expect "restart: page 3 on track 1 shows Off" "follow page 2 labels Off|- t2<-t1:Transp" "$(grep '^follow' <<<"$out" | sed -n 2p)"
expect "restart: cursor on track 2 shows Transp" "follow page 2 labels Transp|Track 1 t2<-t1:Transp" "$(grep '^follow' <<<"$out" | sed -n 3p)"
expect "restart: cursor back on track 1 shows Off" "follow page 2 labels Off|- t2<-t1:Transp" "$(grep '^follow' <<<"$out" | sed -n 4p)"
out=$(run proj "projsave Projects FC1; cat Projects/FC1/project.mtff; tap Right; $PAGE3; tap F4; tap F4; follow; dirty; wait 4000")
expect "save: the project folder has the sidecar" "projsave Projects/FC1 started 1 idle 1|track.2.follow 1 2" "$(grep -E '^(proj|track\.)' <<<"$out" | sed 's/ after [0-9]* ms.*//' | paste -sd'|' -)"
expect "off: Transp -> Tr+Snap -> Off" "follow page 2 labels Off|-" "$(grep '^follow' <<<"$out")"
expect "off: the Workspace project.mtff has no follow key" "no" "$(grep -q '^track\..*\.follow ' "$WORK/proj/Workspace/project.mtff" 2>/dev/null && echo yes || echo no)"
out=$(run proj "projopen Projects FC1; follow")
expect "open: the project's setting is back" "follow page 0 labels | t2<-t1:Transp" "$(grep '^follow' <<<"$out")"
expect "open: the Workspace has the key again" "yes" "$(grep -q '^track\..*\.follow ' "$WORK/proj/Workspace/project.mtff" 2>/dev/null && echo yes || echo no)"
# a project without the sidecar opened over a Workspace that has one: no track follows after it
out=$(run proj "projsave Projects FC1; tap Right; $PAGE3; tap F4; tap F4; projsave Projects FC2; tap F4; follow; projopen Projects FC2; follow")
expect "open of a project without the file: nothing follows" "follow page 2 labels Snap|Track 1 t2<-t1:Snap|follow page 2 labels Off|-" "$(grep '^follow' <<<"$out" | paste -sd'|' -)"
expect "the project without the setting has no key" "no" "$(grep -q '^track\..*\.follow ' "$WORK/proj/Projects/FC2/project.mtff" 2>/dev/null && echo yes || echo no)"

# 10. A project.mtff that is not ours (no "tffproj 1" line) or holds bad lines: nothing or only the good lines follow
run bad "put Workspace/project.mtff track.2.follow 1 1\n" >/dev/null
out=$(run bad "follow")
expect "foreign file: nothing follows" "follow page 0 labels |" "$(grep '^follow' <<<"$out")"
run bad2 "put Workspace/project.mtff tffproj 1\ntrack.2.follow 2 2\ntrack.3.follow 1 9\ntrack.99.follow 1 1\ntrack.4.follow 1 3\n" >/dev/null
out=$(run bad2 "follow")
expect "bad lines: self source, unknown mode and track 99 dropped" "follow page 0 labels | t4<-t1:Tr+Snap" "$(grep '^follow' <<<"$out")"

new=$(cat "$WORK"/*.err 2>/dev/null | grep "SUMMARY" | grep -v "mtWavetableEnvelopes.cpp:206" | sort | uniq -c)
if [ -n "$new" ]; then fails=$((fails+1)); echo "FAIL  sanitizer report in a run:"; echo "$new"; fi
echo "$checks checks"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; else echo "RESULT: RED ($fails failing)"; exit 1; fi
