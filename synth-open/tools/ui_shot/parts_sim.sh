#!/bin/bash
# parts_sim.sh: host test of Parts (core/source/parts.h, ref/parts.md) on the ui_shot simulator. Mac only, no device.
#
#   synth-open/tools/ui_shot/parts_sim.sh [WORKDIR]
#
# Needs build/ui-shot/ui_shot (make -C synth-open/tools/ui_shot) and build/ui-shot-noparts/ui_shot (the same with PARTS=0, the
# tree without Parts, which reads a project the way 1.3.0 does: the parts sidecar is an unknown file to it).
# The sequencer runs with "audio 1" and every note sent to USB MIDI out is logged ("midilog 1"): each track plays a MIDI
# instrument of its own channel, so the log says which pattern's track played in which pass.
#
#   A  build a project: patterns 1-3, playlist 1 2, Parts made in the Song screen's Parts view (Shift + F8, Rec = Build, Insert,
#      hold a track button + pad, Shift + encoder), Part 0 = track 1 from pattern 1, track 2 from pattern 2; Part 1 = the reverse,
#      x2 (old data, set directly: the Bars ladder has no Auto x2); Part 2 = End. Saved as /Projects/PartsTest
#   B  boot from that Workspace, Play Song in the Parts view: passes P0 P1 P1 P0 P1 (End wraps to Part 0). While it plays: the
#      pattern editor takes its edit mode (live edits), a change flag with no edit stores nothing, Performance is refused. After
#      Stop the pattern on screen is pattern 1 again and no pattern file changed
#   C  reopen /Projects/PartsTest: the same Parts list
#   D  the project without its sidecar (as 1.3.0 saved it): no Parts, the Song screen plays the playlist P1 P2 P1
#   E  the no-Parts build plays D's project: the same notes at the same times as D (playlist play unchanged)
#   F  the no-Parts build opens and saves the Parts project: it plays the playlist, the sidecar survives the save unchanged
#   G  Bake (F5 in the Parts view): new patterns plus playlist that play P0 P1 P1, also on the no-Parts build
#   H  live edits while the Parts play (ref/parts-live.md): an edit of the playing image is heard at once, goes to its source pattern
#      file, reaches the next Part that plays the same source track (ready image patched), stays out of Parts that play another
#      source on that track, an edit in the last steps of a Part is stored at the swap, a project save during play stores the
#      edits, the Workspace save runs during play, the playlist and the other patterns are untouched, Stop shows the edited pattern
#   I  Shift + Song opens the Song screen in the Parts view from the Pattern screen, Shift + Song there switches the view
#   J  Build edits: cursor + pad, a tap Off / back, hold + encoder, Copy and Insert after / Shift + Insert before, Delete (cells Off),
#      Shift + Delete (Parts), Shift + encoder Length by 1 step (Auto 16 + 2 = 18, + 7 = 25), pad LEDs of the cursor cell
#   K  the display list with 1, 7, 8, 12, 16 and 40 Parts, every cell its own 3 digit pattern: the frame stays under the 2,043 word
#      cap of display.cpp, nothing overflows, the stock grid is not drawn (7 or more rows of it overflowed its 8 KB slot)
#   L  Performance Insert: what Performance plays (track 1 switched to pattern 2, track 3 muted) becomes a Part after the
#      selected one, a second Insert appends after it, the Parts view shows them
#   M  Arrange keys: F3 Go to Start, F4 Go to End, Shift + F3 the Part that played last, the window follows the playing Part
#   V  Build: the arrows reach the Length and Repeats fields (cursor box), the encoder edits them, saved and restored (ref/parts-arrows.md)
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (...)" (exit 1).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${UI_SHOT_BIN:-${BIN:-$ROOT/build/ui-shot/ui_shot}}"            # UI_SHOT_BIN: the plain binary, UI_SHOT_BIN0: the PARTS=0 one (this script never makes either)
BIN0="${UI_SHOT_BIN0:-${BIN0:-$ROOT/build/ui-shot-noparts/ui_shot}}"
W="${1:-$ROOT/build/parts-sim}"
rm -rf "$W"; mkdir -p "$W/png"
fail=0
bad() { echo "FAIL: $*"; fail=$((fail+1)); }
ok() { echo "ok: $*"; }
[ -x "$BIN" ] || { echo "no $BIN"; echo "RESULT: RED (no binary)"; exit 1; }
[ -x "$BIN0" ] || { echo "no $BIN0 (make -C synth-open/tools/ui_shot PARTS=0)"; echo "RESULT: RED (no binary)"; exit 1; }

run() { # run BIN SD LOG SCRIPT
  timeout 600 "$1" "$2" "$4" > "$3" 2>&1 || { echo "ui_shot exit $? ($3)"; tail -5 "$3"; }
}
# "pass" lines: the note-ons of one pass (same time stamp), as ch:note pairs sorted, with the time
passes() { python3 -I - "$1" <<'EOF'
import sys, collections
ons = collections.OrderedDict()
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) >= 8 and p[0] == 'midi' and p[1] == 'on':   # midi on NOTE VEL ch CH t MS
        ons.setdefault(p[7], []).append('%s:%s' % (p[5], p[2]))
for t, v in ons.items():
    print(t, ' '.join(sorted(v)))
EOF
}
hashes() { (cd "$1" && find . -name 'pattern_*.mtp' -type f | sort | xargs shasum | shasum | cut -c1-12); }

# notes: track 1 plays MIDI channel 1 (instrument 48), track 2 channel 2. Pattern 1: 60 / 62, pattern 2: 72 / 74, pattern 3: 48.
# The MIDI out adds 24 (the default octave of the MIDI instruments): P0 = 1:84 2:98, P1 = 1:96 2:86, pattern 1 = 1:84 2:86, pattern 2 = 1:96 2:98
P0="1:84 2:98"; P1="1:96 2:86"; PAT1="1:84 2:86"; PAT2="1:96 2:98"
SETUP='pattern 1;plen 16;setstep 0 0 60 48 0 0 0 0;setstep 1 0 62 49 0 0 0 0;dirty;pattern 2;plen 16;setstep 0 0 72 48 0 0 0 0;setstep 1 0 74 49 0 0 0 0;dirty;pattern 3;plen 8;setstep 0 0 48 48 0 0 0 0;dirty;pattern 1;playlist 1 2;wait 300'
# Parts view by Shift + F8, Build (Rec), Insert makes Part 0 from the pattern on screen (tracks 1 and 2 hold notes, the rest Off), Insert
# twice more duplicates it. Before that F3-F8 held + pad 0 put pattern 1 on tracks 3-8, Right 8 flips the F-row to tracks 9-16 and
# F1-F8 held + pad 0 put it there (H needs pattern 1 on tracks 3-16). Part 2: Shift + encoder down = End. Part 0: hold F2 (track 2)
# + pad 1 = pattern 2. Part 1: hold F1 + pad 1
PARTS_UI='tap Song;wait 300;shot '"$W"'/png/A1-stock.png;hold Shift;tap F8;unhold Shift;wait 200;shot '"$W"'/png/A2-parts-empty.png;tap Rec;wait 100;shot '"$W"'/png/A2b-build-empty.png;tap Insert;hold F3;hold F4;hold F5;hold F6;hold F7;hold F8;pad 0;unhold F3;unhold F4;unhold F5;unhold F6;unhold F7;unhold F8;tap Right 8;hold F1;hold F2;hold F3;hold F4;hold F5;hold F6;hold F7;hold F8;pad 0;unhold F1;unhold F2;unhold F3;unhold F4;unhold F5;unhold F6;unhold F7;unhold F8;tap Left 8;shot '"$W"'/png/A2c-build-part1.png;tap Insert;tap Insert;parts;hold Shift;tap Left;unhold Shift;tap Up 2;hold F2;pad 1;unhold F2;tap Down;hold F1;pad 1;unhold F1;hold Shift;tap Right;unhold Shift;wait 100;shot '"$W"'/png/A3-build.png;tap Rec;wait 200;shot '"$W"'/png/A4-arrange.png;parts'

echo "### A build and save"
SD="$W/sd"
run "$BIN" "$SD" "$W/A.log" "$SETUP;$PARTS_UI;projsave /Projects PartsTest;parts"
grep -q '^projsave /Projects/PartsTest started 1 idle 1' "$W/A.log" && ok "project saved" || bad "project save ($(grep projsave "$W/A.log"))"
tail -6 "$W/A.log" | grep -q '^parts count 3 mode 1 view 1' && ok "3 Parts in the Parts view" || bad "Parts after the UI edits: $(grep '^parts count' "$W/A.log" | tail -1)"
grep -q '^part 1 len 0 rep 2 ' "$W/A.log" && ok "Shift + Right set the repeats of Part 1 to 2 (Length stays Auto)" || bad "repeats of Part 1"
grep -q '^part 2 len 0 rep 0 ' "$W/A.log" && ok "Shift + Left took the repeats of Part 2 to End" || bad "End Part"
grep -q '^part 0 len 0 rep 1 flags 1 baked 0 src 1 2 1 1 1 1 1 1 1 1 1 1 1 1 1 1$' "$W/A.log" && grep -q '^part 1 len 0 rep 2 flags 1 baked 0 src 2 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1$' "$W/A.log" && ok "Insert from the pattern on screen, held track buttons + pad set the cells (one or many, both halves)" || bad "A cells: $(grep '^part ' "$W/A.log" | tail -3 | tr '\n' '|')"
# the Parts list is the pat.parts.mode and pat.part.N keys of project.mtff (the file has the stamp and the Strum ramp too)
PK() { grep '^pat\.' "$1" 2>/dev/null; }
[ "$(PK "$SD/Workspace/project.mtff" | grep -c '^pat\.part\.[0-9]')" = 3 ] && ok "Workspace/project.mtff holds 3 Part keys" || bad "no Part keys in Workspace/project.mtff"
[ "$(PK "$SD/Projects/PartsTest/project.mtff" | grep -c '^pat\.part\.[0-9]')" = 3 ] && ok "Projects/PartsTest/project.mtff holds 3 Part keys" || bad "no Part keys in Projects/PartsTest/project.mtff"
[ -n "$(PK "$SD/Workspace/project.mtff")" ] && [ "$(PK "$SD/Workspace/project.mtff")" = "$(PK "$SD/Projects/PartsTest/project.mtff")" ] && ok "project sidecar keys = Workspace sidecar keys" || bad "sidecar keys differ"
[ ! -e "$SD/Workspace/parts" ] && [ ! -e "$SD/Projects/PartsTest/parts" ] && ok "no file named parts any more" || bad "a parts file was written"
[ "$(stat -f %z "$SD/Projects/PartsTest/project.mt")" = "$(stat -f %z "$SD/Workspace/project.mt" 2>/dev/null || echo x)" ] || true
H0=$(hashes "$SD/Workspace")
cp -R "$SD/Projects/PartsTest" "$W/PartsTest.saved"

echo "### B boot, Play Song through the Part transitions, edit mode on, Performance refused"
run "$BIN" "$SD" "$W/B.log" 'parts;tap Song;wait 300;tap Up 3;audio 1;midilog 1;tap F2;wait 300;parts;shot '"$W"'/png/B1-playing.png;tap Pattern;wait 300;tap Rec;wait 100;cursor;shot '"$W"'/png/B2-edit-refused.png;dirty;tap Performance;wait 300;status;tap Song;wait 3000;parts;wait 6000;parts;midilog 0;audio 0;step 0 0;step 1 0;wait 500'
head -3 "$W/B.log" | grep -q '^parts count 3 mode 1' && ok "boot loads the Workspace Parts" || bad "boot Parts: $(grep -m1 '^parts count' "$W/B.log")"
grep -q 'parts count 3 mode 1 view 1 playing 1 cur 0 ' "$W/B.log" && ok "Play Song starts at Part 0" || bad "start: $(grep '^parts count' "$W/B.log" | sed -n 2p)"
grep -q '^cursor .* edit 1' "$W/B.log" && ok "pattern editor takes its edit mode while a Part plays" || bad "edit mode while a Part plays: $(grep '^cursor' "$W/B.log")"
grep -q '^pattern 1 song 0 .* perf 0' "$W/B.log" && ok "Performance refused while a Part plays" || bad "Performance: $(grep '^pattern ' "$W/B.log")"
passes "$W/B.log" > "$W/B.passes"
GOT=$(cut -d' ' -f2- "$W/B.passes" | tr '\n' '|')
WANT="$P0|$P1|$P1|"
[ "$GOT" = "$WANT" ] && ok "passes P0 P1 P1 and no more (the repeat plays, the song stops after the last Part, no wrap to Part 0)" || bad "passes: got [$GOT] want [$WANT]"
python3 -I - "$W/B.passes" <<'EOF' && ok "passes 1,848 ms apart (16 steps at 130 BPM, no late switch)" || bad "pass timing"
import sys
t = [float(l.split()[0]) for l in open(sys.argv[1])][:3]
d = [b - a for a, b in zip(t, t[1:])]
sys.exit(0 if d and max(d) - min(d) < 3 and abs(d[0] - 1846) < 6 else 1)
EOF
grep '^parts count' "$W/B.log" | tail -1 | grep -q 'playing 0 cur -1 .* play 0 ' && ok "the song ended by itself after the last pass: nothing plays, no Stop key was pressed" || bad "after the song end: $(grep '^parts count' "$W/B.log" | tail -1)"
grep -q '^step 0 0 note 60 ' "$W/B.log" && grep -q '^step 1 0 note 62 ' "$W/B.log" && ok "pattern 1 back on screen after Stop" || bad "pattern after stop: $(grep '^step' "$W/B.log" | tr '\n' ' ')"
[ "$(hashes "$SD/Workspace")" = "$H0" ] && ok "no pattern file changed by the Parts playback (risk R1)" || bad "pattern files changed"

echo "### C reopen the project"
run "$BIN" "$SD" "$W/C.log" 'projopen /Projects PartsTest;parts'
grep -q '^parts count 3 mode 1' "$W/C.log" && grep -q '^part 1 len 0 rep 2 flags 1 baked 0 src 2 1 ' "$W/C.log" && ok "reopened project has the same Parts" || bad "reopen: $(grep '^part' "$W/C.log" | tr '\n' ' ')"

echo "### D the project without a sidecar (1.3.0 style)"
cp -R "$W/PartsTest.saved" "$SD/Projects/NoSidecar"; rm -f "$SD/Projects/NoSidecar/project.mtff"
SONG='tap Song;wait 300;audio 1;midilog 1;tap F2;wait 5800;tap Play;wait 300;midilog 0;audio 0'
run "$BIN" "$SD" "$W/D.log" "projopen /Projects NoSidecar;parts;$SONG;parts"
grep -q '^parts count 0 mode 0 view 0' "$W/D.log" && ok "no sidecar: no Parts, pattern view" || bad "no sidecar: $(grep -m1 '^parts count' "$W/D.log")"
[ -n "$(PK "$SD/Workspace/project.mtff")" ] && bad "Part keys left in the Workspace from the previous project" || ok "Part keys gone from the Workspace on open"
passes "$W/D.log" > "$W/D.passes"
GOT=$(head -3 "$W/D.passes" | cut -d' ' -f2- | tr '\n' '|')
[ "$GOT" = "$PAT1|$PAT2|$PAT1|" ] && ok "playlist plays pattern 1, 2, 1" || bad "playlist passes: [$GOT]"

echo "### E the no-Parts build plays the same project the same way"
SD0="$W/sd0"; mkdir -p "$SD0/Projects"; cp -R "$W/PartsTest.saved" "$SD0/Projects/NoSidecar"; rm -f "$SD0/Projects/NoSidecar/project.mtff"
cp -R "$SD/Workspace" "$SD0/Workspace" 2>/dev/null
run "$BIN0" "$SD0" "$W/E.log" "projopen /Projects NoSidecar;$SONG"
grep '^midi ' "$W/D.log" > "$W/D.midi"; grep '^midi ' "$W/E.log" > "$W/E.midi"
[ -s "$W/D.midi" ] && cmp -s "$W/D.midi" "$W/E.midi" && ok "MIDI out identical to the no-Parts build ($(wc -l < "$W/D.midi" | tr -d ' ') events)" || bad "MIDI out differs from the no-Parts build"

echo "### F the no-Parts build (1.3.0 style) opens and saves the Parts project"
cp -R "$W/PartsTest.saved" "$SD0/Projects/PartsTest"
run "$BIN0" "$SD0" "$W/F.log" "projopen /Projects PartsTest;$SONG;projsave /Projects PartsTest"
passes "$W/F.log" > "$W/F.passes"
GOT=$(head -3 "$W/F.passes" | cut -d' ' -f2- | tr '\n' '|')
[ "$GOT" = "$PAT1|$PAT2|$PAT1|" ] && ok "no-Parts build plays the playlist of the Parts project" || bad "no-Parts play: [$GOT]"
[ -n "$(PK "$W/PartsTest.saved/project.mtff")" ] && [ "$(PK "$SD0/Projects/PartsTest/project.mtff")" = "$(PK "$W/PartsTest.saved/project.mtff")" ] && ok "the Part keys survive a save by the no-Parts build, byte for byte (unknown lines are kept)" || bad "Part keys lost or changed by the no-Parts save"
[ "$(stat -f %z "$SD0/Projects/PartsTest/project.mt")" = "$(stat -f %z "$W/PartsTest.saved/project.mt")" ] && ok "project.mt same size in both builds" || bad "project.mt size differs"

echo "### G Bake"
run "$BIN" "$SD" "$W/G.log" "projopen /Projects PartsTest;tap Song;wait 300;tap F5;wait 300;shot $W/png/G1-baked.png;parts;$SONG;parts;projsave /Projects Baked"
grep -A4 '^parts count 3 mode 0 view 0' "$W/G.log" | grep -q '^playlist 4 5 5$' && ok "bake wrote patterns 4 5, playlist 4 5 5, pattern view" || bad "bake: $(grep -A4 '^parts count' "$W/G.log" | head -5 | tr '\n' ' ')"
passes "$W/G.log" > "$W/G.passes"
GOT=$(head -4 "$W/G.passes" | cut -d' ' -f2- | tr '\n' '|')
[ "$GOT" = "$P0|$P1|$P1|$P0|" ] && ok "baked playlist plays P0 P1 P1 P0" || bad "baked passes: [$GOT]"
cp -R "$SD/Projects/Baked" "$SD0/Projects/Baked"
run "$BIN0" "$SD0" "$W/G0.log" "projopen /Projects Baked;$SONG"
passes "$W/G0.log" > "$W/G0.passes"
GOT=$(head -4 "$W/G0.passes" | cut -d' ' -f2- | tr '\n' '|')
[ "$GOT" = "$P0|$P1|$P1|$P0|" ] && ok "no-Parts build plays the baked song P0 P1 P1 P0" || bad "no-Parts baked passes: [$GOT]"

echo "### H live edits while the Parts play"
# a pass is 1,846 ms (16 steps at 130 BPM): Part 0 at 0, Part 1 at 1 and 2 passes, Part 0 at 3, Part 1 at 4 and 5.
# Instrument 48 + n plays MIDI channel n + 1, the MIDI out adds 24 to the note. Pattern 1 is the source of tracks 3-16 in every Part.
#   E1 two tracks of the same Part, same source: tracks 3 and 4 step 8 of pattern 1 -> 3:74 4:75 in every pass from the first on
#   E2 track 1 step 12 -> 1:91: Part 0 plays pattern 1 on track 1, Part 1 plays pattern 2 there and must not hear it
#   E3 track 5 step 0 entered in the last steps of Part 0 (stored by the swap, not the settle timer) -> 5:76 from pass 2 on
#   E4 track 6 step 4 entered right before a project save during play (/Projects/LiveSave) -> stored in the saved project
#   E5 track 7 step 2 after that save, then 9 s more of play: the 10 s Workspace autosave runs while the Parts play
cp -R "$SD" "$W/sdH"; SDH="$W/sdH"
hp23() { (cd "$1/Workspace/patterns" && shasum pattern_02.mtp pattern_03.mtp 2>/dev/null | shasum | cut -c1-12); }
HP23=$(hp23 "$SDH")
run "$BIN" "$SDH" "$W/H.log" 'projopen /Projects PartsTest;tap Song;wait 300;tap Up 3;tap F6;audio 1;midilog 1;tap F2;wait 100;tap Pattern;wait 100;tap Rec;wait 50;cursor;setstep 2 8 50 50 0 0 0 0;setstep 3 8 51 51 0 0 0 0;dirty;setstep 0 12 67 48 0 0 0 0;dirty;wait 1000;parts;setstep 4 0 52 52 0 0 0 0;dirty;parts;wait 200;parts;wait 700;setstep 5 4 53 53 0 0 0 0;dirty;projsave /Projects LiveSave;setstep 6 2 54 54 0 0 0 0;dirty;wait 9000;parts;tap Play;wait 300;midilog 0;audio 0;parts;step 6 2;step 2 8;step 3 8;step 0 12;step 4 0;step 5 4;step 0 0;step 1 0;pattern 2;step 0 0;step 1 0;step 0 12;step 2 8;pattern 1;tap Song;wait 200;parts'
grep -q '^cursor .* edit 1' "$W/H.log" && ok "Rec on the Pattern screen while a Part plays: edit mode on" || bad "edit mode: $(grep '^cursor' "$W/H.log")"
python3 -I - "$W/H.log" <<'PY' && ok "E1 E2 E3 heard as planned (same pass, next Part, other Parts' tracks untouched, wrap edit)" || bad "live edit audio"
import sys
ons = []
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) >= 8 and p[0] == 'midi' and p[1] == 'on':
        ons.append(('%s:%s' % (p[5], p[2]), float(p[7])))
L = 1846.15
fails = []
def want(ev, t, yes, why):
    if any(e == ev and abs(x - t) < 6 for e, x in ons) != yes:
        fails.append('%s %s at %.0f ms (%s)' % ('missing' if yes else 'unexpected', ev, t, why))
part = [0, 1, 1, 0, 1, 1]
for k in range(6):
    t0 = k * L
    for ev in ('3:74', '4:75'):
        want(ev, t0 + 8 * L / 16, True, 'E1 pass %d' % k)
    want('1:91', t0 + 12 * L / 16, part[k] == 0, 'E2 pass %d, Part %d' % (k, part[k]))
want('5:76', 0, False, 'E3 is not there before it is entered')
for k in (2, 3, 4, 5):
    want('5:76', k * L, True, 'E3 pass %d' % k)
for f in fails:
    print('  ' + f)
sys.exit(1 if fails else 0)
PY
H1=$(grep '^parts count' "$W/H.log" | sed -n 1p); H2=$(grep '^parts count' "$W/H.log" | sed -n 2p); H3=$(grep '^parts count' "$W/H.log" | sed -n 3p)
echo "$H2" | grep -q ' cur 0 .* pos 1[345] ' && echo "$H3" | grep -q ' cur 1 ' && ok "E3 entered at $(echo "$H2" | grep -o 'pos [0-9]*') of Part 0, Part 1 playing 200 ms later (stored by the swap path)" || bad "E3 timing: [$H1] [$H2] [$H3]"
grep -q '^projsave /Projects/LiveSave started 1 idle 1' "$W/H.log" && ok "project saved while a Part plays" || bad "save during play: $(grep projsave "$W/H.log")"
LAST=$(grep '^parts count .* playing 1 ' "$W/H.log" | tail -1)
echo "$LAST" | grep -q ' late 0 ' && ok "no late Part switch with live edits" || bad "late switches: $LAST"
SAVES=$(echo "$LAST" | sed 's/.* saves \([0-9]*\).*/\1/'); [ "${SAVES:-0}" -ge 2 ] && ok "the Workspace save hook ran $SAVES times while the Parts played (project save + autosave)" || bad "Workspace saves while playing: $SAVES"
STORED=$(echo "$LAST" | sed 's/.* stored \([0-9]*\).*/\1/'); [ "${STORED:-0}" -ge 3 ] && ok "$STORED pattern file writes by live edits" || bad "live edit writes: $STORED"
AFTER=$(grep -A20 '^parts count .* playing 0 ' "$W/H.log" | grep '^step' | head -12)
for want in 'step 6 2 note 54 instr 54' 'step 2 8 note 50 instr 50' 'step 3 8 note 51 instr 51' 'step 0 12 note 67 instr 48' 'step 4 0 note 52 instr 52' 'step 5 4 note 53 instr 53' 'step 0 0 note 60 instr 48' 'step 1 0 note 62 instr 49'; do
  echo "$AFTER" | head -8 | grep -q "^$want " && ok "pattern 1 after Stop: $want" || bad "pattern 1 after Stop lacks: $want"
done
echo "$AFTER" | sed -n 9,12p | tr '\n' '|' | grep -q '^step 0 0 note 72 instr 48 [^|]*|step 1 0 note 74 instr 49 [^|]*|step 0 12 note -1 [^|]*|step 2 8 note -1 ' && ok "pattern 2 untouched (no image track stored into it)" || bad "pattern 2: $(echo "$AFTER" | sed -n 9,12p | tr '\n' '|')"
[ "$(hp23 "$SDH")" = "$HP23" ] && ok "pattern_02 and pattern_03 files unchanged" || bad "pattern_02/03 changed"
tail -4 "$W/H.log" | grep -q '^playlist 1 2$' && ok "stock playlist still 1 2" || bad "playlist: $(grep '^playlist' "$W/H.log" | tail -1)"
tail -5 "$W/H.log" | grep -q '^parts count 3 mode 1 view 1 playing 0 .* screen song' && ok "Parts list unchanged after the live edits" || bad "Parts after H: $(tail -5 "$W/H.log" | grep '^parts count')"
run "$BIN" "$SDH" "$W/H2.log" 'projopen /Projects LiveSave;step 5 4;step 2 8;step 1 0'
grep -q '^step 5 4 note 53 instr 53 ' "$W/H2.log" && grep -q '^step 2 8 note 50 ' "$W/H2.log" && grep -q '^step 1 0 note 62 ' "$W/H2.log" && ok "the project saved during play has E1 and E4 (entered 0 ms before the save) in pattern 1" || bad "LiveSave pattern 1: $(grep '^step' "$W/H2.log" | tr '\n' ' ')"

echo "### I Shift + Song"
run "$BIN" "$SDH" "$W/I.log" 'projopen /Projects PartsTest;tap Song;wait 200;hold Shift;tap F8;unhold Shift;wait 200;tap Pattern;wait 300;parts;hold Shift;tap Song;unhold Shift;wait 300;parts;shot '"$W"'/png/I1-shift-song.png;hold Shift;tap Song;unhold Shift;wait 300;parts;hold Shift;tap Song;unhold Shift;wait 300;parts;tap Pattern;wait 300;hold Shift;tap Song;unhold Shift;wait 300;parts'
ISEQ=$(grep '^parts count' "$W/I.log" | sed 's/.* view \([01]\) .* screen \([a-z]*\).*/\1\2/' | tr '\n' ' ')
[ "$ISEQ" = "0pattern 1song 0song 1song 1song " ] && ok "Shift + Song: Pattern -> Song in the Parts view, switches the view there, opens Parts from Pattern again" || bad "Shift + Song views: [$ISEQ] want [0pattern 1song 0song 1song 1song ]"

echo "### J Build edits"
SDJ="$W/sdJ"; mkdir -p "$SDJ"
BUILD='tap Song;wait 300;hold Shift;tap F8;unhold Shift;wait 200;tap Rec;wait 100;tap Insert'
# Part 0 from pattern 1 (src 1 1 0 ...). Cursor to track 3, pad 4 = pattern 5 there. Tap F1 = track 1 Off, again = back to 1.
# Hold F2 + encoder +2 = track 2 pattern 3. Copy, Insert = Part 1 after, Shift + Insert = a copy before (new Part 1).
# Part 1 (sel): Delete = cursor cell Off. Shift + Down, Shift + Delete = Parts 1-2 gone. Shift + encoder +3 on Part 0 = 2 bars, +8 more = 12 bars
J1="$SETUP;$BUILD;tap Right 2;pad 4;parts;tap F1;parts;tap F1;parts;hold F2;enc 2;unhold F2;parts;padleds;shot $W/png/J1-build-cursor.png"
J2="tap Copy;wait 100;shot $W/png/J2-copied.png;tap Insert;parts;hold Shift;tap Insert;unhold Shift;parts;shot $W/png/J3-pasted.png;tap Delete;parts"
J3="hold Shift;tap Down;unhold Shift;shot $W/png/J4-range.png;hold Shift;tap Delete;unhold Shift;parts;tap Up 3;hold Shift;enc 2;unhold Shift;parts;hold Shift;enc 7;unhold Shift;parts;shot $W/png/J5-bars.png"
run "$BIN" "$SDJ" "$W/J.log" "$J1;$J2;$J3"
PJ=$(grep '^part 0 ' "$W/J.log" | tr '\n' '|')
want_j() { echo "$PJ" | cut -d'|' -f"$1" | grep -q "src $2\$" && ok "$3" || bad "$3: $(echo "$PJ" | cut -d'|' -f"$1")"; }
want_j 1 "1 1 5 0 0 0 0 0 0 0 0 0 0 0 0 0" "cursor track 3 + pad 5 = pattern 5"
want_j 2 "0 1 5 0 0 0 0 0 0 0 0 0 0 0 0 0" "a tap of F1 turns track 1 Off"
want_j 3 "1 1 5 0 0 0 0 0 0 0 0 0 0 0 0 0" "a second tap brings track 1 back"
want_j 4 "1 3 5 0 0 0 0 0 0 0 0 0 0 0 0 0" "hold F2 + encoder +2 = pattern 3, the tap does not turn it Off"
grep '^padleds' "$W/J.log" | head -1 | grep -q '^padleds 0:[0-9]* 2:[0-9]* 4:[0-9]*$' && ok "pads 1 3 5 lit: the patterns of Part 1 (the cursor cell's pattern 5 bright on the device) ($(grep -m1 '^padleds' "$W/J.log" | cut -c1-60))" || bad "pad LEDs: $(grep -m1 '^padleds' "$W/J.log")"
C2=$(grep -c '^parts count 2 ' "$W/J.log"); C3=$(grep '^parts count 3 ' "$W/J.log" | wc -l | tr -d ' ')
[ "$C2" -ge 1 ] && [ "$C3" -ge 2 ] && ok "Insert pastes after, Shift + Insert before (2 then 3 Parts)" || bad "paste counts: 2:$C2 3:$C3"
grep -A4 '^parts count 3 ' "$W/J.log" | tail -4 | grep -q '^part 1 len 0 rep 1 flags 1 baked 0 src 1 3 0 0 ' && ok "Delete turns the cursor cell (track 3) of the selected Part Off" || bad "Delete: $(grep -A4 '^parts count 3 ' "$W/J.log" | tail -4 | tr '\n' '|')"
grep -q '^parts count 1 ' "$W/J.log" && ok "Shift + Down + Shift + Delete removes the 2 selected Parts" || bad "Shift + Delete: $(grep '^parts count' "$W/J.log" | tail -3 | cut -c1-30 | tr '\n' '|')"
grep -q '^part 0 len 18 rep 1 ' "$W/J.log" && grep -q '^part 0 len 25 rep 1 ' "$W/J.log" && ok "Shift + encoder: one step a click, Auto (16) + 2 = 18 steps, 7 more = 25, repeats stay 1" || bad "length: $(grep '^part 0 len' "$W/J.log" | tail -2 | cut -c1-24 | tr '\n' '|')"

echo "### K display list with 1, 7, 8, 12, 16, 40 Parts"
for N in 1 7 8 12 16 40; do
  SDK="$W/sdK$N"; mkdir -p "$SDK"
  S="$SETUP;$BUILD"; for ((k=1;k<N;k++)); do S="$S;tap Insert"; done
  for ((k=0;k<N;k++)); do for ((t=0;t<16;t++)); do S="$S;partset $k src$t $(( 100 + (k*16+t) % 155 ))"; done; done
  S="$S;tap Up $N;tap Rec;wait 300;dlstat;shot $W/png/K$N-arrange.png;tap Rec;wait 300;dlstat;shot $W/png/K$N-build.png;tap Down 12;wait 200;dlstat;parts"
  run "$BIN" "$SDK" "$W/K$N.log" "$S"
  BAD=$(grep '^dlstat' "$W/K$N.log" | awk '$3 > 2043 || $7 != 0 || $11 < 0 || $13 == 0' | head -1)
  DL=$(grep '^dlstat' "$W/K$N.log" | awk '{print $3}' | sort -n | tail -1)
  [ "$(grep -c '^dlstat' "$W/K$N.log")" = 3 ] && [ -z "$BAD" ] && grep -q "^parts count $N " "$W/K$N.log" && ok "$N Parts: frame at most $DL words of 2,043, no overflow, table drawn" || bad "$N Parts dl: [$BAD] $(grep '^dlstat' "$W/K$N.log" | tr '\n' '|')"
done

echo "### L Performance Insert = snapshot to a Part"
SDL="$W/sdL"; mkdir -p "$SDL"
run "$BIN" "$SDL" "$W/L.log" "$SETUP;tap Performance;wait 500;dlstat;hold F1;enc 1;unhold F1;wait 200;mute 2 1;tap Insert;wait 100;dlstat;shot $W/png/L1-snapshot-popup.png;parts;tap Insert;wait 100;parts;tap Song;wait 300;hold Shift;tap F8;unhold Shift;wait 300;shot $W/png/L2-snapshot-tint.png;parts"
BADL=$(grep '^dlstat' "$W/L.log" | awk '$3 > 2043 || $7 != 0' | head -1); DLL=$(grep '^dlstat' "$W/L.log" | awk '{print $3}' | sort -n | tail -1)
[ "$(grep -c '^dlstat' "$W/L.log")" = 2 ] && [ -z "$BADL" ] && ok "the Performance screen and its snapshot popup: frame at most $DLL words of 2,043, no overflow" || bad "Performance dl: [$BADL] $(grep '^dlstat' "$W/L.log" | tr '\n' '|')"
grep -q '^part 0 len 0 rep 1 flags 1 baked 0 src 2 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0$' "$W/L.log" && ok "Insert in Performance: Part 1 = track 1 pattern 2, track 2 pattern 1, every track with no notes (3 muted, 4-16 empty) Off" || bad "snapshot: $(grep '^part 0' "$W/L.log" | head -1)"
grep -q '^parts count 2 ' "$W/L.log" && grep -q '^part 1 len 0 rep 1 flags 1 baked 0 src 2 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0$' "$W/L.log" && ok "a second Insert appends Part 2 after it" || bad "second snapshot: $(grep '^parts count' "$W/L.log" | tail -1 | cut -c1-40)"

echo "### M Arrange: Go to Start / End, last played"
SDM="$W/sdM"; cp -R "$SD" "$SDM"
run "$BIN" "$SDM" "$W/M.log" 'projopen /Projects PartsTest;tap Song;wait 300;tap F4;wait 100;sel;tap F3;wait 100;sel;tap Down;audio 1;tap F2;wait 2500;tap Play;wait 300;tap F3;wait 100;sel;hold Shift;tap F3;unhold Shift;wait 100;sel;audio 0'
SEL=$(grep '^partsel' "$W/M.log" | awk '{print $2}' | tr '\n' ' ')
[ "$SEL" = "1 0 0 1 " ] && ok "F4 = Part 2 (the last before End), F3 = Part 1, Shift + F3 = the Part that played last" || bad "Start / End: [$SEL] want [1 0 0 1 ]"

echo "### N mode label, the half of the F-row, the 8 active columns"
SDN="$W/sdN"; mkdir -p "$SDN"
PX() { python3 -I "$HERE/parts_px.py" "$@" | tr '\n' ' '; }
run "$BIN" "$SDN" "$W/N.log" "$SETUP;tap Song;wait 300;hold Shift;tap F8;unhold Shift;wait 200;partsui;shot $W/png/N1-arrange-empty.png;tap Rec;wait 100;tap Insert;wait 200;partsui;shot $W/png/N2-build-tracks-1-8.png;tap Right 8;wait 200;partsui;shot $W/png/N3-build-tracks-9-16.png;tap Left;wait 200;partsui;tap Rec;wait 200;partsui;shot $W/png/N4-arrange.png;audio 1;tap F2;wait 400;partsui;tap Play;wait 200;shot $W/png/N5-arrange-playing.png"
NT=$(grep '^partsui' "$W/N.log" | sed 's/partsui title \[\(.*\)\] build \([01]\) cursor \([0-9]*\)/\1|\2|\3/' | tr '\n' ' ')
[ "$NT" = "Parts - ARRANGE|0|0 Parts - BUILD  Part 1|1|0 Parts - BUILD  Part 1|1|8 Parts - BUILD  Part 1|1|7 Parts - ARRANGE  Part 1|0|7 Parts - ARRANGE  Part 1|0|7 " ] && ok "the title bar names the mode all the time: ARRANGE (empty list, Rec off), BUILD with Rec on (cursor on track 9 = F-row 9-16, back on track 8 = F-row 1-8), ARRANGE again, ARRANGE while the song plays" || bad "title bar: [$NT]"
# pixels: the gap right of track 3 (x 127) and right of track 10 (x 331) in row 1, the red rule under the title bar (x 700, y 30)
[ "$(PX "$W/png/N1-arrange-empty.png" 127 64 331 64 700 30)" = "10,10,10 10,10,10 10,10,10 " ] && [ "$(PX "$W/png/N4-arrange.png" 127 64 331 64 700 30)" = "10,10,10 10,10,10 10,10,10 " ] && ok "Arrange: no tint, no red rule" || bad "Arrange pixels: $(PX "$W/png/N4-arrange.png" 127 64 331 64 700 30)"
[ "$(PX "$W/png/N2-build-tracks-1-8.png" 127 64 331 64 700 30)" = "42,20,20 10,10,10 205,24,24 " ] && ok "Build, tracks 1-8: the 8 active columns are tinted (tracks 9-16 are not), red rule under the title bar" || bad "Build 1-8 pixels: $(PX "$W/png/N2-build-tracks-1-8.png" 127 64 331 64 700 30)"
[ "$(PX "$W/png/N3-build-tracks-9-16.png" 127 64 331 64 700 30)" = "10,10,10 42,20,20 205,24,24 " ] && ok "Build, tracks 9-16: the tint moved to the right half" || bad "Build 9-16 pixels: $(PX "$W/png/N3-build-tracks-9-16.png" 127 64 331 64 700 30)"

echo "### O Length and Repeats are separate fields"
SDO="$W/sdO"; mkdir -p "$SDO"
# Build, Part 1 from pattern 1 (Auto x1). Shift + encoder = Length by 1 step, Shift + pad 12-19 = 1-8 bars (pad 11 Auto), Shift + Right / Left = Repeats
OB="$SETUP;tap Song;wait 300;hold Shift;tap F8;unhold Shift;wait 200;tap Rec;wait 100;tap Insert"
SH() { echo "hold Shift;$1;unhold Shift;parts"; }   # one Shift edit, then print the list
run "$BIN" "$SDO" "$W/O.log" "$OB;$(SH 'pad 14');$(SH 'tap Right 4');shot $W/png/O1-3bars-x5.png;$(SH 'enc -1');$(SH 'pad 13');$(SH 'tap Right 70');$(SH 'tap Left 70');$(SH 'tap Right');$(SH 'enc 200');$(SH 'enc -200');$(SH 'enc 1');$(SH 'enc -16');$(SH 'enc -1');$(SH 'pad 12');$(SH 'pad 11');$(SH 'pad 20');hold Shift;pad 15;repeat Right 2;unhold Shift;parts;shot $W/png/O2-4bars-x3.png"
OP=$(grep '^part 0 ' "$W/O.log" | sed 's/^part 0 len \([0-9]*\) rep \([0-9]*\) .*/\1x\2/' | tr '\n' ' ')
# Insert (Auto x1), pad 3 bars, +4 repeats, -1 step, pad 2 bars, +70 repeats, -70 repeats, +1, +200 steps, -200 steps, Auto + 1 step, -16 steps,
# -1 step (Auto again), pad 12 = 1 bar, pad 11 = Auto, pad 20 (no bar pad: nothing), 4 bars by pad and a held Right (2 more presses)
WANT="48x1 48x5 47x5 32x5 32x64 32x0 32x1 128x1 0x1 17x1 1x1 0x1 16x1 0x1 0x1 64x3 "
[ "$OP" = "$WANT" ] && ok "3 bars x 5 = 48 steps x 5 passes, Length steps by 1 (47), the bar pads jump to whole bars (32), a Length step leaves Repeats alone and the other way round, Repeats cap at 64 and go down to End (0), Length caps at 128 steps and floors at Auto (a step from Auto counts from the 16 steps Auto gives: 17), pad 11 is Auto and pad 20 does nothing, a held Right repeats" || bad "fields: got [$OP] want [$WANT]"
# 3 bars x 5 plays 5 passes of 48 steps (the 16 step pattern tiled 3 times): at 300 BPM a step is 50 ms, 15 hits of track 1, 800 ms apart,
# the last pass ends at 12,000 ms and the song stops there
SDO3="$W/sdO3"; mkdir -p "$SDO3"
run "$BIN" "$SDO3" "$W/O3.log" "$OB;$(SH 'pad 14');$(SH 'tap Right 4');tempo 300;tap Rec;wait 200;audio 1;midilog 1;tap F2;wait 13000;parts;midilog 0;audio 0"
passes "$W/O3.log" > "$W/O3.passes"
python3 -I - "$W/O3.passes" "$PAT1" <<'PY' && ok "3 bars x 5 plays 15 hits of the tiled pattern 800 ms apart (48 steps x 5 passes = 240 steps at 300 BPM) and stops" || bad "3 bars x 5 playback"
import sys
rows = [l.split(' ', 1) for l in open(sys.argv[1]).read().splitlines()]
t = [float(r[0]) for r in rows]
d = [b - a for a, b in zip(t, t[1:])]
ok = len(rows) == 15 and all(r[1] == sys.argv[2] for r in rows) and d and max(d) - min(d) < 4 and abs(d[0] - 800) < 6
if not ok:
    print('  %d hits, gaps %s' % (len(rows), sorted(set(round(x) for x in d))))
sys.exit(0 if ok else 1)
PY
grep '^parts count' "$W/O3.log" | tail -1 | grep -q 'playing 0 cur -1 .* play 0 ' && ok "the sequencer stopped on its own after the 5th pass" || bad "O3 end: $(grep '^parts count' "$W/O3.log" | tail -1 | cut -c1-80)"

echo "### P Play Song stops after the last Part, Loop Song, End mid list, Loop Part"
SDP="$W/sdP"; cp -R "$SD" "$SDP"
STOPS() { grep '^parts count' "$1" | tail -1; }
# P1 the F6 key: Loop Song on, on Part 0 only, kept by Insert before Part 0 and Shift + Delete of Part 0, written non-default only
run "$BIN" "$SDP" "$W/P1.log" "projopen /Projects PartsTest;tap Song;wait 300;parts;tap F6;wait 100;parts;projsave /Projects LoopSave;tap Rec;wait 100;hold Shift;tap Insert;unhold Shift;wait 100;parts;hold Shift;tap Delete;unhold Shift;wait 100;parts;tap Rec;wait 100;tap F6;wait 100;parts;projsave /Projects NoLoop"
FL=$(grep '^part ' "$W/P1.log" | sed 's/^part \([0-9]*\) len [0-9]* rep [0-9]* flags \([0-9]*\) .*/\1:\2/' | tr '\n' ' ')
[ "$FL" = "0:1 1:1 2:1 0:3 1:1 2:1 0:3 1:1 2:1 3:1 0:3 1:1 2:1 0:1 1:1 2:1 " ] && ok "F6 sets the loop flag on Part 0 only, Shift + Insert before Part 0 and Shift + Delete of it keep it on the first Part, F6 again clears it" || bad "loop flags: [$FL]"
grep -q '^pat\.part\.1 [0-9a-f]\{32\} 0 1 3 0$' "$SDP/Projects/LoopSave/project.mtff" && grep -q '^pat\.part\.2 [0-9a-f]\{32\} 0 2 1 0$' "$SDP/Projects/LoopSave/project.mtff" && grep -q '^pat\.part\.1 [0-9a-f]\{32\} 0 1 1 0$' "$SDP/Projects/NoLoop/project.mtff" && ok "project.mtff: flags 3 on Part 1 only with Loop Song on, the same flags 1 as before without it (no new key)" || bad "mtff flags: $(grep '^pat.part' "$SDP/Projects/LoopSave/project.mtff" | sed 's/^pat.part.\([0-9]\) [0-9a-f]* /\1 /' | tr '\n' '|')"
run "$BIN" "$SDP" "$W/P2.log" "projopen /Projects LoopSave;parts"
grep -q '^part 0 len 0 rep 1 flags 3 ' "$W/P2.log" && ok "the loop flag is back after a project open" || bad "loop flag after open: $(grep '^part 0' "$W/P2.log")"
# P2 Loop Song on: from Part 1 (x2) the End Part ends the list, then Part 0 again: P1 P1 P0 P1 P1, plays on
run "$BIN" "$SDP" "$W/P3.log" "projopen /Projects LoopSave;tap Song;wait 300;tap Down;audio 1;midilog 1;tap F2;wait 9600;parts;tap Play;wait 300;midilog 0;audio 0;parts"
passes "$W/P3.log" > "$W/P3.passes"
GOT=$(head -5 "$W/P3.passes" | cut -d' ' -f2- | tr '\n' '|')
[ "$GOT" = "$P1|$P1|$P0|$P1|$P1|" ] && grep '^parts count' "$W/P3.log" | head -1 | grep -q 'playing 1 ' && ok "Loop Song on: P1 P1 then back to Part 0 (P0 P1 P1), the song keeps playing" || bad "loop passes: [$GOT] $(grep '^parts count' "$W/P3.log" | head -1 | cut -c1-60)"
# P3 Loop Song off, from Part 1: P1 P1 and stop. From Part 0 with End in the middle (Part 1 End, Part 2 plays pattern 1): P0 and stop
run "$BIN" "$SDP" "$W/P4.log" "projopen /Projects NoLoop;tap Song;wait 300;tap Down;audio 1;midilog 1;tap F2;wait 6500;parts;midilog 0;audio 0"
passes "$W/P4.log" > "$W/P4.passes"
GOT=$(cut -d' ' -f2- "$W/P4.passes" | tr '\n' '|')
[ "$GOT" = "$P1|$P1|" ] && STOPS "$W/P4.log" | grep -q 'playing 0 cur -1 .* play 0 ' && ok "F2 on the last Part with Loop Song off: P1 P1 and the sequencer stops" || bad "last Part: [$GOT] $(STOPS "$W/P4.log" | cut -c1-80)"
run "$BIN" "$SDP" "$W/P5.log" "projopen /Projects NoLoop;partset 1 rep 0;partset 2 rep 1;tap Song;wait 300;audio 1;midilog 1;tap F2;wait 5000;parts;midilog 0;audio 0;midilog 1;audio 1;tap Down 2;tap F2;wait 4000;parts;midilog 0;audio 0"
passes "$W/P5.log" > "$W/P5.passes"
GOT=$(cut -d' ' -f2- "$W/P5.passes" | tr '\n' '|')
[ "$GOT" = "$P0|$PAT1|" ] && ok "End mid list stops the song there (P0, Part 2 behind the End does not play) and F2 on Part 2 plays it alone" || bad "End mid list: [$GOT]"
grep '^parts count' "$W/P5.log" | grep -c 'playing 0 cur -1 .* play 0 ' | grep -q 2 && ok "both runs ended with the sequencer stopped" || bad "after End mid list: $(grep '^parts count' "$W/P5.log" | cut -c1-70 | tr '\n' '|')"
# Loop Song on and an End in the middle: End = the loop point, P0 P0 P0 (the playlist-free stock behaviour of 1.4.0-int)
run "$BIN" "$SDP" "$W/P6.log" "projopen /Projects LoopSave;partset 1 rep 0;partset 2 rep 1;tap Song;wait 300;audio 1;midilog 1;tap F2;wait 5700;parts;midilog 0;audio 0"
passes "$W/P6.log" > "$W/P6.passes"
GOT=$(head -3 "$W/P6.passes" | cut -d' ' -f2- | tr '\n' '|')
[ "$GOT" = "$P0|$P0|$P0|" ] && STOPS "$W/P6.log" | grep -q 'playing 1 ' && ok "Loop Song on with an End Part in the middle: the song loops back to Part 0 at the End (P0 P0 P0 ...)" || bad "loop + End: [$GOT]"
# Loop Part: F1 loops the selected Part, nothing stops it, Loop Song or not
run "$BIN" "$SDP" "$W/P7.log" "projopen /Projects NoLoop;tap Song;wait 300;audio 1;midilog 1;tap F1;wait 7500;parts;tap Play;wait 300;midilog 0;audio 0;parts"
passes "$W/P7.log" > "$W/P7.passes"
GOT=$(head -4 "$W/P7.passes" | cut -d' ' -f2- | tr '\n' '|')
[ "$GOT" = "$P0|$P0|$P0|$P0|" ] && grep '^parts count' "$W/P7.log" | head -1 | grep -q 'playing 1 ' && ok "F1 Loop Part is unchanged: P0 over and over, still playing after 4 passes" || bad "Loop Part: [$GOT]"
true

echo "### Q Song and Song Stems export end at the last Part"
SDQ="$W/sdQ"; cp -R "$SD" "$SDQ"
run "$BIN" "$SDQ" "$W/Q.log" "projopen /Projects NoLoop;tap Song;wait 300;export song;wait 200;export songstems;wait 200;projopen /Projects LoopSave;tap Song;wait 300;export song;wait 200;export songstems;wait 200"
python3 -I - "$SDQ/Export" <<'PY' && ok "exports of the 3 Part song (P0 + P1 x2 = 3 passes of 1,846 ms): Song and every Song Stems file are 5.54-5.90 s, with Loop Song off and on, the looped song is not longer" || bad "export length"
import glob, os, sys, wave
fails, seen = [], 0
dirs = sorted(glob.glob(os.path.join(sys.argv[1], '*', '*')))
if len(dirs) != 4:
    fails.append('export folders: %s' % dirs)
frames = {}
for d in dirs:
    for f in sorted(glob.glob(os.path.join(d, '*.wav'))):
        w = wave.open(f)
        sec = w.getnframes() / w.getframerate()
        seen += 1
        frames.setdefault(os.path.basename(d).split('_')[-1][0], set()).add(w.getnframes())
        if not 5.538 <= sec <= 5.90:
            fails.append('%s %.3f s' % (f[-40:], sec))
if seen < 30:
    fails.append('only %d wav files' % seen)
if any(len(v) != 1 for v in frames.values()):
    fails.append('lengths differ inside a kind: %s' % frames)
for f in fails:
    print('  ' + f)
sys.exit(1 if fails else 0)
PY
true

echo "### R start step of a track in its Part"
# Pattern 4 has 32 steps and track 1 notes 60-67 at steps 0 4 ... 28 (the MIDI out adds 24: 84-91), one note every 461.5 ms at 130 BPM.
# Part 0 = track 1 from pattern 4 (the other tracks hold nothing: Off). Hold F1 + Shift + encoder = the start step of track 1, + Shift + a bar pad = bar N
S4='pattern 4;plen 32;setstep 0 0 60 48 0 0 0 0;setstep 0 4 61 48 0 0 0 0;setstep 0 8 62 48 0 0 0 0;setstep 0 12 63 48 0 0 0 0;setstep 0 16 64 48 0 0 0 0;setstep 0 20 65 48 0 0 0 0;setstep 0 24 66 48 0 0 0 0;setstep 0 28 67 48 0 0 0 0;dirty;wait 300'
RB="tap Song;wait 300;hold Shift;tap F8;unhold Shift;wait 200;tap Rec;wait 100;tap Insert"
ST() { echo "hold F1;hold Shift;$1;unhold Shift;unhold F1;partstart"; }   # a start edit of track 1, then print the starts
SDR="$W/sdR"; mkdir -p "$SDR"
run "$BIN" "$SDR" "$W/R1.log" "$SETUP;$S4;$RB;partstart;$(ST 'enc 16');shot $W/png/R1-start-16.png;$(ST 'enc 200');$(ST 'enc -300');$(ST 'pad 14');$(ST 'pad 11');$(ST 'pad 13');$(ST 'enc -1');$(ST 'enc 17');$(ST 'pad 13');tap Insert;partstart;parts;projsave /Projects OffTest;projopen /Projects OffTest;partstart;parts"
RS=$(grep '^partstart 0 ' "$W/R1.log" | awk '{print $3}' | tr '\n' ' ')
[ "$RS" = "0 16 127 0 32 0 16 15 32 16 16 16 " ] && ok "start of track 1: 0 after Insert, +16 by the encoder, caps at 127, floors at 0, bar pad 14 = bar 3 (32), pad 11 = step 1, pad 13 = bar 2 (16), -1 = 15, +17 = 32, 16 again, the same after the reopen" || bad "starts: [$RS]"
grep '^partstart 1 ' "$W/R1.log" | head -1 | grep -q '^partstart 1 16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0$' && ok "Insert copies the Part with its start steps" || bad "copied starts: $(grep '^partstart 1 ' "$W/R1.log" | head -1)"
grep '^part 0 ' "$W/R1.log" | head -1 | grep -q 'src 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0$' && ok "the start edits did not touch the patterns of the cells" || bad "src: $(grep '^part 0 ' "$W/R1.log" | head -1)"
PS() { grep '^pat\.start' "$1" 2>/dev/null; }
[ "$(PS "$SDR/Projects/OffTest/project.mtff")" = "pat.start.1 10000000000000000000000000000000
pat.start.2 10000000000000000000000000000000" ] && [ "$(PS "$SDR/Projects/OffTest/project.mtff")" = "$(PS "$SDR/Workspace/project.mtff")" ] && ok "project.mtff holds pat.start.1 and pat.start.2 (32 hex digits, track 1 at 0x10), project and Workspace equal" || bad "pat.start lines: $(PS "$SDR/Projects/OffTest/project.mtff" | tr '\n' '|')"
cp -R "$SDR/Projects/OffTest" "$W/OffTest.saved"

# playback. Each case: the notes of the one Part that plays, then t when they sit on the 461.5 ms grid (every 461.5 ms at 130 BPM, within 8 ms: the last word t)
OFFPLAY() { # OFFPLAY NAME PARTSET-COMMANDS WAIT-MS: one run of a copy of the saved project
  local d="$W/sdR_$1"; rm -rf "$d"; cp -R "$SDR" "$d"
  run "$BIN" "$d" "$W/R_$1.log" "projopen /Projects OffTest;$2;tap Song;wait 300;audio 1;midilog 1;tap F2;wait $3;wait 300;midilog 0;audio 0;parts"
  python3 -I - "$W/R_$1.log" <<'PY'
import sys
ons = []
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) >= 8 and p[0] == 'midi' and p[1] == 'on':
        ons.append((int(p[2]), float(p[7])))
t0 = ons[0][1] if ons else 0
dev = max([abs((t - t0) - k * 461.538) for k, (n, t) in enumerate(ons)] or [0])
print(' '.join('%d' % n for n, t in ons), 't' if dev < 8 else 'late%.0f' % dev)
PY
}
# the saved project has 2 Parts (both start 16 on track 1); Part 2 is made End so only Part 1 plays
E='partset 1 rep 0'
GOT=$(OFFPLAY win "$E;partset 0 rep 2" 4300)
[ "$GOT" = "88 89 90 91 88 89 90 91 t" ] && ok "start 16, Auto, x2: the Part is the window of 16 steps (Auto = 32 - 16), notes 88 89 90 91 twice, 1,846 ms a pass" || bad "window: [$GOT]"
GOT=$(OFFPLAY tile "$E;partset 0 len 48;partset 0 start0 24" 5800)
[ "$GOT" = "90 91 90 91 90 91 90 91 90 91 90 91 t" ] && ok "start 24, length 48 (3 bars): the 8 step window 24-31 tiles 6 times (90 91 x 6), not the whole pattern" || bad "tile: [$GOT]"
GOT=$(OFFPLAY notile "$E;partset 0 len 48;partset 0 start0 24;partset 0 flags 0" 5800)
[ "$GOT" = "90 91 t" ] && ok "start 24, length 48, tiling off: the window plays once, the rest of the Part is empty" || bad "no tile: [$GOT]"
GOT=$(OFFPLAY past "$E;partset 0 start0 99" 3800)
[ "$GOT" = "84 85 86 87 88 89 90 91 t" ] && ok "a start past the end of the source (99 of 32) plays the whole source, Auto = 32 steps" || bad "past the end: [$GOT]"
GOT=$(OFFPLAY zero "$E;partset 0 start0 0" 3800)
[ "$GOT" = "84 85 86 87 88 89 90 91 t" ] && ok "start 0 is the whole source as before the field existed (84-91)" || bad "start 0: [$GOT]"
GOT=$(OFFPLAY two "partset 0 rep 1;partset 1 rep 1;partset 1 start0 28;partset 1 len 16" 4400)
[ "$GOT" = "88 89 90 91 91 91 91 91 t" ] && ok "Part 2 starts at 28 (a 4 step window, 1 note) in a 16 step Part: it tiles 4 times; Part 1 (start 16) is not changed by it" || bad "two Parts: [$GOT]"
# list edits move the start rows with their Parts: Shift + Insert before Part 1 (a copy, with its start), Shift + Delete of Part 3, a start of 5
# on Part 2, Shift + Delete of Part 1
SDRC="$W/sdRC"; rm -rf "$SDRC"; cp -R "$SDR" "$SDRC"
run "$BIN" "$SDRC" "$W/R2.log" "projopen /Projects OffTest;partset 1 start0 0;tap Song;wait 300;tap Rec;wait 100;hold Shift;tap Insert;unhold Shift;partstart;tap Down 2;hold Shift;tap Delete;unhold Shift;partstart;partset 1 start0 5;tap Up 2;hold Shift;tap Delete;unhold Shift;partstart;tap Insert;partstart;tap Copy;tap Insert;partstart"
RS=$(grep '^partstart ' "$W/R2.log" | awk '{print $3}' | tr '\n' ' ')
[ "$RS" = "16 16 0 16 16 5 5 5 5 5 5 " ] && ok "the start rows follow their Parts: Shift + Insert copies Part 1 with its start (16 16 0), Shift + Delete of the last Part leaves 16 16, after a start of 5 on Part 2 and Shift + Delete of Part 1 only 5 is left, Insert and Copy + Insert duplicate it with the start" || bad "start rows: [$RS]"
# another pattern in the cell resets the start (a start into the old pattern means nothing there), Off and back keeps it, Delete resets it
SDRD="$W/sdRD"; rm -rf "$SDRD"; cp -R "$SDR" "$SDRD"
run "$BIN" "$SDRD" "$W/R3.log" "projopen /Projects OffTest;tap Song;wait 300;tap Rec;wait 100;hold F1;pad 4;unhold F1;partstart;parts;tap Down;tap F1;partstart;parts;tap F1;partstart;parts;hold F1;enc 1;unhold F1;partstart;parts;partset 1 start0 9;partset 1 src0 4;tap Delete;partstart;parts"
RS=$(grep '^partstart ' "$W/R3.log" | awk '{print $3}' | tr '\n' ' ')
RP=$(grep '^part [01] ' "$W/R3.log" | awk '{print $12}' | tr '\n' ' ')
[ "$RS" = "0 16 0 16 0 16 0 0 0 0 " ] && [ "$RP" = "5 4 5 0 5 4 5 5 5 0 " ] && ok "a pad (pattern 5) resets the start of its cell (0), a tap Off and a tap back keep it (16), the encoder to another pattern resets it, Delete (cell Off) resets it" || bad "resets: starts [$RS] patterns [$RP]"
# the saved lines survive a save by the build without Parts (the lines are unknown to it), and open on 1.4.0-int style (no Parts code)
SD0R="$W/sd0R"; rm -rf "$SD0R"; mkdir -p "$SD0R/Projects"; cp -R "$W/OffTest.saved" "$SD0R/Projects/OffTest"
run "$BIN0" "$SD0R" "$W/R4.log" "projopen /Projects OffTest;projsave /Projects OffTest"
[ -n "$(PS "$W/OffTest.saved/project.mtff")" ] && [ "$(PS "$SD0R/Projects/OffTest/project.mtff")" = "$(PS "$W/OffTest.saved/project.mtff")" ] && ok "the build without Parts keeps the pat.start lines byte for byte over a save" || bad "pat.start lines after a no-Parts save: $(PS "$SD0R/Projects/OffTest/project.mtff" | tr '\n' '|')"
# the display list with a start step in every cell: the numbers give way first, nothing overflows
SDRK="$W/sdRK"; mkdir -p "$SDRK"
S="$SETUP;$BUILD"; for ((k=1;k<16;k++)); do S="$S;tap Insert"; done
for ((k=0;k<16;k++)); do for ((t=0;t<16;t++)); do S="$S;partset $k src$t $(( 100 + (k*16+t) % 155 ));partset $k start$t $(( 1 + (k*9+t*7) % 120 ))"; done; done
run "$BIN" "$SDRK" "$W/RK.log" "$S;tap Up 16;tap Rec;wait 300;dlstat;shot $W/png/RK-arrange.png;tap Rec;wait 300;dlstat;shot $W/png/RK-build.png;tap Down 12;wait 200;dlstat;parts"
BADK=$(grep '^dlstat' "$W/RK.log" | awk '$3 > 2043 || $7 != 0 || $11 < 0 || $13 == 0' | head -1); DLK=$(grep '^dlstat' "$W/RK.log" | awk '{print $3}' | sort -n | tail -1)
[ "$(grep -c '^dlstat' "$W/RK.log")" = 3 ] && [ -z "$BADK" ] && ok "16 Parts, a start step in all 256 cells: frame at most $DLK words of 2,043, no overflow, table drawn" || bad "start dl: [$BADK] $(grep '^dlstat' "$W/RK.log" | tr '\n' '|')"

echo "### S Unlink (Shift + Copy in Build): the cursor track of the Part as a pattern of its own"
SDS="$W/sdS"; rm -rf "$SDS"; mkdir -p "$SDS"
P4H() { (cd "$1/Workspace/patterns" && shasum pattern_04.mtp | cut -c1-12); }
# S1 start 16, Auto (16 steps): the window 16-31 of pattern 4 becomes the first free pattern (5: patterns 1-3 hold notes, 4 is on screen)
UB="$SETUP;$S4;$RB;$(ST 'enc 16');fx34popup clear;hold Shift;tap Copy;unhold Shift;wait 200;fx34popup;parts;partstart;shot $W/png/S1-unlinked.png;pattern 5;step 0 0;step 0 4;step 0 12;step 0 16;step 1 0;pattern 4;step 0 16"
run "$BIN" "$SDS" "$W/S1.log" "$UB;projsave /Projects Unlinked"
grep -q "^fx34popup 'Unlinked' 'Track 1 is pattern 5'" "$W/S1.log" && ok "Shift + Copy: the popup says Track 1 is pattern 5" || bad "unlink popup: $(grep '^fx34popup' "$W/S1.log" | tail -1)"
grep '^part 0 ' "$W/S1.log" | head -1 | grep -q 'src 5 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0$' && grep '^partstart 0 ' "$W/S1.log" | tail -1 | grep -q '^partstart 0 0 ' && ok "the cell points at pattern 5 and starts at step 1 (the start is 0)" || bad "unlink cell: $(grep '^part 0 ' "$W/S1.log" | head -1) $(grep '^partstart 0 ' "$W/S1.log" | tail -1)"
grep -q '^step 0 0 note 64 ' "$W/S1.log" && grep -q '^step 0 4 note 65 ' "$W/S1.log" && grep -q '^step 0 12 note 67 ' "$W/S1.log" && grep -q '^step 0 16 note -1 ' "$W/S1.log" && grep -q '^step 1 0 note -1 ' "$W/S1.log" && ok "pattern 5 holds steps 16-31 of pattern 4 as steps 1-16 (notes 64 65 .. 67), nothing after them and nothing on the other tracks" || bad "pattern 5: $(grep '^step' "$W/S1.log" | tr '\n' '|')"
tail -2 "$W/S1.log" | grep -q '^step 0 16 note 64 ' && ok "pattern 4 is untouched" || bad "pattern 4: $(tail -2 "$W/S1.log")"
# it sounds the same: the Part plays 4 notes (88-91), as it did with the start 16
GOT=$(run "$BIN" "$SDS" "$W/S1p.log" "projopen /Projects Unlinked;tap Song;wait 300;audio 1;midilog 1;tap F2;wait 2300;wait 300;midilog 0;audio 0;parts" ; python3 -I - "$W/S1p.log" <<'PY'
import sys
ons = [(int(l.split()[2]), float(l.split()[7])) for l in open(sys.argv[1]) if l.startswith('midi on')]
t0 = ons[0][1] if ons else 0
dev = max([abs((t - t0) - k * 461.538) for k, (n, t) in enumerate(ons)] or [0])
print(' '.join('%d' % n for n, t in ons), 't' if dev < 8 else 'late%.0f' % dev)
PY
)
[ "$GOT" = "88 89 90 91 t" ] && ok "the unlinked Part plays 88 89 90 91 (pattern 5 length 16 = the window), as with the start 16" || bad "unlinked playback: [$GOT]"
# S2 a tiled window: start 24, length 48 -> a 48 step pattern of the 8 step window (90 91 x 6)
SDS2="$W/sdS2"; rm -rf "$SDS2"; mkdir -p "$SDS2"
run "$BIN" "$SDS2" "$W/S2.log" "$SETUP;$S4;$RB;partset 0 len 48;partset 0 start0 24;hold Shift;tap Copy;unhold Shift;wait 200;parts;pattern 5;step 0 0;step 0 4;step 0 8;step 0 40;step 0 44;step 0 47;projsave /Projects Unlinked2"
grep '^step 0 ' "$W/S2.log" | tr '\n' '|' | grep -q '^step 0 0 note 66 [^|]*|step 0 4 note 67 [^|]*|step 0 8 note 66 [^|]*|step 0 40 note 66 [^|]*|step 0 44 note 67 [^|]*|step 0 47 note -1 ' && ok "start 24, length 48: pattern 5 holds the window tiled to 48 steps (66 67 66 .. 66 67 at step 41 and 45, nothing at 48)" || bad "tiled unlink: $(grep '^step 0 ' "$W/S2.log" | tr '\n' '|')"
GOT=$(run "$BIN" "$SDS2" "$W/S2p.log" "projopen /Projects Unlinked2;tap Song;wait 300;audio 1;midilog 1;tap F2;wait 5800;wait 300;midilog 0;audio 0;parts" ; python3 -I - "$W/S2p.log" <<'PY'
import sys
ons = [(int(l.split()[2]), float(l.split()[7])) for l in open(sys.argv[1]) if l.startswith('midi on')]
t0 = ons[0][1] if ons else 0
dev = max([abs((t - t0) - k * 461.538) for k, (n, t) in enumerate(ons)] or [0])
print(' '.join('%d' % n for n, t in ons), 't' if dev < 8 else 'late%.0f' % dev)
PY
)
[ "$GOT" = "90 91 90 91 90 91 90 91 90 91 90 91 t" ] && ok "the unlinked tiled Part sounds as before (90 91 x 6)" || bad "tiled unlink playback: [$GOT]"
# S3 refusals: nothing free (every pattern number is in a Part), an Off cell, while playing
SDS3="$W/sdS3"; rm -rf "$SDS3"; mkdir -p "$SDS3"
S="$SETUP;$S4;$RB"; for ((k=1;k<16;k++)); do S="$S;tap Insert"; done
for ((k=0;k<16;k++)); do for ((t=0;t<16;t++)); do S="$S;partset $k src$t $(( (k*16+t) % 255 + 1 ))"; done; done
run "$BIN" "$SDS3" "$W/S3.log" "$S;tap Up 16;partstart;fx34popup clear;hold Shift;tap Copy;unhold Shift;wait 200;fx34popup;parts"
H3=$(P4H "$SDS3")
grep -q "^fx34popup 'Unlink failed' 'no free pattern left'" "$W/S3.log" && grep '^part 0 ' "$W/S3.log" | head -1 | grep -q 'src 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16$' && ok "no free pattern: the popup says so, the cell is unchanged" || bad "no free pattern: $(grep '^fx34popup' "$W/S3.log" | tail -1) $(grep '^part 0 ' "$W/S3.log" | head -1)"
SDS3b="$W/sdS3b"; rm -rf "$SDS3b"; mkdir -p "$SDS3b"
run "$BIN" "$SDS3b" "$W/S3b.log" "$SETUP;$S4;$RB;tap Right;fx34popup clear;hold Shift;tap Copy;unhold Shift;wait 200;fx34popup;parts"
grep -q "^fx34popup 'Unlink failed' 'nothing to unlink here'" "$W/S3b.log" && ok "an Off cell: nothing to unlink" || bad "off cell: $(grep '^fx34popup' "$W/S3b.log" | tail -1)"
SDS3c="$W/sdS3c"; rm -rf "$SDS3c"; mkdir -p "$SDS3c"
run "$BIN" "$SDS3c" "$W/S3c.log" "$SETUP;$S4;$RB;tap Rec;wait 100;audio 1;tap F2;wait 500;tap Rec;wait 100;fx34popup clear;hold Shift;tap Copy;unhold Shift;wait 200;fx34popup;parts;tap Rec;tap Play;wait 300;audio 0"
grep -q "^fx34popup 'Stop the playback' 'to unlink a track'" "$W/S3c.log" && grep '^part 0 ' "$W/S3c.log" | tail -1 | grep -q 'src 4 0 ' && ok "while a Part plays: refused, the cell is unchanged" || bad "playing: $(grep '^fx34popup' "$W/S3c.log" | tail -1) $(grep '^part 0 ' "$W/S3c.log" | tail -1)"
echo "### T Length text and the start step in the cell (goldens/parts, byte equal PNGs, UPDATE=1 writes them)"
GOLD="$HERE/goldens/parts"; mkdir -p "$GOLD"
SDT="$W/sdT"; rm -rf "$SDT"; mkdir -p "$SDT"
# Build, Part 1 = track 1 of pattern 4 from step 17, 52 steps (3 bars + 4), the cursor on that cell: the cell reads 4:17, the orange line says
# Track 1: from step 17, the length column says 3b+4
run "$BIN" "$SDT" "$W/T1.log" "$SETUP;$S4;$RB;$(ST 'enc 16');partset 0 len 52;wait 200;parts;dlstat;shot $W/png/T1-cell-start-length.png"
grep -q '^part 0 len 52 ' "$W/T1.log" && grep -q '^dlstat .* overflow 0 ' "$W/T1.log" && ok "Part 1: start 17 on track 1 (the cell reads 4:17), length 52 (3b+4), no display list overflow" || bad "T1: $(grep '^part 0 ' "$W/T1.log" | head -1) $(grep '^dlstat' "$W/T1.log" | head -1)"
# the same Part without a start: the round 1 look of the cell (4), the column says 3b+4 as well
run "$BIN" "$SDT" "$W/T2.log" "$SETUP;$S4;$RB;partset 0 len 52;wait 200;shot $W/png/T2-no-start.png"
for g in T1-cell-start-length T2-no-start; do
  if [ -n "${UPDATE:-}" ]; then cp "$W/png/$g.png" "$GOLD/$g.png"; ok "T $g.png written to goldens/parts"
  elif [ -f "$W/png/$g.png" ] && cmp -s "$W/png/$g.png" "$GOLD/$g.png"; then ok "T $g.png is byte equal to the golden"
  else bad "T $g.png differs from goldens/parts/$g.png (UPDATE=1 rewrites it)"; fi
done
echo "### U live edit with a start step: an edit at Part step s lands at source step start + s"
# OffTest: Part 1 = track 1 of pattern 4 from step 17 (notes 64 65 66 67 at src steps 16 20 24 28 = Part steps 0 4 8 12), x2, Part 2 End.
# The Pattern screen of the playing Part is the windowed image: step 0 is note 64 (src step 16), not 60
SDU="$W/sdU"; rm -rf "$SDU"; cp -R "$SDR" "$SDU"
run "$BIN" "$SDU" "$W/U1.log" "projopen /Projects OffTest;partset 1 rep 0;partset 0 rep 2;tap Song;wait 300;audio 1;midilog 1;tap F2;wait 150;tap Pattern;wait 100;tap Rec;wait 50;step 0 0;step 0 4;setstep 0 4 70 48 0 0 0 0;dirty;wait 100;step 0 4;wait 4300;midilog 0;audio 0;parts;pattern 4;step 0 0;step 0 4;step 0 16;step 0 20;step 0 24;step 0 28"
grep '^step 0 0 ' "$W/U1.log" | head -1 | grep -q 'note 64 ' && grep '^step 0 4 ' "$W/U1.log" | head -1 | grep -q 'note 65 ' && ok "the Pattern screen of a playing Part shows the windowed image (step 1 = note 64 of source step 17, step 5 = 65)" || bad "windowed image: $(grep '^step 0 [04] ' "$W/U1.log" | head -2 | tr '\n' '|')"
python3 -I - "$W/U1.log" <<'PY' && ok "the edit at Part step 5 (note 70) is heard from its next hit: 88 94 90 91 in pass 2 (and in pass 1 when it came in time)" || bad "U1 audio"
import sys
ons = []
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) >= 8 and p[0] == 'midi' and p[1] == 'on':
        ons.append(int(p[2]))
print(' '.join(map(str, ons)))
sys.exit(0 if ons[4:8] == [88, 94, 90, 91] and ons[1] in (89, 94) else 1)
PY
TL=$(tail -6 "$W/U1.log" | grep '^step' | awk '{print $2 ":" $3 "=" $5}' | tr '\n' ' ')
[ "$TL" = "0:0=60 0:4=61 0:16=64 0:20=70 0:24=66 0:28=67 " ] && ok "stored in pattern 4 at source step 21 (note 70), source step 5 is still note 61, the others as they were" || bad "pattern 4 after the live edit: [$TL]"
grep '^part 0 ' "$W/U1.log" | tail -1 | grep -q 'src 4 0 ' && grep -q '^parts count 2 ' "$W/U1.log" && ok "the Part list is unchanged by the edit" || bad "Part after U1: $(grep '^part 0 ' "$W/U1.log" | tail -1)"
# tiled: start 24, 48 steps = the 8 step window (notes 66 67) six times. An edit at Part step 12 (second copy of source step 28) stores at source step 28
SDU2="$W/sdU2"; rm -rf "$SDU2"; cp -R "$SDR" "$SDU2"
run "$BIN" "$SDU2" "$W/U2.log" "projopen /Projects OffTest;partset 1 rep 0;partset 0 len 48;partset 0 start0 24;tap Song;wait 300;audio 1;midilog 1;tap F2;wait 150;tap Pattern;wait 100;tap Rec;wait 50;step 0 0;step 0 4;step 0 12;setstep 0 12 80 48 0 0 0 0;dirty;wait 6000;midilog 0;audio 0;parts;pattern 4;step 0 20;step 0 24;step 0 28"
TL=$(tail -4 "$W/U2.log" | grep '^step' | awk '{print $2 ":" $3 "=" $5}' | tr '\n' ' ')
[ "$TL" = "0:20=65 0:24=66 0:28=80 " ] && ok "tiled window 24-31: an edit at Part step 13 (the 2nd copy of source step 29) is stored once, at source step 29 (note 80), source steps 21 and 25 are untouched" || bad "tiled live edit: [$TL]"
python3 -I - "$W/U2.log" <<'PY' && ok "the tiled edit sounds from its own copy on and on every later copy (90 91 90 80 90 80 ...), the copy before it keeps the old note until the Part is assembled again" || bad "U2 audio"
import sys
ons = []
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) >= 8 and p[0] == 'midi' and p[1] == 'on':
        ons.append(int(p[2]))
print(' '.join(map(str, ons)))
sys.exit(0 if len(ons) >= 12 and ons[-2:] == [90, 104] else 1)
PY
echo "### V Build: arrows reach the Length and Repeats fields, the encoder edits them (ref/parts-arrows.md)"
# Parts view on pattern 4 (32 steps), Build, two Parts (Insert twice), Part 1 selected (Up). Right x15: cursor on track 16, Right x1: the Length
# field (red box at x 535), Right: Repeats (box at x 631). Encoder +3 on Length: Auto 32 -> 35 steps, on Repeats +4: x1 -> x5, Left, encoder -1:
# Length 34. A pad on a field changes no cell. Left to track 16: the encoder selects the Part again (no value changes), Right x2 + encoder +2:
# Part 2 Repeats x3 (the field stays when the Part changes). Saved, reopened: the same values
SDV="$W/sdV"; rm -rf "$SDV"; mkdir -p "$SDV"
V0="$SETUP;$S4;$RB;tap Insert;tap Up;tap Right 15;wait 200;shot $W/png/V0-track16.png"
V1="tap Right;wait 200;shot $W/png/V1-length-field.png;enc 3;wait 100;pad 3;wait 100;parts;tap Right;wait 200;shot $W/png/V2-repeats-field.png;enc 4;wait 100;parts;tap Left;enc -1;wait 100;parts"
V2="tap Left;enc 1;wait 100;parts;partsui;tap Right 2;enc 2;wait 200;shot $W/png/V3-part2-repeats.png;parts;tap Left 3;partsui;projsave /Projects ArrowsTest"
run "$BIN" "$SDV" "$W/V.log" "$V0;$V1;$V2"
VP=$(grep '^part ' "$W/V.log" | cut -d' ' -f1-6 | tr '\n' '|')
[ "$(echo "$VP" | cut -d'|' -f1-2)" = "part 0 len 35 rep 1|part 1 len 0 rep 1" ] && ok "Right x16 reached the Length field, the encoder +3 made Auto 32 -> 35 steps (Part 1 only), a pad did nothing" || bad "V Length: $VP"
[ "$(echo "$VP" | cut -d'|' -f3-4)" = "part 0 len 35 rep 5|part 1 len 0 rep 1" ] && ok "Right again: the Repeats field, encoder +4: x1 -> x5" || bad "V Repeats: $VP"
[ "$(echo "$VP" | cut -d'|' -f5-6)" = "part 0 len 34 rep 5|part 1 len 0 rep 1" ] && ok "Left back to Length, encoder -1: 34 steps, Repeats stay x5" || bad "V back: $VP"
[ "$(echo "$VP" | cut -d'|' -f7-8)" = "part 0 len 34 rep 5|part 1 len 0 rep 1" ] && ok "Left to track 16: the encoder selects the Part again, no value changed" || bad "V cell encoder: $VP"
[ "$(echo "$VP" | cut -d'|' -f9-10)" = "part 0 len 34 rep 5|part 1 len 0 rep 3" ] && ok "Right x2 on Part 2: encoder +2 = Repeats x3" || bad "V part 2: $VP"
grep '^part 0 ' "$W/V.log" | head -1 | grep -q ' src 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0$' && ok "a pad on the field left every cell as it was" || bad "V pad on field: $(grep '^part 0 ' "$W/V.log" | head -1)"
[ "$(grep '^partsui' "$W/V.log" | tail -1 | sed 's/.*cursor //')" = 14 ] && ok "Left x3 from Repeats: Length, track 16, track 15 (leaving the fields works both ways)" || bad "V cursor: $(grep '^partsui' "$W/V.log" | tr '\n' ' ')"
run "$BIN" "$SDV" "$W/V2.log" "projopen /Projects ArrowsTest;parts"
grep '^part ' "$W/V2.log" | cut -d' ' -f1-6 | tr '\n' '|' | grep -q '^part 0 len 34 rep 5|part 1 len 0 rep 3|' && ok "the project reopens with Length 34 x5 and Repeats x3 (the sidecar, no new field)" || bad "V reload: $(grep '^part ' "$W/V2.log" | cut -d' ' -f1-6 | tr '\n' '|')"
# the box: Length field = red at x 535 (not at the Repeats place 631), Repeats field the other way round, track 16 has neither
V1PX=$(PX "$W/png/V1-length-field.png" 535 64 631 64); V2PX=$(PX "$W/png/V2-repeats-field.png" 535 64 631 64); V0PX=$(PX "$W/png/V0-track16.png" 535 64 631 64)
[ "$V1PX" = "205,24,24 10,10,10 " ] && [ "$V2PX" = "10,10,10 205,24,24 " ] && [ "$V0PX" = "10,10,10 10,10,10 " ] && ok "the cursor box is drawn around the Length field, then the Repeats field, and on neither at track 16" || bad "V box pixels: [$V0PX] [$V1PX] [$V2PX]"
echo "### V2 Arrange (Rec off) is unchanged: Left / Right still go to the table and the Tempo field"
SDV3="$W/sdV3"; rm -rf "$SDV3"; mkdir -p "$SDV3"
run "$BIN" "$SDV3" "$W/V3.log" "$SETUP;$S4;$RB;tap Rec;tap Right 20;enc 3;parts"
grep '^part 0 ' "$W/V3.log" | tail -1 | grep -q 'part 0 len 0 rep 1 ' && ok "Arrange: no Length or Repeats edit from the arrows" || bad "V arrange: $(grep '^part 0 ' "$W/V3.log" | tail -1)"
echo "### model errors"
grep -l 'model errors\|signal ' "$W"/*.log && bad "ui_shot model errors or a crash" || ok "no model error, no crash"

[ $fail -eq 0 ] && { echo "RESULT: GREEN"; exit 0; }
echo "RESULT: RED ($fail failed)"; exit 1
