#!/usr/bin/env bash
# synth_nna_sim.sh [PNG_DIR]
#
# What a synth part does with its notes, on the real sequencer, the real shim (synth-open/open_synth.cpp) and the real engines of this tree
# (ui_shot, the host build, nothing here touches a device). Every scenario has its own SD card folder. The sequencer runs with "audio 1"
# (the firmware's Tracker::updateAudioBuffers, 128 frame blocks of 2,901 us on the virtual clock), a script plays a few steps and reads the
# voices of the synth part back with "poke voices" (host_poke.cpp): one entry per voice that is on, SLOT:GATE:NOTE:ID:FADE. The id is the
# per note id of the shim (0 = no id, the note came from the pad, MIDI in or record). Ids are printed by first appearance (a, b, c ...). A "~" after
# a voice: it is on the shim's New Note Action ramp (Cut or Fade), "F": stolen by the pool (fading).
# At 120 BPM a step is 125 ms: "tap Play" takes 80 ms, so the polls after it and every "wait 125" later sit in the middle of a step.
#
# Rows: S-ID1..S-ID6 are the note id phase (P1 of ref/synth-nna-design.md). The first commit of this script (P0) recorded what 814dc1b does
#       before any note id: its rows are the same scenarios with the P0 answers, "P0:" in a row text names the one that changed. F13 is "the
#       sequencer's Stop never reaches the shim's kStop" (the answer: it does not, in P0 and now)
#       S-C*, S-F*, S-P* are P2a (Cut, Fade and the project field), S-K* are P2b (Continue)
# Reference of the sound: the FNV-1a 64 of every block the shim renders for a part (poke renderhash) over a set of projects, goldens/synth_nna_hashes.txt
# (UPDATE=p0 / UPDATE=p1 rewrite the P0 / the P1 file, only for a change of the sound that is wanted). A project marked "class" holds two live notes of one pitch
# started by different sources, the one case the note id changes on purpose (D1 of the design).
# SAN=address,undefined: the sanitizer build. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
# the values the Up key walks on the New note block, in order: Release, Continue, Cut, Fade (the third Up stays on Fade, then four Downs)
NNA_UI_WANT="${NNA_UI_WANT:-0 1 2 3 0}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
GOLD="$HERE/goldens/synth_nna_hashes.txt"        # P0, 814dc1b, before the note ids
GOLD1="$HERE/goldens/synth_nna_hashes_p1.txt"    # P1: the projects of class 1 after the note ids
WORK="$(mktemp -d "${TMPDIR:-/tmp}/synth_nna_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }

# run NAME "script": the project starts empty, part 1 is instrument 64. Output files: $WORK/NAME.poke.out (raw), $WORK/NAME.v<P> (the voices lines of
# part P, ids normalised, one per "poke voices")
VOICES1='poke voicecount 0 1'
VOICES4='poke voicecount 0 4'
run() {
    local n="$1"
    rm -rf "$WORK/$n"; mkdir -p "$WORK/$n"
    UISHOT_POKE="$WORK/$n.poke" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$n" "wait 500; instr 64; $2" >"$WORK/$n.out" 2>"$WORK/$n.err"
    [ -f "$WORK/$n.poke.out" ] || : >"$WORK/$n.poke.out"
    # ids by first appearance: 0 stays 0, the others become a b c ...; the fade flag is shown as "F" only when set
    awk -v dir="$WORK" -v n="$n" '
      /^voices [0-9] / {
        p = $2; line = ""
        for (i = 4; i <= NF; i++) {
          if ($i == "[" || $i == "]" || $i ~ /^\[/ || $i ~ /^n=/) continue
          split($i, f, ":")
          if (length(f) < 5) continue
          id = f[4]
          if (id != 0 && !(id in seen)) { seen[id] = sprintf("%c", 96 + ++k) }
          line = line (line == "" ? "" : " ") f[1] ":" f[2] ":" f[3] ":" (id == 0 ? "0" : seen[id]) (f[5] == 1 ? "F" : "") (f[6] > 0 ? "~" : "")
        }
        cnt[p]++; print (line == "" ? "-" : line) > (dir "/" n ".v" p)
      }' "$WORK/$n.poke.out"
    for p in 0 1 2; do [ -f "$WORK/$n.v$p" ] || : >"$WORK/$n.v$p"; done
}
v() { sed -n "$3p" "$WORK/$1.v$2"; }             # v NAME PART N: the Nth "poke voices" of part PART
watch() { grep '^midiwatch' "$WORK/$1.poke.out" | sed -n "${2:-1}p"; }
P='poke voices'

# ============================================================================================================================================
# Rows. Ids are shown by first appearance (a, b, c), 0 is no id. "P0" marks what 814dc1b did (synth_nna_sim.sh of the commit before the note ids)
# ============================================================================================================================================

# S-ID1 (F8): two tracks play the same pitch. P0: the first track's Off released BOTH voices (the match was by pitch only), now only its own
# T1 step 0 C-5 (60), T2 step 2 C-5, T1 Off at step 4, T2 Off at step 8
run f8 "$VOICES4; setstep 0 0 60 64 0 0 0 0; setstep 1 2 60 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; setstep 1 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; wait 125; wait 125; $P; wait 125; wait 125; $P; wait 125; wait 125; wait 125; $P; tap Play"
expect "S-ID1 st0: one voice" "0:1:60:a" "$(v f8 0 1)"
expect "S-ID1 st2: the second track's note is a second voice, with its own id" "0:1:60:a 1:1:60:b" "$(v f8 0 2)"
expect "S-ID1 st4 (T1 Off): only the first note's voice is released (P0: both)" "0:0:60:a 1:1:60:b" "$(v f8 0 3)"
expect "S-ID1 st6: the second track's note keeps sounding" "0:0:60:a 1:1:60:b" "$(v f8 0 4)"
expect "S-ID1 st9 (T2 Off at 8): and its own Off ends it (the first voice has run out by then)" "1:0:60:b" "$(v f8 0 5)"

# control: the second track plays another pitch, the Off releases its own note only (same as P0)
run f8b "$VOICES4; setstep 0 0 60 64 0 0 0 0; setstep 1 2 64 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; audio 1; tap Play; wait 125; wait 125; $P; wait 125; wait 125; $P; tap Play"
expect "control, other pitch: both gated" "0:1:60:a 1:1:64:b" "$(v f8b 0 1)"
expect "control, other pitch: Off releases its own note only" "0:0:60:a 1:1:64:b" "$(v f8b 0 2)"

# S-ID2: mono part (Voices 1): the second track's note takes the held voice (legato) and its id, the first track's Off no longer matches it
run mono_other "$VOICES1; setstep 0 0 60 64 0 0 0 0; setstep 1 2 62 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; setstep 1 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; wait 125; wait 125; $P; wait 125; wait 125; wait 125; wait 125; $P; tap Play"
expect "mono st0: one voice" "0:1:60:a" "$(v mono_other 0 1)"
expect "mono st2: the voice is reused by the second track's note (new id)" "0:1:62:b" "$(v mono_other 0 2)"
expect "mono st4 (T1 Off for 60): the voice plays 62, the Off does not match (same as P0)" "0:1:62:b" "$(v mono_other 0 3)"
expect "mono st8 (T2 Off): released" "0:0:62:b" "$(v mono_other 0 4)"
run mono_same "$VOICES1; setstep 0 0 60 64 0 0 0 0; setstep 1 2 60 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; setstep 1 8 -4 0 0 0 0 0; audio 1; tap Play; wait 125; wait 125; $P; wait 125; wait 125; $P; wait 125; wait 125; wait 125; wait 125; $P; tap Play"
expect "S-ID2 mono, same pitch st2: one voice, reused (the second note's id)" "0:1:60:a" "$(v mono_same 0 1)"
expect "S-ID2 mono, same pitch st4: the first track's Off leaves the second track's note (P0: released it)" "0:1:60:a" "$(v mono_same 0 2)"
expect "S-ID2 mono, same pitch st8: the second track's own Off ends it" "0:0:60:a" "$(v mono_same 0 3)"

# S-ID3: strum. +63 (126 ms between the notes) is cut by the next step: only the first note started, the others never start
run strumcut "$VOICES4; setstep 0 0 60 64 14 24 47 63; setstep 0 1 62 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; audio 1; poke midiwatch on; tap Play; $P; wait 125; $P; wait 125; wait 125; $P; poke midiwatch; tap Play"
expect "strum cut st0: the first chord note only" "0:1:60:a" "$(v strumcut 0 1)"
expect "strum cut st1: the chord note is released, the next step's note sounds" "0:0:60:a 1:1:62:b" "$(v strumcut 0 2)"
expect "strum cut st3: the pending strummed notes never started" "0:0:60:a 1:1:62:b" "$(v strumcut 0 3)"
expect "strum cut: note events the shim saw by step 3 (2 on, the 4 chord pitches get an off)" "midiwatch on 2 off 4 stop 0" "$(watch strumcut 1 | sed 's/ all [0-9]*//')"
# +30 (60 ms): the notes that started carry the chord's id and are released with it, the one still waiting is cancelled
run strumcut2 "$VOICES4; setstep 0 0 60 64 14 24 47 30; setstep 0 1 62 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; audio 1; poke midiwatch on; tap Play; $P; wait 125; $P; wait 125; wait 125; $P; poke midiwatch; tap Play"
expect "strum +30 st0: the notes that started share the chord's id" "0:1:60:a 1:1:64:a" "$(v strumcut2 0 1)"
expect "strum +30 st1: they are released by it, the cancelled ones never start, the next note has its own id" "0:0:60:a 1:0:64:a 2:1:62:b" "$(v strumcut2 0 2)"
expect "strum +30: note events (3 on)" "midiwatch on 3 off 4 stop 0" "$(watch strumcut2 1 | sed 's/ all [0-9]*//')"
# +10 (20 ms): all four notes start before the next step, they carry one id and an Off ends all of them
run strumall "$VOICES4; setstep 0 0 60 64 14 24 47 10; setstep 0 2 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; tap Play"
expect "strum +10 st0: the chord is four voices of one id" "0:1:60:a 1:1:64:a 2:1:67:a 3:1:71:a" "$(v strumall 0 1)"
expect "strum +10 st2 (Off): all four released" "0:0:60:a 1:0:64:a 2:0:67:a 3:0:71:a" "$(v strumall 0 2)"

# Stop: the tracks' notes are released by their own note-offs (stopAllOpenNotes)
run stop4 "$VOICES4; setstep 0 0 60 64 0 0 0 0; setstep 1 0 64 64 0 0 0 0; setstep 2 0 67 64 0 0 0 0; audio 1; poke midiwatch on; tap Play; $P; tap Play; $P; wait 200; poke midiwatch"
expect "Stop: before, 3 held notes with 3 ids" "0:1:60:a 1:1:64:b 2:1:67:c" "$(v stop4 0 1)"
expect "Stop: every voice released" "0:0:60:a 1:0:64:b 2:0:67:c" "$(v stop4 0 2)"

# F13: the sequencer's Stop never reaches the shim's kStop handler (releaseAll, governor reset): the shim sees no Stop event at all, only the note-offs
run f13 "$VOICES4; setstep 0 0 60 64 0 0 0 0; setstep 1 0 64 64 0 0 0 0; audio 1; poke midiwatch on; tap Play; wait 300; tap Play; wait 300; poke midiwatch; tap Play; wait 300; tap Play; wait 300; poke midiwatch"
expect "F13: play + Stop: 2 note-ons, 2 note-offs, 0 kStop reached the shim" "midiwatch on 2 off 2 stop 0" "$(watch f13 1 | sed 's/ all [0-9]*//')"
expect "F13: a second play + Stop: still 0 kStop" "midiwatch on 4 off 4 stop 0" "$(watch f13 2 | sed 's/ all [0-9]*//')"

# mute: a muted track ends its open synth note (mtSequencer.cpp noteOpen branch), the other tracks go on
run mute "$VOICES4; setstep 0 0 60 64 0 0 0 0; setstep 1 0 64 64 0 0 0 0; audio 1; tap Play; $P; poke mute 0 1; wait 40; $P; wait 125; $P; poke mute 0 0; tap Play"
expect "mute: both tracks sound" "0:1:60:a 1:1:64:b" "$(v mute 0 1)"
expect "mute: the muted track's note is released, the other one holds" "0:0:60:a 1:1:64:b" "$(v mute 0 2)"
expect "mute: and it stays so" "0:0:60:a 1:1:64:b" "$(v mute 0 3)"

# S-ID5: the pad (a live note outside the tracks' step state, no id) held over a sequencer note of the same pitch (pad 0 = C-6, note 72).
# P0: the sequencer's Off released the pad's voice too, now it does not. The pad's own release has no id and matches by pitch, as before
run pad_off "$VOICES4; setstep 0 0 72 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; audio 1; tap Play; padon 0 100; $P; wait 125; wait 125; wait 125; $P; padoff 0 100; $P; tap Play"
expect "S-ID5 pad: sequencer note and pad note, same pitch, two voices (the pad's has no id)" "0:1:72:a 1:1:72:0" "$(v pad_off 0 1)"
expect "S-ID5 pad: the sequencer's Off leaves the pad's voice (P0: released it too)" "0:0:72:a 1:1:72:0" "$(v pad_off 0 2)"
expect "S-ID5 pad: the pad's own release ends it" "0:0:72:a 1:0:72:0" "$(v pad_off 0 3)"
run pad_up "$VOICES4; setstep 0 0 72 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; padon 0 100; $P; padoff 0 100; $P; tap Play"
expect "S-ID5 pad release: no id, by pitch, it releases the sequencer's voice of that pitch too (same as P0)" "0:0:72:a 1:0:72:0" "$(v pad_up 0 2)"
# MIDI in (midion 96 plays note 72 here) the same
run midi_off "$VOICES4; setstep 0 0 72 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; audio 1; tap Play; midion 96 100 100; $P; wait 125; wait 125; wait 125; $P; midioff 96; $P; tap Play"
expect "S-ID5 MIDI in: sequencer note and MIDI note of one pitch, two voices" "0:1:72:a 1:1:72:0" "$(v midi_off 0 1)"
expect "S-ID5 MIDI in: the sequencer's Off leaves the MIDI voice" "0:0:72:a 1:1:72:0" "$(v midi_off 0 2)"
expect "S-ID5 MIDI in: the MIDI note-off ends it" "0:0:72:a 1:0:72:0" "$(v midi_off 0 3)"

# no hung note: a pad pressed while the sequencer plays takes a track whose note is over (the track's stepSent is the pad's note now). Stop
# releases it only because the track's id is cleared where the live note takes the track (mtSequencer.cpp): with the sequencer's old id the
# Stop would send an off for the old note and the pad's voice (no id) would sound until the next restart
run pad_stop "$VOICES4; setstep 0 0 72 64 0 0 0 0; setstep 1 0 64 64 0 0 0 0; setstep 1 2 -4 0 0 0 0 0; audio 1; tap Play; wait 125; wait 125; wait 125; $P; padon 4 100; $P; tap Play; $P; padoff 4 100; $P"
expect "pad on a track after its sequencer note: sequencer note held, 64 released, pad note held" "0:1:72:a 1:0:64:b 2:1:76:0" "$(v pad_stop 0 2)"
expect "pad on a track: Stop releases the pad's note (no hung note)" "0:0:72:a 1:0:64:b 2:0:76:0" "$(v pad_stop 0 3)"
expect "pad on a track: and the pad's release after the Stop changes nothing" "0:0:72:a 1:0:64:b 2:0:76:0" "$(v pad_stop 0 4)"

# S-ID4: a note-on that waits for the next block (the admission queue, only with a cycle counter) keeps its id, and the off of another note
# of that pitch does not end it: the unit rows of host_smoke_test.cpp ("S-ID" lines), run here so one script has every row
if [ -z "${SKIP_SMOKE:-}" ]; then
    SMOKE_ARCH=arm64 sh "$HERE/../run_smoke.sh" "ACD FAT VAP WTFM PERC" >"$WORK/smoke.log" 2>&1
    grep '^  S-ID' "$WORK/smoke.log" | sed 's/^ *//' >"$WORK/smoke_sid.txt"
    expect "S-ID4 the unit rows of host_smoke_test.cpp ran (9 rows)" "9" "$(wc -l <"$WORK/smoke_sid.txt" | tr -d ' ')"
    expect "S-ID4 every unit row is OK (queued note-on and its off, id of the pending entry, mono, untagged)" "0" "$(grep -vc ' OK$' "$WORK/smoke_sid.txt")"
fi

# ============================================================================================================================================
# P2a: New Note Action Cut and Fade (poke nna P MODE: 2 Cut, 3 Fade). The mode of the REPLACED note's part applies, only when the new step is a
# synth step too, only on a track (Arp, Roll, Stutter, pad and MIDI in always Release), and only on a part with room for a second voice
# ============================================================================================================================================
left()  { grep '^voices 0 ' "$WORK/$1.poke.out" | sed -n "${2}p" | tr ' ' '\n' | grep "^$3:" | head -1 | cut -d: -f6; }   # the nnaLeft of slot $3 in poll $2 ("" when not on)
RELEASEPOLY="$VOICES4; poke nna 0 0"
# S-C1: a poly part, three tracks hold notes, track 1 replaces its own: the old voice of track 1 is gone one block after the replace and the new
# note takes the free record (no third voice is stolen). Release (control): the old voice rings on in its release
for m in 0 2; do
    run c1_$m "$VOICES4; poke nna 0 $m; setstep 0 0 60 64 0 0 0 0; setstep 1 0 67 64 0 0 0 0; setstep 2 0 71 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; tap Play"
done
expect "S-C1 st0: three held notes" "0:1:60:a 1:1:67:b 2:1:71:c" "$(v c1_2 0 1)"
expect "S-C1 Cut st2: the replaced voice is gone, the other two held notes stay, the new note is on" "1:1:67:b 2:1:71:c 3:1:64:d" "$(v c1_2 0 2)"
expect "S-C1 control Release st2: the replaced voice rings in its release" "0:0:60:a 1:1:67:b 2:1:71:c 3:1:64:d" "$(v c1_0 0 2)"
# S-C2: MIDI Chord (chord 1 = 3 notes), a chord replaces a chord on a part of 4 voices: every voice of the old chord is cut (Release: one rings on)
for m in 0 2; do
    run c2_$m "$VOICES4; poke nna 0 $m; setstep 0 0 60 64 14 1 0 0; setstep 0 2 62 64 14 1 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; tap Play"
done
expect "S-C2 chord st0: three voices of one id" "0:1:60:a 1:1:62:a 2:1:67:a" "$(v c2_2 0 1)"
expect "S-C2 Cut chord st2: no voice of the old chord is left, three of the new one" "0:1:64:b 1:1:69:b 3:1:62:b" "$(v c2_2 0 2)"
expect "S-C2 control Release: one voice of the old chord rings on (room for it)" "0:1:64:b 1:1:69:b 2:0:67:a 3:1:62:b" "$(v c2_0 0 2)"
# S-C3: strum +63 (126 ms) replaced by the next step: the first note started and is cut, the pending ones never start, the next note is on
run c3 "$VOICES4; poke nna 0 2; setstep 0 0 60 64 14 24 47 63; setstep 0 1 62 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; $P; wait 125; wait 125; $P; tap Play"
expect "S-C3 strum st0: the first chord note" "0:1:60:a" "$(v c3 0 1)"
expect "S-C3 Cut strum st1: the started note is cut, the next step's note sounds" "1:1:62:b" "$(v c3 0 2)"
expect "S-C3 Cut strum st3: the strummed notes that had not started never start" "1:1:62:b" "$(v c3 0 3)"
# S-C4: the mode is the replaced note's part: part 1 (Cut) replaced by a note of part 2 (Release) is cut, and the other way round it is released
run c4a "$VOICES4; poke voicecount 1 4; poke nna 0 2; poke nna 1 0; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 65 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; tap Play"
expect "S-C4 part 1 Cut, then a note of part 2: part 1's voice is cut" "-" "$(v c4a 0 2)"
expect "S-C4 part 2 has the new note" "0:1:64:b" "$(v c4a 1 2)"
run c4b "$VOICES4; poke voicecount 1 4; poke nna 0 0; poke nna 1 2; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 65 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; tap Play"
expect "S-C4 part 1 Release, part 2 Cut: part 1's voice is released, not cut (the replaced note's part decides)" "0:0:60:a" "$(v c4b 0 2)"
# S-C5: the new step is a sample step: no NNA (the note would have no end event), the synth note is released as before
run c5 "$VOICES4; poke nna 0 2; setstep 0 0 60 64 0 0 0 0; setstep 0 2 60 0 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; tap Play"
expect "S-C5 Cut, then a sample step: released, no ramp" "0:0:60:a" "$(v c5 0 2)"
# S-C6: Roll retriggers inside a step always release: no voice is ever on a ramp
run c6 "$VOICES4; poke nna 0 2; setstep 0 0 60 64 3 4 0 0; setstep 0 4 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 30; $P; wait 30; $P; wait 30; $P; wait 30; $P; wait 30; $P; tap Play"
expect "S-C6 Cut part, Roll retriggers: no voice on a ramp in 6 polls" "0" "$(cat $WORK/c6.v0 | grep -c '~')"
expect "S-C6 Cut part, Roll retriggers: released voices do ring (the rolls ended with a plain release)" "yes" "$(grep -q ':0:60:' $WORK/c6.v0 && echo yes || echo no)"
# S-C7: pad notes (no id, no track) are always released
run c7 "$VOICES4; poke nna 0 2; audio 1; padon 0 100; $P; padoff 0 100; $P"
expect "S-C7 pad on a Cut part: released, no ramp" "0:0:72:0" "$(v c7 0 2)"
# S-C8: a mono part (Voices 1) has no record to ramp out on: Cut and Fade act as Release (legato reuse, as before). PERC too (one voice)
for m in 0 2 3; do
    run c8_$m "poke voicecount 0 1; poke nna 0 $m; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; wait 125; wait 125; wait 125; wait 125; $P; tap Play"
    run c9_$m "poke synthlib engine 0 4; poke nna 0 $m; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; tap Play"
done
expect "S-C8 mono, Cut: same as Release" "$(cat $WORK/c8_0.v0 | tr '\n' ' ')" "$(cat $WORK/c8_2.v0 | tr '\n' ' ')"
expect "S-C8 mono, Fade: same as Release" "$(cat $WORK/c8_0.v0 | tr '\n' ' ')" "$(cat $WORK/c8_3.v0 | tr '\n' ' ')"
expect "S-C8 mono Cut: the voice is reused (no ramp)" "0:1:64:b" "$(v c8_2 0 2)"
expect "S-C9 PERC, Cut: same as Release" "$(cat $WORK/c9_0.v0 | tr '\n' ' ')" "$(cat $WORK/c9_2.v0 | tr '\n' ' ')"
expect "S-C9 PERC, Fade: same as Release" "$(cat $WORK/c9_0.v0 | tr '\n' ' ')" "$(cat $WORK/c9_3.v0 | tr '\n' ' ')"
# S-F1: Fade: the replaced voice is released and ramps its gain down over 41 blocks (120 ms): one block of the ramp per block, 29 ms apart = 10 blocks
run f1 "$VOICES4; poke nna 0 3; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 100; $P; wait 29; $P; wait 29; $P; tap Play"
expect "S-F1 st2: the old voice is released and on the ramp, the new note on" "0:0:60:a~ 1:1:64:b" "$(v f1 0 2)"
fa=$(left f1 2 0); fb=$(left f1 3 0)
expect "S-F1 the ramp is 1..41 blocks left after the replace" "yes" "$([ -n "$fa" ] && [ "$fa" -ge 1 ] && [ "$fa" -le 41 ] && echo yes || echo no)"
expect "S-F1 29 ms (10 blocks) later the ramp is 10 blocks shorter (9..11)" "yes" "$([ -n "$fb" ] && [ $((fa - fb)) -ge 9 ] && [ $((fa - fb)) -le 11 ] && echo yes || echo no)"
expect "S-F1 the old voice is off once the ramp is over (the new note only)" "1:1:64:b" "$(v f1 0 4)"
# S-F2: a Fade note's voice counts for the pool while it rings: with Voices 2 the replaced voice (fading out) keeps its record for the new note
run f2 "poke voicecount 0 2; poke nna 0 3; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; tap Play"
expect "S-F2 Voices 2: old voice on the ramp and the new note, two records" "0:0:60:a~ 1:1:64:b" "$(v f2 0 2)"

# ============================================================================================================================================
# P2b: New Note Action Continue (poke nna 0 1). The replaced note is not ended, it is parked and released with the track's newest note: its Off
# step, its gate end, a mute, Stop (poke nnapark: how many notes the sequencer holds parked)
# ============================================================================================================================================
PK='poke nnapark'
pk() { grep '^nnapark' "$WORK/$1.poke.out" | sed -n "${2}p" | cut -d' ' -f2; }
# S-K1: a poly part, one track, three notes: every replaced note stays gated, the Off step releases all three
K1S="setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 4 67 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0"
for m in 0 1; do
    run k1_$m "$VOICES4; poke nna 0 $m; $K1S; audio 1; tap Play; $P; wait 125; wait 125; $P; $PK; wait 125; wait 125; $P; $PK; wait 125; wait 125; wait 125; wait 125; $P; $PK; tap Play"
done
expect "S-K1 Continue st2: the replaced note stays gated, two voices" "0:1:60:a 1:1:64:b" "$(v k1_1 0 2)"
expect "S-K1 Continue st4: three gated voices, two notes parked" "0:1:60:a 1:1:64:b 2:1:67:c / 2" "$(v k1_1 0 3) / $(pk k1_1 2)"
expect "S-K1 Continue: the Off step releases all three, nothing is parked" "0:0:60:a 1:0:64:b 2:0:67:c / 0" "$(v k1_1 0 4) / $(pk k1_1 3)"
expect "S-K1 control Release st2: the replaced note is released" "0:0:60:a 1:1:64:b" "$(v k1_0 0 2)"
expect "S-K1 control Release: nothing is ever parked" "0 0 0" "$(pk k1_0 1) $(pk k1_0 2) $(pk k1_0 3)"
# S-K2: the other ends of the newest note. Gate end (FX 20 = Gate), mute, Stop. Each leaves nothing parked
run k2g "$VOICES4; poke nna 0 1; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 20 20 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; $PK; wait 125; wait 125; $P; $PK; tap Play"
expect "S-K2 Gate end: the new note's gate ends and releases the parked note too" "0:1:60:a / 0:0:60:a 1:0:64:b / 0" "$(v k2g 0 1) / $(v k2g 0 3) / $(pk k2g 2)"
run k2m "$VOICES4; poke nna 0 1; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; audio 1; tap Play; wait 125; wait 125; $P; $PK; poke mute 0 1; wait 40; $P; $PK; poke mute 0 0; tap Play"
expect "S-K2 Mute: both notes held before" "0:1:60:a 1:1:64:b / 1" "$(v k2m 0 1) / $(pk k2m 1)"
expect "S-K2 Mute: the muted track releases the open note and the parked one" "0:0:60:a 1:0:64:b / 0" "$(v k2m 0 2) / $(pk k2m 2)"
run k2s "$VOICES4; poke nna 0 1; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; audio 1; poke midiwatch on; tap Play; wait 125; wait 125; $P; $PK; tap Play; $P; $PK; wait 200; poke midiwatch"
expect "S-K2 Stop: two notes held, one parked" "0:1:60:a 1:1:64:b / 1" "$(v k2s 0 1) / $(pk k2s 1)"
expect "S-K2 Stop: both released, nothing parked, one note-off each" "0:0:60:a 1:0:64:b / 0 / midiwatch on 2 off 2 stop 0" "$(v k2s 0 2) / $(pk k2s 2) / $(watch k2s 1 | sed 's/ all [0-9]*//')"
# S-K10: interrupt safety of the park list. The audio callback preempts the main loop (Stop from the UI) inside a pop: "poke nnapreempt on" runs the tick
# net of every track at each point inside the pop where the callback could run (copy done, shift done, count not yet lowered), which the interrupt mask
# of the guarded pop forbids. Without the mask (proven by taking it out) the net empties the list under the pop, the shift length wraps and ui_shot dies
run k10 "$VOICES4; poke nna 0 1; $K1S; audio 1; poke midiwatch on; tap Play; wait 125; wait 125; wait 125; wait 125; $P; $PK; poke nnapreempt on; tap Play; $P; $PK; wait 200; poke midiwatch"
expect "S-K10 Stop under preemption: three gated, two parked before" "0:1:60:a 1:1:64:b 2:1:67:c / 2" "$(v k10 0 1) / $(pk k10 1)"
expect "S-K10 Stop under preemption: all released, nothing parked, one note-off per note" "0:0:60:a 1:0:64:b 2:0:67:c / 0 / midiwatch on 3 off 3 stop 0" "$(v k10 0 2) / $(pk k10 2) / $(watch k10 1 | sed 's/ all [0-9]*//')"
# S-K3: seven notes replace each other on a pool of 6 parked entries: the oldest is released by the seventh, at once (the note-off count shows it)
K3S="setstep 0 0 60 64 0 0 0 0; setstep 0 1 62 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 3 65 64 0 0 0 0; setstep 0 4 67 64 0 0 0 0; setstep 0 5 69 64 0 0 0 0; setstep 0 6 71 64 0 0 0 0; setstep 0 7 72 64 0 0 0 0; setstep 0 14 -4 0 0 0 0 0"
run k3 "poke voicecount 0 8; poke nna 0 1; $K3S; audio 1; poke midiwatch on; tap Play; wait 125; wait 125; wait 125; wait 125; wait 125; $PK; poke midiwatch; wait 125; $PK; poke midiwatch; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; $PK; poke midiwatch; tap Play"
expect "S-K3 st6: seven notes on, six parked, no note-off yet" "6 / midiwatch on 7 off 0 stop 0" "$(pk k3 1) / $(watch k3 1 | sed 's/ all [0-9]*//')"
expect "S-K3 st7: the pool is full, the oldest is released at once (one note-off), still six parked" "6 / midiwatch on 8 off 1 stop 0" "$(pk k3 2) / $(watch k3 2 | sed 's/ all [0-9]*//')"
expect "S-K3 Off step: the other seven go, eight note-offs in all, nothing parked" "0 / midiwatch on 8 off 8 stop 0" "$(pk k3 3) / $(watch k3 3 | sed 's/ all [0-9]*//')"
# S-K4: chords. Poly: a MIDI Chord (chord 1 = three notes) replaces a chord, both release together on the Off step. Para (ACD, Chord Mode 1): one note-on
# per chord, one note-off per chord, the parked chord's own flag decides (the new chord's strum or flag is not touched)
K4S="setstep 0 0 60 64 14 1 0 0; setstep 0 2 62 64 14 1 0 0; setstep 0 8 -4 0 0 0 0 0"
K4W="wait 125; wait 125; $P; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; $P; $PK; poke midiwatch; tap Play"
run k4c "$VOICES4; poke nna 0 1; $K4S; audio 1; poke midiwatch on; tap Play; $P; $K4W"
expect "S-K4 poly chord, Continue: the new chord and the held pitch of the old one" "0:1:64:b 1:1:69:b 2:1:67:a 3:1:62:b" "$(v k4c 0 2)"
expect "S-K4 poly chord, Continue: the Off step releases both chords (6 note-ons, 6 note-offs), nothing parked" "0:0:64:b 1:0:69:b 2:0:67:a 3:0:62:b / 0 / midiwatch on 6 off 6 stop 0" "$(v k4c 0 3) / $(pk k4c 1) / $(watch k4c 1 | sed 's/ all [0-9]*//')"
PARA="tap Params; wait 1500; tap Up 3; wait 1800; tap Pattern; wait 300; poke synthparam 0 Sustain 1; poke synthparam 0 Chord_Mode 1; $VOICES4"
for m in 0 1; do
    run k4p_$m "$PARA; poke nna 0 $m; $K4S; audio 1; poke midiwatch on; tap Play; $P; $K4W"
done
expect "S-K4 Para chord st0: one voice for the chord" "0:1:60:a" "$(v k4p_1 0 1)"
expect "S-K4 Para chord, Continue st2: the old chord's voice stays gated next to the new chord's" "0:1:60:a 1:1:62:b" "$(v k4p_1 0 2)"
expect "S-K4 Para chord, Continue: released together by the Off step, 2 note-ons and 2 note-offs (the para flag of the parked chord)" "0:0:60:a 1:0:62:b / 0 / midiwatch on 2 off 2 stop 0" "$(v k4p_1 0 3) / $(pk k4p_1 1) / $(watch k4p_1 1 | sed 's/ all [0-9]*//')"
expect "S-K4 Para chord control Release st2: the old chord is released at the replace" "0:0:60:a 1:1:62:b" "$(v k4p_0 0 2)"
# S-K5: a mono part (Voices 1): Continue is legato. The replace sends no note-off (the shim sees on 2 off 0), the voice is reused with the new note's id
run k5 "poke voicecount 0 1; poke nna 0 1; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; poke midiwatch on; tap Play; $P; wait 125; wait 125; poke midiwatch; $P; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; $P; $PK; poke midiwatch; tap Play"
expect "S-K5 mono, Continue: the replace has two note-ons and no note-off at the shim" "midiwatch on 2 off 0 stop 0" "$(watch k5 1 | sed 's/ all [0-9]*//')"
expect "S-K5 mono, Continue: one voice, reused by the new note (its id)" "0:1:60:a / 0:1:64:b" "$(v k5 0 1) / $(v k5 0 2)"
expect "S-K5 mono, Continue: the Off step ends it (parked note-off does not match the new id), nothing parked" "0:0:64:b / 0" "$(v k5 0 3) / $(pk k5 1)"
# S-K6: Continue, then a sample step: the new step is not a synth note, the old one is released (no NNA)
run k6 "$VOICES4; poke nna 0 1; setstep 0 0 60 64 0 0 0 0; setstep 0 2 60 0 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; poke midiwatch on; tap Play; $P; wait 125; wait 125; $P; $PK; poke midiwatch; tap Play"
expect "S-K6 Continue, then a sample step: released at the replace, one note-off, nothing parked" "0:0:60:a / 0 / midiwatch on 1 off 1 stop 0" "$(v k6 0 2) / $(pk k6 1) / $(watch k6 1 | sed 's/ all [0-9]*//')"
# S-K9: PERC (one voice): Continue changes nothing, same voices as Release
for m in 0 1; do
    run k9_$m "poke synthlib engine 0 4; poke nna 0 $m; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; $P; $PK; tap Play"
done
expect "S-K9 PERC, Continue: the same voices as Release at every poll" "$(cat $WORK/k9_0.v0 | tr '\n' ' ')" "$(cat $WORK/k9_1.v0 | tr '\n' ' ')"
expect "S-K9 PERC, Continue: the voice is reused (legato)" "0:1:64:b" "$(v k9_1 0 2)"
# S-K8: voice pressure. A Continue pad (part 1, track 1, four notes that hold) next to a bass on part 2 (track 2, one voice). The part has room for three
# voices: the fourth pad note takes the pool's oldest voice, the parked note 60. Part 2 keeps its note. The parked entry of the stolen note stays until
# the Off step (its note-off finds no voice and does nothing), nothing hangs and nothing is left parked
K8S="setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 4 67 64 0 0 0 0; setstep 0 6 71 64 0 0 0 0; setstep 1 1 36 65 0 0 0 0; setstep 1 3 38 65 0 0 0 0; setstep 1 5 40 65 0 0 0 0; setstep 1 7 41 65 0 0 0 0; setstep 0 12 -4 0 0 0 0 0; setstep 1 12 -4 0 0 0 0 0"
run k8 "poke voicecount 0 4; poke voicecount 1 4; poke nna 0 1; $K8S; audio 1; tap Play; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; $P; $PK; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; $P; $PK; tap Play"
expect "S-K8 pressure: the pad's oldest (parked) note is stolen by the fourth, the others hold, the bass part keeps its note" "0:1:71:a 1:1:64:b 2:1:67:c / 1:1:41:d / 3" "$(v k8 0 1) / $(v k8 1 1) / $(pk k8 1)"
expect "S-K8 pressure: the Off steps release everything, nothing parked" "0:0:71:a 1:0:64:b 2:0:67:c / 1:0:41:d / 0" "$(v k8 0 2) / $(v k8 1 2) / $(pk k8 2)"
# S-K7: the hunt for a track end that leaves a note parked. Stop alone would hide it (it releases every note), so every row reads the park list too.
# a pattern switch ends nothing (the open note of a track goes on, in Release too): the parked notes go with the open one, at the Off step of the new
# pattern or at Stop. A pad on the track and a pad on another one: the parked note is still released at Stop. A short pattern that loops (4 steps
# of a 2 note pattern): every wrap parks again, Stop leaves nothing
run k7a "$VOICES4; poke nna 0 1; $K1S; audio 1; tap Play; wait 125; wait 125; wait 125; wait 125; $PK; pattern 2; wait 125; $PK; $P; wait 125; $PK; tap Play; $PK; $P"
expect "S-K7 pattern switch: nothing ends, two notes stay parked (the open note stays too, as in Release)" "2 2 2" "$(pk k7a 1) $(pk k7a 2) $(pk k7a 3)"
expect "S-K7 pattern switch: Stop releases everything and the park list is empty" "0 / 0:0:60:a 1:0:64:b 2:0:67:c" "$(pk k7a 4) / $(v k7a 0 2)"
run k7b "$VOICES4; poke nna 0 1; $K1S; audio 1; tap Play; wait 125; wait 125; wait 125; wait 125; $PK; pattern 2; setstep 0 6 -4 0 0 0 0 0; wait 125; wait 125; wait 125; $PK; $P; tap Play; $PK"
expect "S-K7 pattern switch to a pattern with an Off step: the Off ends the parked notes too" "2 / 0 / 0:0:60:a 1:0:64:b 2:0:67:c" "$(pk k7b 1) / $(pk k7b 2) / $(v k7b 0 1)"
run k7c "$VOICES4; poke nna 0 1; plen 4; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; audio 1; tap Play; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; wait 125; $PK; tap Play; $PK; $P"
expect "S-K7 a 4 step pattern that loops: the wraps park again, Stop leaves nothing parked and no voice gated" "4 / 0 / 0:0:60:a 1:0:64:b 2:0:60:c 3:0:64:d" "$(pk k7c 1) / $(pk k7c 2) / $(v k7c 0 1)"
run k7d "$VOICES4; poke nna 0 1; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; audio 1; tap Play; wait 125; wait 125; wait 125; $PK; padon 0 100; $PK; wait 125; padoff 0 100; wait 80; $PK; $P; tap Play; $PK; $P"
expect "S-K7 a pad on the track while a note is parked: the parked note stays until Stop (as the open note does), Stop releases all" "1 1 1 / 0 / 0:0:60:a 1:0:64:b 2:0:72:0" "$(pk k7d 1) $(pk k7d 2) $(pk k7d 3) / $(pk k7d 4) / $(v k7d 0 2)"

# S-P: the project field. Release is stored as the padding of an old project (0 0), a mode as nna and nna ^ 0xA5
run p1 "poke structsizes; poke nna 0; poke nna 0 3; poke nna 1 2; poke nna 2 1; saveproj nnaP1; poke nna 0 0; poke nna 1 0; poke nna 2 0; openproj nnaP1; poke nna 0; poke nna 1; poke nna 2; poke nnaraw 0 2 0; poke nnaraw 0 2 167; poke nnaraw 0 5 160; poke nnaraw 0 0 165; poke nnaraw 0 3 0; poke nnaraw 0 255 255; poke nnaraw 0 4 161; poke nna 0 0"
g() { grep "^$2" "$WORK/$1.poke.out" | sed -n "${3:-1}p"; }
expect "S-P2 sizeof(SynthPartSettings) 64, the pair at offset 62 (the old padding)" "structsizes SynthPartSettings 64 strMtValues 1792 nna_offset 62" "$(g p1 structsizes)"
expect "S-P1 a fresh part is Release, stored as 0 0" "nna 0 mode 0 raw 0 0" "$(g p1 'nna 0 ' 1)"
expect "S-P1 Fade is stored as 3 and 3 ^ 0xA5" "nna 0 mode 3 raw 3 166" "$(g p1 'nna 0 ' 2)"
expect "S-P1 round trip, part 1: Fade kept" "nna 0 mode 3 raw 3 166" "$(g p1 'nna 0 ' 4)"
expect "S-P1 round trip, part 2: Cut kept" "nna 1 mode 2 raw 2 167" "$(g p1 'nna 1 ' 3)"
expect "S-P1 round trip, part 3: Continue kept" "nna 2 mode 1 raw 1 164" "$(g p1 'nna 2 ' 3)"
expect "S-P1 garbage: a mode without its check byte reads Release" "nnaraw 0 mode 0 raw 2 0" "$(g p1 nnaraw 1)"
expect "S-P1 a valid pair reads its mode" "nnaraw 0 mode 2 raw 2 167" "$(g p1 nnaraw 2)"
expect "S-P1 garbage: mode 5 with a matching check byte reads Release" "nnaraw 0 mode 0 raw 5 160" "$(g p1 nnaraw 3)"
expect "S-P1 garbage: 0 with check 0xA5 reads Release" "nnaraw 0 mode 0 raw 0 165" "$(g p1 nnaraw 4)"
expect "S-P1 garbage: a check byte without the mode reads Release" "nnaraw 0 mode 0 raw 3 0" "$(g p1 nnaraw 5)"
expect "S-P1 garbage: 255 255 reads Release" "nnaraw 0 mode 0 raw 255 255" "$(g p1 nnaraw 6)"
expect "S-P1 garbage: 4 with its check byte (mode 4 does not exist) reads Release" "nnaraw 0 mode 0 raw 4 161" "$(g p1 nnaraw 7)"
PMT="$WORK/p1/Projects/nnaP1/project.mt"
expect "S-P2 the saved project.mt is 2,324 B (the size of the 1.4.1 file)" "2324" "$(stat -f %z "$PMT" 2>/dev/null || stat -c %s "$PMT")"
# a Release project is bit identical to what the base writes: save the same project with Release in every part, compare with the file the base build wrote
if [ -n "${BASE_BIN:-}" ] && [ -x "$BASE_BIN" ]; then
    run p2 "poke nna 0 0; saveproj nnaP2"
    rm -rf "$WORK/basep"; mkdir -p "$WORK/basep"
    "$BASE_BIN" "$WORK/basep" "wait 500; instr 64; saveproj nnaP2" >/dev/null 2>&1
    expect "S-P2 a Release project: project.mt bit identical to the base build's file" "equal" "$(cmp -s "$WORK/p2/Projects/nnaP2/project.mt" "$WORK/basep/Projects/nnaP2/project.mt" && echo equal || echo differs)"
    # a project the base wrote (padding 0 0, the file of a build that has no such field) opens as Release in all three parts
    rm -rf "$WORK/p3"; mkdir -p "$WORK/p3/Projects"; cp -R "$WORK/basep/Projects/nnaP2" "$WORK/p3/Projects/"
    UISHOT_POKE="$WORK/p3.poke" "$BIN" "$WORK/p3" "wait 500; instr 64; poke nna 0 3; poke nna 1 3; poke nna 2 3; openproj nnaP2; poke nna 0; poke nna 1; poke nna 2" >/dev/null 2>&1
    expect "S-P1 a project written by the base build (no field) opens as Release in all three parts" "nna 0 mode 0 raw 0 0|nna 1 mode 0 raw 0 0|nna 2 mode 0 raw 0 0" "$(grep '^nna ' "$WORK/p3.poke.out" | tail -3 | tr '\n' '|' | sed 's/|$//')"
fi
# S-U1: the "New note" block of the Synth screen (page 1/3): PNG goldens, byte equal (UPDATE_GOLDENS=1 writes them). Place 7 is the block, the Up key
# steps the value (Release, Continue, Cut, Fade), the Left key goes back to the slot panel (place 6)
GOLDS="$HERE/goldens/synth_nna"
mkdir -p "$WORK/png"
SP="instr 64; tap Params; wait 1200; wait 2500"
run u1 "$SP; tap Right 7; wait 300; shot $WORK/png/place7-release.png; poke nna 0; tap Up; wait 300; shot $WORK/png/place7-v1.png; poke nna 0; tap Up; wait 300; shot $WORK/png/place7-v2.png; poke nna 0; tap Up; wait 300; shot $WORK/png/place7-v3.png; poke nna 0; tap Down; tap Down; tap Down; tap Down; wait 200; poke nna 0; tap Left; wait 300; shot $WORK/png/place6.png"
# the value steps (poke prints the stored mode after each key)
UI_STEPS="$(grep '^nna 0 ' "$WORK/u1.poke.out" | awk '{printf "%s%s", (NR > 1 ? " " : ""), $4}')"
expect "S-U1 keys: Release, then Up steps through the modes in order and stops at the last (and back down to Release)" "$NNA_UI_WANT" "$UI_STEPS"
for g in place7-release place7-v1 place7-v2 place7-v3 place6; do
    [ -n "${UPDATE_GOLDENS:-}" ] && { mkdir -p "$GOLDS"; cp "$WORK/png/$g.png" "$GOLDS/$g.png"; }
    expect "S-U1 golden $g: the screen is byte equal to goldens/synth_nna/$g.png" "equal" "$([ -f "$WORK/png/$g.png" ] && cmp -s "$WORK/png/$g.png" "$GOLDS/$g.png" && echo equal || echo differs)"
done

# ============================================================================================================================================
# Offline render hashes
# ============================================================================================================================================
# A project is a script that sets the part Voices and the steps, plays N ms and stops. Hash of every block the shim renders for each part.
hashrun() {   # hashrun NAME "setup script" PLAY_MS
    run "h_$1" "$2; audio 1; poke renderhash reset; tap Play; wait $3; tap Play; wait 600; poke renderhash"
    grep '^renderhash' "$WORK/h_$1.poke.out" | tail -1 | sed 's/^renderhash \([0-9a-f]*\) \([0-9a-f]*\) \([0-9a-f]*\) .*/\1 \2 \3/'
}
declare -a PROJ_NAMES PROJ_CLASS
PROJ_NAMES=(melody overlap chord_strum mono_legato arp roll gate three_parts mute_mid cutoff same_pitch same_pitch_mono pad_over_seq)
PROJ_CLASS=(0 0 0 0 0 0 0 0 0 0 1 1 1)
declare -a PROJ_SETUP PROJ_MS
PROJ_SETUP[0]="$VOICES4; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 4 67 64 0 0 0 0; setstep 0 6 72 64 0 0 0 0; setstep 0 7 -4 0 0 0 0 0; setstep 0 8 62 64 0 0 0 0; setstep 0 10 -4 0 0 0 0 0"
PROJ_MS[0]=1500
PROJ_SETUP[1]="$VOICES4; setstep 0 0 48 64 0 0 0 0; setstep 1 1 64 64 0 0 0 0; setstep 2 2 67 64 0 0 0 0; setstep 0 6 -4 0 0 0 0 0; setstep 1 7 -4 0 0 0 0 0; setstep 2 8 -4 0 0 0 0 0; setstep 0 9 50 64 0 0 0 0; setstep 1 11 53 64 0 0 0 0; setstep 0 13 -4 0 0 0 0 0"
PROJ_MS[1]=1900
# chord_strum pins the Strum ramp depth to 0: the per note strum velocity ramp of 1.4.0 (default depth 100) changes the strummed velocities, and the P0 hash
# was recorded before it. With depth 0 the hash is the P0 one bit for bit (checked on the merge into release-1.4.0-int)
PROJ_SETUP[2]="poke strumdepth 0; $VOICES4; setstep 0 0 60 64 14 24 47 10; setstep 0 4 62 64 14 5 47 74; setstep 0 8 65 64 14 24 47 63; setstep 0 9 60 64 14 5 0 0; setstep 0 12 -4 0 0 0 0 0"
PROJ_MS[2]=1900
PROJ_SETUP[3]="$VOICES1; setstep 0 0 60 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 1 3 67 64 0 0 0 0; setstep 0 4 62 64 0 0 0 0; setstep 1 5 -4 0 0 0 0 0; setstep 0 6 -4 0 0 0 0 0; setstep 0 8 65 64 0 0 0 0; setstep 0 10 -4 0 0 0 0 0"
PROJ_MS[3]=1500
PROJ_SETUP[4]="$VOICES4; setstep 0 0 60 64 14 24 21 6; setstep 0 4 -4 0 0 0 0 0; setstep 0 6 62 64 14 5 21 8; setstep 0 10 -4 0 0 0 0 0"
PROJ_MS[4]=1500
PROJ_SETUP[5]="$VOICES4; setstep 0 0 60 64 3 4 0 0; setstep 0 4 62 64 3 8 0 0; setstep 0 8 -4 0 0 0 0 0"
PROJ_MS[5]=1300
PROJ_SETUP[6]="$VOICES4; setstep 0 0 60 64 20 40 0 0; setstep 0 2 64 64 20 80 0 0; setstep 1 3 67 64 20 20 0 0; setstep 0 4 72 64 0 0 0 0; setstep 0 6 -4 0 0 0 0 0"
PROJ_MS[6]=1100
PROJ_SETUP[7]="poke voicecount 0 3; poke voicecount 1 2; poke voicecount 2 1; setstep 0 0 60 64 0 0 0 0; setstep 1 0 55 65 0 0 0 0; setstep 2 1 67 66 0 0 0 0; setstep 0 4 64 64 0 0 0 0; setstep 1 5 -4 0 0 0 0 0; setstep 2 6 72 66 0 0 0 0"
PROJ_MS[7]=1000
PROJ_SETUP[8]="$VOICES4; setstep 0 0 60 64 0 0 0 0; setstep 1 1 64 64 0 0 0 0; setstep 0 4 67 64 0 0 0 0; setstep 1 5 71 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0"
PROJ_MS[8]=1300
PROJ_SETUP[9]="poke voicecount 0 2; setstep 0 0 60 64 0 0 0 0; setstep 0 1 64 64 0 0 0 0; setstep 0 2 67 64 0 0 0 0; setstep 0 3 72 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; setstep 0 5 55 64 0 0 0 0; setstep 0 6 -2 0 0 0 0 0; setstep 0 7 57 64 0 0 0 0; setstep 0 8 -3 0 0 0 0 0"
PROJ_MS[9]=1300
PROJ_SETUP[10]="$VOICES4; setstep 0 0 60 64 0 0 0 0; setstep 1 2 60 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; setstep 1 8 -4 0 0 0 0 0"
PROJ_MS[10]=1300
PROJ_SETUP[11]="$VOICES1; setstep 0 0 60 64 0 0 0 0; setstep 1 2 60 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0; setstep 1 8 -4 0 0 0 0 0"
PROJ_MS[11]=1300
PROJ_SETUP[12]="$VOICES4; setstep 0 0 72 64 0 0 0 0; setstep 0 4 -4 0 0 0 0 0"
PROJ_MS[12]=900

# the mute project needs the mute at a point of the play: its own script
declare -A HASH
for i in "${!PROJ_NAMES[@]}"; do
    n="${PROJ_NAMES[$i]}"
    case "$n" in
      mute_mid)
        run "h_$n" "${PROJ_SETUP[$i]}; audio 1; poke renderhash reset; tap Play; wait 300; poke mute 1 1; wait 400; poke mute 1 0; wait ${PROJ_MS[$i]}; tap Play; wait 600; poke renderhash" ;;
      pad_over_seq)
        run "h_$n" "${PROJ_SETUP[$i]}; audio 1; poke renderhash reset; tap Play; padon 0 100; wait 500; padoff 0 100; wait ${PROJ_MS[$i]}; tap Play; wait 600; poke renderhash" ;;
      *) run "h_$n" "${PROJ_SETUP[$i]}; audio 1; poke renderhash reset; tap Play; wait ${PROJ_MS[$i]}; tap Play; wait 600; poke renderhash" ;;
    esac
    HASH[$n]="$(grep '^renderhash' "$WORK/h_$n.poke.out" | tail -1 | sed 's/^renderhash \([0-9a-f]*\) \([0-9a-f]*\) \([0-9a-f]*\) blocks.* loud \([0-9]*\) .*/\1 \2 \3 loud \4/')"
done

if [ "${UPDATE:-}" = p0 ]; then
    { echo "# synth_nna_sim.sh: FNV-1a 64 of every block the shim renders for parts 1 2 3 (poke renderhash), recorded on 814dc1b (P0, before the note ids)"
      echo "# name class(1 = holds two live notes of one pitch, the note id changes it on purpose) h0 h1 h2 loud(blocks of part 1 that were not silent)"
      for i in "${!PROJ_NAMES[@]}"; do n="${PROJ_NAMES[$i]}"; echo "$n ${PROJ_CLASS[$i]} ${HASH[$n]}"; done; } >"$GOLD"
    echo "wrote $GOLD"
fi
if [ "${UPDATE:-}" = p1 ]; then
    { echo "# synth_nna_sim.sh: the same hashes for the projects of class 1 once the notes carry ids (P1), the other projects stay at the P0 hash"
      for i in "${!PROJ_NAMES[@]}"; do n="${PROJ_NAMES[$i]}"; [ "${PROJ_CLASS[$i]}" = 1 ] && echo "$n ${HASH[$n]}"; done; } >"$GOLD1"
    echo "wrote $GOLD1"
fi
for i in "${!PROJ_NAMES[@]}"; do
    n="${PROJ_NAMES[$i]}"
    want_hash="$(grep "^$n " "$GOLD" 2>/dev/null | head -1 | cut -d' ' -f3-)"
    loud="${HASH[$n]##* }"
    expect "render $n: part 1 is not silent ($loud blocks)" "yes" "$([ "$loud" -gt 100 ] && echo yes || echo no)"
    if [ "${PROJ_CLASS[$i]}" = 1 ]; then
        # the one case that changes on purpose: two live notes of one pitch from two sources (D1). Frozen at the P1 hash
        expect "render $n (class: same pitch, two sources): differs from the P0 hash" "differs" "$([ "${HASH[$n]}" != "$want_hash" ] && echo differs || echo same)"
        expect "render $n (class): the P1 hash" "$(grep "^$n " "$GOLD1" 2>/dev/null | head -1 | cut -d' ' -f2-)" "${HASH[$n]}"
    else
        expect "render $n: bit exact with the P0 hash" "$want_hash" "${HASH[$n]}"
    fi
done

# ============================================================================================================================================
# P3: the NNA step FX (FX id 54, nna-fx). It sets the New note action of the note on its step, for that note only, over the setting of the part
# (poke nna P MODE): value 0 Release, 1 Continue, 2 Cut, 3 Fade. Every row runs on a part whose setting is the OTHER one, so the FX is the only reason
# for a change. Steps without the FX follow the part. These rows fail on a7505fc1 (FX 54 is no FX there: the part setting decides)
# ============================================================================================================================================
NNA=54
# S-N1 Cut: the S-C1 scenario with the part on Release, the FX on the first note (track 1 step 0) with value 2
run n1 "$VOICES4; poke nna 0 0; setstep 0 0 60 64 $NNA 2 0 0; setstep 1 0 67 64 0 0 0 0; setstep 2 0 71 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; tap Play"
expect "S-N1 st0: three held notes" "0:1:60:a 1:1:67:b 2:1:71:c" "$(v n1 0 1)"
expect "S-N1 Cut FX on a Release part, st2: the replaced voice is gone in one block, the other two held notes stay" "1:1:67:b 2:1:71:c 3:1:64:d" "$(v n1 0 2)"
# the same FX in the second slot
run n1s "$VOICES4; poke nna 0 0; setstep 0 0 60 64 0 0 $NNA 2; setstep 1 0 67 64 0 0 0 0; setstep 2 0 71 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; wait 125; wait 125; $P; tap Play"
expect "S-N1 the FX in slot 2 is the same (ids by first poll)" "1:1:67:a 2:1:71:b 3:1:64:c" "$(v n1s 0 1)"
# S-N2 Fade: the S-F1 scenario with the FX, the part on Release. About 120 ms ramp, 29 ms = 10 blocks apart
run n2 "$VOICES4; poke nna 0 0; setstep 0 0 60 64 $NNA 3 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 100; $P; wait 29; $P; wait 29; $P; tap Play"
expect "S-N2 Fade FX on a Release part, st2: the old voice is released and on the ramp, the new note on" "0:0:60:a~ 1:1:64:b" "$(v n2 0 2)"
na=$(left n2 2 0); nb=$(left n2 3 0)
expect "S-N2 the ramp is 1..41 blocks left after the replace (about 120 ms)" "yes" "$([ -n "$na" ] && [ "$na" -ge 1 ] && [ "$na" -le 41 ] && echo yes || echo no)"
expect "S-N2 29 ms (10 blocks) later the ramp is 10 blocks shorter" "yes" "$([ -n "$nb" ] && [ $((na - nb)) -ge 9 ] && [ $((na - nb)) -le 11 ] && echo yes || echo no)"
expect "S-N2 the old voice is off once the ramp is over" "1:1:64:b" "$(v n2 0 4)"
# S-N3 Continue: the S-K1 scenario with the FX on the first two notes, the part on Release: the replaced notes stay gated, parked, the Off step ends all
N3S="setstep 0 0 60 64 $NNA 1 0 0; setstep 0 2 64 64 $NNA 1 0 0; setstep 0 4 67 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0"
run n3 "$VOICES4; poke nna 0 0; $N3S; audio 1; tap Play; $P; wait 125; wait 125; $P; $PK; wait 125; wait 125; $P; $PK; wait 125; wait 125; wait 125; wait 125; $P; $PK; tap Play"
expect "S-N3 Continue FX st2: the replaced note stays gated, two voices" "0:1:60:a 1:1:64:b" "$(v n3 0 2)"
expect "S-N3 Continue FX st4: three gated voices, two notes parked" "0:1:60:a 1:1:64:b 2:1:67:c / 2" "$(v n3 0 3) / $(pk n3 2)"
expect "S-N3 Continue FX: the Off step releases all three, nothing is parked" "0:0:60:a 1:0:64:b 2:0:67:c / 0" "$(v n3 0 4) / $(pk n3 3)"
# S-N4 Release FX on a part that is on Cut: the note is released, not cut (the FX wins the other way too), Release value 0
run n4 "$VOICES4; poke nna 0 2; setstep 0 0 60 64 $NNA 0 0 0; setstep 1 0 67 64 0 0 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; tap Play"
expect "S-N4 Release FX on a Cut part: the replaced voice rings in its release" "0:0:60:a 1:1:67:b 2:1:64:c" "$(v n4 0 2)"
# S-N5 only that note: the part is on Release, the first note has the Cut FX, the second note has none: the second one is released at step 4 (no ramp, not cut),
# the third note has the Continue FX and stays gated when the fourth starts
run n5 "$VOICES4; poke nna 0 0; setstep 0 0 60 64 $NNA 2 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 4 67 64 $NNA 1 0 0; setstep 0 6 71 64 0 0 0 0; setstep 0 10 -4 0 0 0 0 0; audio 1; tap Play; $P; wait 125; wait 125; $P; wait 125; wait 125; $P; wait 125; wait 125; $P; tap Play"
expect "S-N5 st0: one voice" "0:1:60:a" "$(v n5 0 1)"
expect "S-N5 st2: the FX note is cut, the new one is on" "1:1:64:b" "$(v n5 0 2)"
expect "S-N5 st4: the note without the FX follows the part (Release): it rings in its release, no ramp" "0:1:67:c 1:0:64:b" "$(v n5 0 3)"
expect "S-N5 st6: the Continue FX note stays gated with the new one, the earlier released one still rings out" "0:1:67:c 1:0:64:b 2:1:71:d" "$(v n5 0 4)"
# the part setting is not changed by a step with the FX: a part on Release that played an NNA step releases the next note it replaces like before
run n6 "$VOICES4; poke nna 0 0; setstep 0 0 60 64 $NNA 2 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 4 67 64 0 0 0 0; setstep 0 10 -4 0 0 0 0 0; audio 1; tap Play; wait 125; wait 125; wait 125; wait 125; wait 80; $P; tap Play"
expect "S-N6 a part on Release after an NNA step still releases (no ramp, the old note rings)" "0:1:67:a 1:0:64:b" "$(v n6 0 1)"
# S-N7 a sample step with the FX does nothing (no synth voice is touched, the run ends clean): the FX is a model only FX away from synth parts
run n7 "$VOICES4; poke nna 0 0; setstep 0 0 60 0 $NNA 2 0 0; setstep 0 2 64 64 0 0 0 0; setstep 0 8 -4 0 0 0 0 0; audio 1; tap Play; wait 125; wait 125; $P; tap Play"
expect "S-N7 NNA FX on a sample step: the synth note plays as usual" "0:1:64:a" "$(v n7 0 1)"

new=$(cat "$WORK"/*.err 2>/dev/null | grep "SUMMARY" | grep -v "mtWavetableEnvelopes.cpp:206" | sort | uniq -c)
if [ -n "$new" ]; then fails=$((fails+1)); echo "FAIL  sanitizer report in a run:"; echo "$new"; fi
echo "$checks checks"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
