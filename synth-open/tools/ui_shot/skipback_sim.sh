#!/usr/bin/env bash
# skipback_sim.sh UI_SHOT OUTDIR
# Skip-back (Config > Features > Skip-back) on the Mac, EXTHOOKS=1 ui_shot: the switch on the page, a synth part playing through the real
# audio callback, Shift + Sample Rec, and the saved WAV compared sample for sample with what the callback rendered up to the press
# (UISHOT_AUDIO_DUMP + audiomark, skipback_check.py). Also: off = stock (the shortcut opens the Sample Recorder, nothing on the card),
# Stereo 2 s through the page, the number goes on across a start, Sample Rec alone still opens its page.
# Exit 0 when every case passes, the last line is "RESULT: GREEN" or "RESULT: RED (n failing)".
set -uo pipefail
U=$1; O=$2; mkdir -p "$O"; bad=0
HERE=$(cd "$(dirname "$0")" && pwd)
CHK="python3 -I $HERE/skipback_check.py"
NAV='tap Config;wait 800;tap Down 4;wait 300;tap F8;wait 500'   # Config, Features, into the list
PAGE="$NAV;tap Down 4;wait 300;tap F8;wait 500"                 # the Skip-back page, row 4 of the list (Restart mode, Developer mode, Demo start and Capture are hidden without /.develop)
ON="$PAGE;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600"    # State On (cursor stays on State)
PLAY='tap Pattern;wait 500;instr 64;audio 1;midion 60 100 10;wait 3000;midioff 60;midion 67 100 10;wait 4000'
SAVE='hold Shift;skipback;audiomark;tap SampleRec;unhold Shift'
run() {  # name sd script: OUT/name.log, the dump OUT/name.raw, popups OUT/name.poke.out
  rm -f "$O/$1.raw" "$O/$1.poke.out"
  UISHOT_AUDIO_DUMP=$O/$1.raw UISHOT_POKE=$O/$1.poke timeout 300 "$U" "$2" "$3" > "$O/$1.log" 2>&1; echo "exit $?" >> "$O/$1.log"
}
ok() { echo "PASS $1: $2"; }
no() { echo "FAIL $1: $2"; bad=$((bad+1)); }
mark() { sed -n 's/^audio frames //p' "$O/$1.log" | sed -n "${2:-1}p"; }
has() { grep -qE "$2" "$O/$1"; }

# A off (the default): Shift + Sample Rec is the stock key (the Sample Recorder opens), the ring and the card stay untouched
rm -rf "$O/a"; mkdir -p "$O/a"
run a "$O/a" "wait 300;$PLAY;$SAVE;wait 1500;status;skipback;features"
if has a.log "^exit 0" && has a.log "^pattern 0 " && has a.log "skipback on 0 saving 0 channels 1 filled 0 " &&
   has a.log "feature skipback on 0 listed 1 value Off" && [ ! -e "$O/a/Skipback" ] && [ ! -e "$O/a/Features" ]; then
  ok A "off: Shift + Sample Rec left the pattern (stock), nothing captured, no /Skipback, no /Features"
else no A "$(grep -E 'pattern|skipback|exit' "$O/a.log" | tr '\n' ' ')"; fi

# B on with the defaults (Mono, 6 s): the whole ring (5.94 s) of the press, popups, the pattern stays, a second save gets Skip_002
rm -rf "$O/b"; mkdir -p "$O/b"
run b "$O/b" "wait 300;$ON;shot $O/b-page.png;$PLAY;$SAVE;poke popuplines;status;wait 60;shot $O/b-saving.png;wait 1500;poke popuplines;skipback;shot $O/b-saved.png;wait 2000;$SAVE;wait 1500;skipback;midioff 67;wait 2000"
r=0; has b.log "^exit 0" || r=1
has b.log "^pattern 1 " || { echo "    the shortcut left the pattern screen"; r=1; }
has b.poke.out "^popuplines 'Skip-back' 'Saving Skip_001.wav'" || { echo "    no saving popup"; r=1; }
has b.poke.out "^popuplines 'Skip-back saved' '/Skipback/Skip_001.wav'" || { echo "    no saved popup"; r=1; }
$CHK "$O/b/Skipback/Skip_001.wav" "$O/b.raw" "$(mark b 1)" --min-rms 50 | sed 's/^/    /' | grep -q "PASS" || r=1
$CHK "$O/b/Skipback/Skip_001.wav" "$O/b.raw" "$(mark b 1)" --min-rms 50 | sed 's/^/    /'
python3 -c "import sys; sys.exit(0 if (open('$O/b/Skipback/Skip_001.wav','rb').seek(0,2)) == 44 + 262144 * 2 else 1)" || { echo "    Skip_001 is not the whole mono ring"; r=1; }
# the second save: what was captured since the first one ended (filled before the press), exact
f2=$(sed -n 's/^skipback on 1 saving 0 channels 1 filled \([0-9]*\) .*/\1/p' "$O/b.log" | sed -n 3p)
$CHK "$O/b/Skipback/Skip_002.wav" "$O/b.raw" "$(mark b 2)" --min-rms 50 | sed 's/^/    /'
$CHK "$O/b/Skipback/Skip_002.wav" "$O/b.raw" "$(mark b 2)" --min-rms 50 | grep -q PASS || r=1
python3 -c "import sys; sys.exit(0 if open('$O/b/Skipback/Skip_002.wav','rb').seek(0,2) == 44 + ${f2:-0} * 2 and ${f2:-0} > 0 else 1)" || { echo "    Skip_002 is not the $f2 frames captured since the first save"; r=1; }
c=$(tr '\n' ' ' < "$O/b/Features/skipback.cfg" 2>/dev/null)
[ "$c" = "feature 1 id skipback version 1 on 1 chan 1 secs 5 " ] || { echo "    skipback.cfg: $c"; r=1; }
[ $r = 0 ] && ok B "Mono 6 s: Skip_001 = 262,144 frames before the press, Skip_002 = the $f2 frames since, popups, pattern kept, cfg written" || no B "see above"

# C Stereo, 2 s through the page: 88,200 stereo frames before the press, as rendered
rm -rf "$O/c"; mkdir -p "$O/c"
run c "$O/c" "wait 300;$ON;tap Down 1;wait 300;tap F8;wait 300;tap Up;wait 200;tap F8;wait 600;tap Down 1;wait 300;tap F8;wait 300;tap Up;wait 200;tap Up;wait 200;tap Up;wait 200;tap Up;wait 200;tap F8;wait 600;shot $O/c-page.png;$PLAY;$SAVE;wait 1500;skipback;midioff 67;wait 2000"
r=0; has c.log "^exit 0" || r=1
c=$(tr '\n' ' ' < "$O/c/Features/skipback.cfg" 2>/dev/null)
[ "$c" = "feature 1 id skipback version 1 on 1 chan 0 secs 1 " ] || { echo "    skipback.cfg: $c"; r=1; }
has c.log "saved 88200 " || { echo "    not 88,200 frames saved"; r=1; }
$CHK "$O/c/Skipback/Skip_001.wav" "$O/c.raw" "$(mark c)" --min-rms 50 | sed 's/^/    /'
$CHK "$O/c/Skipback/Skip_001.wav" "$O/c.raw" "$(mark c)" --min-rms 50 | grep -q "PASS.*2 ch, 88200 frames" || r=1
[ $r = 0 ] && ok C "Stereo 2 s from the page: 88,200 stereo frames, sample exact" || no C "see above"

# D the next start on B's card: on from the file, the numbers go on (Skip_003), Sample Rec alone still opens the Sample Recorder
run d "$O/b" "wait 300;features;$PLAY;$SAVE;wait 1500;skipback;status;tap SampleRec;wait 500;status"
r=0; has d.log "^exit 0" || r=1
has d.log "feature skipback on 1 listed 1 value On" || { echo "    not on after the start"; r=1; }
$CHK "$O/b/Skipback/Skip_003.wav" "$O/d.raw" "$(mark d)" --min-rms 50 | grep -q PASS || { echo "    Skip_003 missing or wrong"; r=1; }
[ "$(grep -c '^pattern ' "$O/d.log")" = 2 ] && [ "$(grep '^pattern ' "$O/d.log" | head -1 | cut -c1-9)" = "pattern 1" ] &&
  [ "$(grep '^pattern ' "$O/d.log" | tail -1 | cut -c1-9)" = "pattern 0" ] || { echo "    $(grep '^pattern' "$O/d.log" | tr '\n' ' ')"; r=1; }
[ $r = 0 ] && ok D "on across a start, Skip_003 exact, Sample Rec without Shift opens its page" || no D "see above"

[ $bad = 0 ] && { echo "RESULT: GREEN"; exit 0; }
echo "RESULT: RED ($bad failing)"; exit 1
