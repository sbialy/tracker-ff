#!/bin/bash
# tuning_label_sim.sh
#
# Project Settings (File F7): the value column of the Scale snap, Stereo pairs and Tuning rows (projectPageRowValue, configMenu_projpage.inc). The three rows
# used to format their text into ONE static buffer while cMenuGroup::reloadValues keeps a pointer per row, so the row drawn last (Tuning) overwrote the text of
# the rows above it: with a Scala file picked, "Stereo pairs" read "just-major-5lim" (found on 2.0.0 beta 1). Only the label was wrong, the pair links and the
# tuning are data of their own. Driven through the real UI and read with the "projrows" script command (every value pointer taken first, as the list draws).
#   TL1  pairs 1+2 and 3+4 linked, 12 EDO: Stereo pairs "2 linked", Tuning "12 EDO"
#   TL2  a Scala file picked through the Scale file row: Stereo pairs still "2 linked", Tuning the file, the pair mask 3 and the file loaded
#   TL3  save, then a new process (a power up reading project.mtff): same rows, mask 3, file 1
#   TL4  a pair changed through the Stereo pairs rows: the Scala file stays loaded, the label of the pairs follows the links
#   TL5  Scale snap on 3 tracks with 19 notes per period: Scale snap "3 tracks", Tuning "19 EDO" (also on Tracker+ and Mini, no pairs row there)
# Host only, ui_shot (build/ui-shot, or BIN: on the Tracker+ and Mini builds through SCRIPT=tuning_label_sim.sh microtune_scl_imx_sim.sh PLUS|MINI). Ends with "RESULT: GREEN" (exit 0).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
EVID="${1:-}"
PLAT="${PLAT:-og}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/tuning_label_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN:-}" ]; then
    BIN="${UI_SHOT_BIN:-$ROOT/build/ui-shot${SAN:+-san}/ui_shot}"
    [ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "tuning_label_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
[ -n "$EVID" ] && mkdir -p "$EVID"
fails=0
check() { if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
SD="$WORK/sd"; mkdir -p "$SD/Tunings" "$SD/Plugins/Master" "$SD/Plugins/Synth"; cp "$ROOT/releases/tunings/just-major-5limit.scl" "$SD/Tunings/"
if [ "$PLAT" = og ]; then PAIRS=1; else PAIRS=0; fi   # Tracker+ and Mini have no Stereo pairs row
GO="wait 500; tap File; wait 800; tap F7; wait 800;"
# open the page and enter the Tuning group's Scale file row (row 12), pick the first file: as microtune_scl_sim.sh
E1="enc 1"; [ "${LISTKEYS:-enc}" = tap ] && E1="tap Down"   # the Mini has no encoder in its lists
PICK="tap Down 20; tap Up; tap F8; wait 300; tap Up 20; tap Down 12; tap F8; wait 200; $E1; tap F8; wait 300; tap F7; wait 300;"
row() { grep '^projrows' | tail -1 | grep -o "\[$1=[^]]*\]" | sed "s/^\[$1=//; s/\]$//"; }
PS_FIELD='Stereo pairs'
shotp() { [ -n "$EVID" ] && echo "shot $EVID/$1.png;"; }
rm -f "$WORK/poke"

# TL1
if [ $PAIRS = 1 ]; then
    out=$(UISHOT_POKE="$WORK/poke" "$BIN" "$SD" "wait 300; pair 3; $GO projrows" 2>/dev/null)
    a=$(echo "$out" | row "$PS_FIELD"); b=$(echo "$out" | row Tuning)
    [ "$a" = "2 linked" ] && [ "$b" = "12 EDO" ] && check "TL1 pairs 3, 12 EDO: Stereo pairs '2 linked', Tuning '12 EDO'" ok || check "TL1" "Stereo pairs '$a' Tuning '$b'"
fi
# TL2, TL3: pick through the UI, save, reload
out=$(UISHOT_POKE="$WORK/poke" "$BIN" "$SD" "wait 300; ${PAIRS:+pair 3;} $GO $PICK $(shotp tl2-after-pick) projrows; tune scale; poke projsave" 2>/dev/null)
t=$(echo "$out" | row Tuning); f=$(echo "$out" | grep '^tune scale file' | tail -1); m=$(echo "$out" | grep '^projrows' | sed 's/^projrows [0-9]* pairs \([0-9]*\):.*/\1/')
[ "$t" = "just-major-5lim" ] && check "TL2 the file is the Tuning row text" ok || check "TL2 Tuning row" "'$t'"
[[ "$f" == "tune scale file 1 name just-major-5limit.scl n 7 "* ]] && check "TL2 the Scala file is loaded" ok || check "TL2 file" "$f"
if [ $PAIRS = 1 ]; then
    a=$(echo "$out" | row "$PS_FIELD")
    [ "$a" = "2 linked" ] && check "TL2 Stereo pairs still '2 linked' after the pick" ok || check "TL2 Stereo pairs row" "'$a'"
    [ "$m" = 3 ] && check "TL2 the pair mask is unchanged by the tuning pick (3)" ok || check "TL2 mask" "$m"
fi
out=$("$BIN" "$SD" "$GO $(shotp tl3-after-reload) projrows; tune scale" 2>/dev/null)
t=$(echo "$out" | row Tuning); f=$(echo "$out" | grep '^tune scale file' | tail -1); m=$(echo "$out" | grep '^projrows' | sed 's/^projrows [0-9]* pairs \([0-9]*\):.*/\1/')
[ "$t" = "just-major-5lim" ] && check "TL3 after a reload the Tuning row is the file" ok || check "TL3 Tuning row" "'$t'"
[[ "$f" == "tune scale file 1 name just-major-5limit.scl n 7 "* ]] && check "TL3 the Scala file is loaded after the reload" ok || check "TL3 file" "$f"
if [ $PAIRS = 1 ]; then
    a=$(echo "$out" | row "$PS_FIELD")
    [ "$a" = "2 linked" ] && check "TL3 after a reload Stereo pairs reads '2 linked'" ok || check "TL3 Stereo pairs row" "'$a'"
    [ "$m" = 3 ] && check "TL3 the pair mask survives the reload (3)" ok || check "TL3 mask" "$m"
    # TL4: a pair changed through the Stereo pairs group (row 15 of the page, after the nine stock rows moved there, the rows 1+2 .. 7+8): Tracks 1+2 back to Mono
    out=$("$BIN" "$SD" "$GO tap Down 15; tap F8; wait 300; tap F8; wait 200; enc -1; tap F8; wait 300; tap F7; wait 300; projrows; tune scale" 2>/dev/null)
    a=$(echo "$out" | row "$PS_FIELD"); m=$(echo "$out" | grep '^projrows' | sed 's/^projrows [0-9]* pairs \([0-9]*\):.*/\1/'); f=$(echo "$out" | grep '^tune scale file' | tail -1)
    [ "$m" = 2 ] && [ "$a" = "1 linked" ] && check "TL4 a pair changed through its row: mask 2, label '1 linked'" ok || check "TL4 pair change" "mask '$m' label '$a'"
    [[ "$f" == "tune scale file 1 name just-major-5limit.scl n 7 "* ]] && check "TL4 the Scala file is unchanged by a pair change" ok || check "TL4 file" "$f"
fi
# TL5: Scale snap on 3 tracks, 19 EDO
SD2="$WORK/sd2"; mkdir -p "$SD2/Plugins/Master" "$SD2/Plugins/Synth"   # a card of its own: the project of the first one holds the Scala file
out=$("$BIN" "$SD2" "wait 300; tune 12; snap 0 1; snap 1 1; snap 2 1; tune 19; $GO projrows" 2>/dev/null)
s=$(echo "$out" | row "Scale snap"); t=$(echo "$out" | row Tuning)
[ "$s" = "3 tracks" ] && [ "$t" = "19 EDO" ] && check "TL5 Scale snap '3 tracks' next to Tuning '19 EDO'" ok || check "TL5" "Scale snap '$s' Tuning '$t'"
if [ "$fails" = 0 ]; then echo "RESULT: GREEN"; exit 0; else echo "RESULT: RED ($fails)"; exit 1; fi
