#!/bin/bash
# synth_order_sim.sh [PNG_DIR]
#
# The order and visibility of the Model list on the Synth screen, run on the real UI code of this tree (core/ and synth-open/, the PLUG_HOST
# configuration, built by ui_shot) on the Mac. Host only: nothing here touches a device, every scenario has its own SD card folder.
# Each scenario stages a card (the plugins of $PLG_DIR, an order file or none), runs a ui_shot script of key presses and reads the list the
# control holds back ("poke picker", host_poke.cpp), the file the unit wrote ("cat") and the engine the part stores in its project values.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)". PNGs of the interesting states go to PNG_DIR when it is given.
#
#   synth-open/tools/ui_shot/synth_order_sim.sh [PNG_DIR]
#   CHIP_PLUGIN_SRC  engine_chip.cpp of tracker-engines-os (CHIP is chip.plg since 1.3.0): builds ui_shot with the CHIP host twin, which lists CHIP
#                    at stored 6 and installs it when the list rests on it. Without it the checks that move to CHIP fail
#   PLG_DIR   folder with the .plg and .plgi of the synth plugins (default: the newest of ~/prj/poly/build/cpu-adopt, plugin-synth)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="$ROOT/build/ui-shot${CHIP_PLUGIN_SRC:+-chipplg}"   # CHIP is a plugin engine: its host twin (CHIP_PLUGIN_SRC, as in ui_shot.sh) lists it at stored 6
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/synth_order_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

plg="${PLG_DIR:-}"
if [ -z "$plg" ]; then for d in "$HOME/prj/poly/build/cpu-adopt" "$HOME/prj/poly/build/plugin-synth"; do [ -f "$d/ju6.plg" ] && { plg="$d"; break; }; done; fi
[ -f "$plg/ju6.plg" ] || { echo "synth_order_sim.sh: no plugins (set PLG_DIR to a folder with ju6.plg ...)" >&2; exit 2; }
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
expect() {   # expect "what" "expected" "got"
    if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi
}

# stage DIR [files not to put on the card...]: the synth plugins of $plg (the master inserts and the private modules stay out)
stage() {
    local sd="$1"; shift
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    local f b skip
    for f in "$plg"/*.plg "$plg"/*.plgi; do
        b="$(basename "$f")"
        case "$b" in dirt.*|bbd.*|tape.*|lofi.*|console.*|tube.*|phaser.*|phz.*|pluck.*) continue ;; esac
        skip=0; for x in "$@"; do [ "$b" = "$x.plg" ] || [ "$b" = "$x.plgi" ] && skip=1; done
        [ "$skip" -eq 1 ] || cp "$f" "$sd/Plugins/Synth/"
    done
}

# run SD "script": the Synth screen of part 1 is up at the start of the script (SYNTH). Output: $WORK/out.txt (stdout, picker lines last)
SYNTH='instr 64; tap Params; wait 1500'
run() {
    local sd="$1" script="$2"
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "$script" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] || : >"$WORK/poke.out"
}
picker_line() { grep '^picker' "$WORK/poke.out" | sed -n "${1}p"; }                       # the Nth picker line
list_of() { picker_line "$1" | sed 's/.* list //'; }
cursor_of() { picker_line "$1" | sed 's/.* cursor \([-0-9]*\) list.*/\1/'; }
part_engine() { grep '^part' "$WORK/poke.out" | sed -n "$(( ($1 - 1) * 3 + $2 + 1 ))p" | sed 's/.* engine //'; }   # picker N, part P (0..2)
part_stored() { grep '^part' "$WORK/poke.out" | sed -n "$(( ($1 - 1) * 3 + $2 + 1 ))p" | sed 's/.* stored \([0-9]*\) engine.*/\1/'; }
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 10"; }

P='poke picker'

# ---------------------------------------------------------------------------------------------------------------- no order file
echo "### scenario 1: no order file"
sd="$WORK/s1"; stage "$sd"
run "$sd" "$SYNTH; $P"
DEFAULT_LIST="$(list_of 1)"
echo "     default list: $DEFAULT_LIST (cursor $(cursor_of 1), part 1 plays $(part_engine 1 0))"
case "$DEFAULT_LIST" in ACD,FAT,VAP,WTFM,PERC,CHIP,*JU6*) pass "no file: the built in engines, then the plugins, in the engine list order" ;; *) fail "no file: default order" "$DEFAULT_LIST" ;; esac
NROWS="$(echo "$DEFAULT_LIST" | tr ',' '\n' | wc -l | tr -d ' ')"
FIRST="${DEFAULT_LIST%%,*}"; LAST="${DEFAULT_LIST##*,}"
[ ! -f "$sd/Plugins/synth-order.txt" ] && pass "no file: the unit does not write one on its own" || fail "no file: the unit does not write one on its own"
START="$(part_engine 1 0)"
STORED0="$(part_stored 1 0)"

# ---------------------------------------------------------------------------------------------------------------- wrap around
echo "### scenario 2: arrows go round the ends, the encoder does not"
sd="$WORK/s2"; stage "$sd"
run "$sd" "$SYNTH; tap Up $NROWS; wait 700; $P; tap Down $NROWS; wait 700; $P"
# the part starts on $START at row k. Up through the list and back to the same row after NROWS presses
expect "Up NROWS times: back on the same engine" "$START" "$(part_engine 1 0)"
# start on the first row: go there with Up presses, then one more Up is the last row
POS_START=$(echo "$DEFAULT_LIST" | tr ',' '\n' | grep -n "^$START\$" | head -1 | cut -d: -f1)
run "$sd" "$SYNTH; tap Up $((POS_START - 1)); wait 800; $P; tap Up; wait 800; $P"
expect "up to the first row" "0" "$(cursor_of 1)"
expect "Up on the first row goes to the last" "$((NROWS - 1))" "$(cursor_of 2)"
run "$sd" "$SYNTH; tap Up $((POS_START - 1)); tap Up; wait 800; tap Down; wait 800; $P"
expect "Down on the last row goes to the first" "0" "$(cursor_of 1)"
run "$sd" "$SYNTH; enc -$NROWS; wait 800; enc -3; wait 800; $P"
expect "the encoder stops at the first row" "0" "$(cursor_of 1)"
run "$sd" "$SYNTH; enc $NROWS; wait 800; enc 3; wait 800; $P"
expect "the encoder stops at the last row" "$((NROWS - 1))" "$(cursor_of 1)"

# ---------------------------------------------------------------------------------------------------------------- order and hide from the file
echo "### scenario 3: order and hide"
sd="$WORK/s3"; stage "$sd"
printf '# test\r\nCHIP\r\n-FAT\r\nJU6\r\nACD\r\n-PERC\r\n' >"$sd/Plugins/synth-order.txt"
run "$sd" "$SYNTH; $(shot s3-order); $P"
L3="$(list_of 1)"
case "$L3" in CHIP,JU6,ACD,*) pass "the listed engines come first in the order of the file" ;; *) fail "order of the file" "$L3" ;; esac
case ",$L3," in *,FAT,*) fail "a hidden engine (FAT) is not listed" "$L3" ;; *) pass "a hidden engine (FAT) is not listed" ;; esac
case ",$L3," in *,PERC,*) fail "a hidden engine (PERC) is not listed" "$L3" ;; *) pass "a hidden engine (PERC) is not listed" ;; esac
case "$L3" in CHIP,JU6,ACD,VAP,WTFM,FM4,*) pass "unlisted engines are added at the end in the engine list order" ;; *) fail "unlisted engines at the end" "$L3" ;; esac
expect "hidden engines are removed, nothing else" "$((NROWS - 2))" "$(echo "$L3" | tr ',' '\n' | wc -l | tr -d ' ')"
# the project keeps the engine number: hiding and ordering never change what a part stores
expect "the part keeps its engine (stored number unchanged)" "$STORED0" "$(part_stored 1 0)"

echo "### scenario 4: a hidden engine that the project uses is at the top"
sd="$WORK/s4"; stage "$sd"
printf 'CHIP\r\nJU6\r\nACD\r\n-WTFM\r\n-FAT\r\n' >"$sd/Plugins/synth-order.txt"
run "$sd" "$SYNTH; $(shot s4-used-hidden); $P"
L4="$(list_of 1)"
case "$START" in
    WTFM) case "$L4" in WTFM,CHIP,JU6,ACD,*) pass "part 1 plays hidden WTFM: it is the first row, then the ordered ones" ;; *) fail "hidden engine in use at the top" "$L4" ;; esac
          expect "the cursor is on it" "0" "$(cursor_of 1)" ;;
    *) fail "the start engine is WTFM (scenario assumption)" "$START" ;;
esac
case ",$L4," in *,FAT,*) fail "FAT hidden and not in use: not listed" "$L4" ;; *) pass "FAT hidden and not in use: not listed" ;; esac
# picking another engine leaves the hidden one in the list until the screen is shown again, then it is gone
# CHIP is a plugin engine: a pick of it waits for the rest of the list (kModelRestMs, 1 s) before it installs and is stored
run "$sd" "$SYNTH; tap Down; wait 2500; tap Pattern; wait 300; $SYNTH; $P"
L4b="$(list_of 1)"
case ",$L4b," in *,WTFM,*) fail "after leaving WTFM and coming back it is hidden again" "$L4b" ;; *) pass "after leaving WTFM and coming back it is hidden again" ;; esac
expect "the part now plays the engine it was moved to" "CHIP" "$(part_engine 1 0)"

# a hidden engine that another part plays: the open project uses it, so it is at the top of the list of part 1 as well. Part 2 is moved to
# the next engine, then the file hides that engine (written while the unit runs, as an edit on the card would), and part 1 is opened
sd="$WORK/s4c"; stage "$sd"
S2="instr 65; tap Params; wait 1500"
run "$sd" "$S2; $P; tap Down; wait 800; $P"
P2_ENG="$(part_engine 2 1)"
echo "     part 2 plays $(part_engine 1 1), then $P2_ENG"
run "$sd" "$S2; tap Down; wait 800; put Plugins/synth-order.txt CHIP\\r\\nACD\\r\\n-$P2_ENG\\r\\n; tap Pattern; wait 300; $SYNTH; $P"
L4c="$(list_of 1)"
expect "part 2 plays the engine the test hid" "$P2_ENG" "$(part_engine 1 1)"
expect "an engine hidden but played by part 2 is at the top of the list of part 1" "$P2_ENG" "$(echo "$L4c" | cut -d, -f1)"
expect "and after it come the ordered engines" "CHIP,ACD" "$(echo "$L4c" | cut -d, -f2-3)"

echo "### scenario 5: the file names engines that are not on the card, the card has engines the file does not know"
sd="$WORK/s5"; stage "$sd" fm4
printf 'FM4\r\nNOSUCH\r\n-GONE\r\nJU6\r\nACD\r\n' >"$sd/Plugins/synth-order.txt"
run "$sd" "$SYNTH; $P"
L5="$(list_of 1)"
case "$L5" in JU6,ACD,*) pass "an entry whose .plg is gone is ignored" ;; *) fail "entries of missing plugins" "$L5" ;; esac
case ",$L5," in *,FM4,*|*,NOSUCH,*|*,GONE,*) fail "the engines that are gone are not listed" "$L5" ;; *) pass "the engines that are gone are not listed" ;; esac
# a new .plg that the file does not list: shown, at the end
sd="$WORK/s5b"; stage "$sd"
printf 'JU6\r\nACD\r\n' >"$sd/Plugins/synth-order.txt"
run "$sd" "$SYNTH; $P"
L5b="$(list_of 1)"
expect "an engine the file does not list is shown, after the listed ones" "JU6,ACD" "$(echo "$L5b" | cut -d, -f1-2)"
expect "all the engines of the card are in the list" "$NROWS" "$(echo "$L5b" | tr ',' '\n' | wc -l | tr -d ' ')"

echo "### scenario 6: corrupt files"
sd="$WORK/s6"; stage "$sd"
head -c 3000 /dev/urandom >"$sd/Plugins/synth-order.txt"
run "$sd" "$SYNTH; $P"
expect "a binary file: the default list" "$DEFAULT_LIST" "$(list_of 1)"
: >"$sd/Plugins/synth-order.txt"
run "$sd" "$SYNTH; $P"
expect "an empty file: the default list" "$DEFAULT_LIST" "$(list_of 1)"
printf -- '-ACD\r\n-FAT\r\n-VAP\r\n-WTFM\r\n-PERC\r\n-CHIP\r\n-JU6\r\n' >"$sd/Plugins/synth-order.txt"
run "$sd" "$SYNTH; $P"
case ",$(list_of 1)," in *,WTFM,*) pass "everything hidden that is on the list: the part's engine still shows, the list is not empty" ;; *) fail "all hidden" "$(list_of 1)" ;; esac
{ for i in $(seq 1 3000); do printf 'JUNK%d\r\n' "$i"; done; printf 'CHIP\r\n'; } >"$sd/Plugins/synth-order.txt"
run "$sd" "$SYNTH; $P"
expect "a long file of junk lines (3000): the engines are all there" "$NROWS" "$(list_of 1 | tr ',' '\n' | wc -l | tr -d ' ')"

# ---------------------------------------------------------------------------------------------------------------- edit mode
echo "### scenario 7: the edit mode on the unit"
sd="$WORK/s7"; stage "$sd"
run "$sd" "$SYNTH; hold Shift; tap F1; unhold Shift; wait 800; $(shot s7-edit-mode); $P"
L7="$(list_of 1)"
expect "the edit mode lists every engine" "$DEFAULT_LIST" "$L7"
expect "the cursor starts on the engine of the part" "$(( POS_START - 1 ))" "$(cursor_of 1)"
expect "nothing is picked on entering it" "$START" "$(part_engine 1 0)"
# the cursor to the top, hide the 2nd (FAT), move the 1st below it with Shift + Down, leave
run "$sd" "$SYNTH; hold Shift; tap F1; unhold Shift; wait 500; tap Up $((POS_START - 1)); tap Down; tap Delete; wait 500; $P; tap Up; hold Shift; tap Down; unhold Shift; wait 500; $P; $(shot s7-edited); tap F1; wait 800; $P"
expect "Delete hides the engine under the cursor (edit list)" "ACD,-FAT,$(echo "$DEFAULT_LIST" | cut -d, -f3-)" "$(list_of 1)"
expect "Shift + Down moves the engine down, the cursor goes with it" "-FAT,ACD,$(echo "$DEFAULT_LIST" | cut -d, -f3-)" "$(list_of 2)"
expect "the cursor followed the engine" "1" "$(cursor_of 2)"
expect "leaving the edit mode: the list without the hidden engine, in the new order" "ACD,$(echo "$DEFAULT_LIST" | cut -d, -f3-)" "$(list_of 3)"
expect "leaving the edit mode: nothing was picked meanwhile" "$START" "$(part_engine 3 0)"
expect "leaving the edit mode: the stored number of the part is unchanged" "$STORED0" "$(part_stored 3 0)"
# the file the unit wrote
run "$sd" "wait 10; cat Plugins/synth-order.txt"
echo "     the file after the edit:"; sed 's/^/        /' "$WORK/out.txt" | head -12
grep -q '^-FAT' "$sd/Plugins/synth-order.txt" && pass "the file has -FAT" || fail "the file has -FAT"
expect "the file lists the engines in the new order (first two lines of names)" "FAT ACD" "$(grep -v '^#' "$sd/Plugins/synth-order.txt" | tr -d '\r-' | head -2 | tr '\n' ' ' | sed 's/ $//')"
# the next start reads it
run "$sd" "$SYNTH; $P"
expect "after a restart the file is what the list shows" "ACD,$(echo "$DEFAULT_LIST" | cut -d, -f3-)" "$(list_of 1)"
# Insert shows it again
run "$sd" "$SYNTH; hold Shift; tap F1; unhold Shift; wait 500; tap Up $((POS_START - 1)); wait 300; $P; tap Insert; wait 500; $P; tap F1; wait 600; $P"
expect "edit list shows FAT with its - mark" "-FAT,ACD,$(echo "$DEFAULT_LIST" | cut -d, -f3-)" "$(list_of 1)"
expect "Insert shows the engine again" "FAT,ACD,$(echo "$DEFAULT_LIST" | cut -d, -f3-)" "$(list_of 2)"
expect "and the normal list has it back (at the order's place)" "FAT,ACD,$(echo "$DEFAULT_LIST" | cut -d, -f3-)" "$(list_of 3)"

echo "### scenario 8: other buttons leave the edit mode, pad keys keep working"
sd="$WORK/s8"; stage "$sd"
run "$sd" "$SYNTH; hold Shift; tap F1; unhold Shift; wait 400; tap Right; wait 400; $P; tap Down; wait 400; $P"
case "$(list_of 1)" in *-*) fail "Right leaves the edit mode" "$(list_of 1)" ;; *) pass "Right leaves the edit mode (normal rows, no - marks)" ;; esac
expect "after Right the Down key is on another column, the engine is unchanged" "$START" "$(part_engine 2 0)"

echo "### scenario 9: reorder, the engine picked in the new order is stored by its own number"
sd="$WORK/s9"; stage "$sd"
printf 'CHIP\r\nPERC\r\nACD\r\n' >"$sd/Plugins/synth-order.txt"
sdd="$WORK/s9d"; stage "$sdd"
# row 0 of the ordered list is CHIP (a plugin engine since 1.3.0: the pick waits for the rest), row 1 PERC, row 2 ACD. In the default order they are rows 5, 4 and 0
run "$sd" "$SYNTH; enc -100; wait 300; $P; wait 300; tap Down 0; $P"
expect "ordered list, first row" "CHIP" "$(list_of 1 | cut -d, -f1)"
for pair in "0 5 CHIP" "1 4 PERC" "2 0 ACD"; do
    set -- $pair
    run "$sd" "$SYNTH; enc -100; wait 400; tap Down $1; wait 2500; $P"
    ordered_eng="$(part_engine 1 0)"; ordered_st="$(part_stored 1 0)"
    run "$sdd" "$SYNTH; enc -100; wait 400; tap Down $2; wait 2500; $P"
    expect "row $1 of the ordered list and row $2 of the default list are $3" "$3 $3" "$ordered_eng $(part_engine 1 0)"
    expect "$3 is stored by the same number with and without an order file" "$(part_stored 1 0)" "$ordered_st"
done
run "$sd" "$SYNTH; enc -100; wait 400; tap Down 0; wait 2500; $(shot s9-ordered); $P"

echo "### scenario 10: no /Plugins folder on the card (no plugin engines)"
sd="$WORK/s10"; stage "$sd"; rm -rf "$sd/Plugins"
run "$sd" "$SYNTH; $P; tap Up; wait 500; $P; hold Shift; tap F1; unhold Shift; wait 400; tap Delete; wait 500; $P; $(shot s10-no-plugins-folder)"
expect "the built in engines are listed" "ACD,FAT,VAP,WTFM,PERC" "$(list_of 1)"
expect "an arrow wraps in a short list too" "2" "$(cursor_of 2)"
expect "the edit mode still works and hides in memory (WTFM at the cursor is the part's engine)" "5" "$(echo "$(list_of 3)" | tr ',' '\n' | wc -l | tr -d ' ')"
# (the host card creates the folder on the first write, the unit's FAT does not: there the save says "Order not saved")

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks checks)"; exit 1
