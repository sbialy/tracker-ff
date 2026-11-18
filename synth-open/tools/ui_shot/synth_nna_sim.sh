#!/bin/bash
# synth_nna_sim.sh [PNG_DIR]
#
# What a synth part does with its notes, on the real sequencer, the real shim (synth-open/open_synth.cpp) and the real engines of this tree
# (ui_shot, the host build, nothing here touches a device). Every scenario has its own SD card folder. The sequencer runs with "audio 1"
# (the firmware's Tracker::updateAudioBuffers, 128 frame blocks of 2,901 us on the virtual clock), a script plays a few steps and reads the
# voices of the synth part back with "poke voices" (host_poke.cpp): one entry per voice that is on, SLOT:GATE:NOTE:ID:FADE. The id is the
# per note id of the shim (0 = no id, the note came from the pad, MIDI in or record). Ids are printed by first appearance (a, b, c ...).
# At 120 BPM a step is 125 ms: "tap Play" takes 80 ms, so the polls after it and every "wait 125" later sit in the middle of a step.
#
# Rows: S-ID1..S-ID6 are the note id phase (P1 of ref/synth-nna-design.md). The first commit of this script (P0) recorded what 814dc1b does
#       before any note id: its rows are the same scenarios with the P0 answers, "P0:" in a row text names the one that changed. F13 is "the
#       sequencer's Stop never reaches the shim's kStop" (the answer: it does not, in P0 and now)
# Reference of the sound: the FNV-1a 64 of every block the shim renders for a part (poke renderhash) over a set of projects, goldens/synth_nna_hashes.txt
# (UPDATE=p0 / UPDATE=p1 rewrite the P0 / the P1 file, only for a change of the sound that is wanted). A project marked "class" holds two live notes of one pitch
# started by different sources, the one case the note id changes on purpose (D1 of the design).
# SAN=address,undefined: the sanitizer build. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
GOLD="$HERE/goldens/synth_nna_hashes.txt"        # P0, 814dc1b, before the note ids
GOLD1="$HERE/goldens/synth_nna_hashes_p1.txt"    # P1: the projects of class 1 after the note ids
WORK="$(mktemp -d "${TMPDIR:-/tmp}/synth_nna_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

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
          line = line (line == "" ? "" : " ") f[1] ":" f[2] ":" f[3] ":" (id == 0 ? "0" : seen[id]) (f[5] == 1 ? "F" : "")
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

new=$(cat "$WORK"/*.err 2>/dev/null | grep "SUMMARY" | grep -v "mtWavetableEnvelopes.cpp:206" | sort | uniq -c)
if [ -n "$new" ]; then fails=$((fails+1)); echo "FAIL  sanitizer report in a run:"; echo "$new"; fi
echo "$checks checks"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
