#!/bin/bash
# model_rest_sim.sh [PNG_DIR]
#
# When the "Init patch loaded" message of the Synth Model list appears, run on the real UI code of this tree (ui_shot, the PLUG_HOST
# configuration with the CHIP host twin) on the Mac. Host only: no device, every scenario has its own SD card folder.
# The message belongs to the engine change. Any engine, built in, attached or a plugin that is not installed, is picked after the list
# rests on it for kModelRestMs (1 s) and not before. Scrolling past engines with short rests picks and installs nothing and shows no message. Reads the popup on screen ("poke popup", host_poke.cpp) and the engine the part stores ("poke picker").
# Every step of the list (arrow, held key repeat, encoder) restarts the rest: scenarios 5 to 10 step more often than kModelRestMs, count the
# engine changes of part 0 ("poke loads") and time the one load against the last step.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
#
#   synth-open/tools/ui_shot/model_rest_sim.sh [PNG_DIR]
#   CHIP_PLUGIN_SRC  engine_chip.cpp of tracker-engines-os (default ~/prj/poly-engines-os/src/synth-open/engine_chip.cpp): the CHIP twin is
#                    the one plugin engine that installs on the host, JU6 and the others stay "not installed" (a pick of them only moves)
#   CHIP_PLG_DIR     folder with chip.plg and chip.plgi (default ~/prj/poly/build/chip-plugin)
#   PLG_DIR          folder with the other synth .plg (default the newest of ~/prj/poly/build/cpu-adopt, plugin-synth)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
export CHIP_PLUGIN_SRC="${CHIP_PLUGIN_SRC:-$HOME/prj/poly-engines-os/src/synth-open/engine_chip.cpp}"
CHIP_PLG_DIR="${CHIP_PLG_DIR:-$HOME/prj/poly/build/chip-plugin}"
BUILD="$ROOT/build/ui-shot-chipplg"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/model_rest_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

plg="${PLG_DIR:-}"
if [ -z "$plg" ]; then for d in "$HOME/prj/poly/build/cpu-adopt" "$HOME/prj/poly/build/plugin-synth"; do [ -f "$d/ju6.plg" ] && { plg="$d"; break; }; done; fi
[ -f "$plg/ju6.plg" ] || { echo "model_rest_sim.sh: no plugins (set PLG_DIR to a folder with ju6.plg ...)" >&2; exit 2; }
[ -f "$CHIP_PLG_DIR/chip.plg" ] || { echo "model_rest_sim.sh: no chip.plg (set CHIP_PLG_DIR)" >&2; exit 2; }
[ -f "$CHIP_PLUGIN_SRC" ] || { echo "model_rest_sim.sh: no CHIP_PLUGIN_SRC $CHIP_PLUGIN_SRC" >&2; exit 2; }
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
expect() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }

stage() {
    local sd="$1" f b
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    for f in "$plg"/*.plg "$plg"/*.plgi "$CHIP_PLG_DIR"/chip.plg "$CHIP_PLG_DIR"/chip.plgi; do
        b="$(basename "$f")"
        case "$b" in dirt.*|bbd.*|tape.*|lofi.*|console.*|tube.*|phaser.*|phz.*|pluck.*) continue ;; esac
        cp "$f" "$sd/Plugins/Synth/"
    done
}

SYNTH='instr 64; tap Params; wait 1500'   # the Synth screen of part 1, which plays WTFM. Rows: ACD FAT VAP WTFM PERC CHIP JU6 FM4 ...
# CHECK = what the screen and the part say now. Output lines, in order: "popup ..." then the three "part" lines of the picker
CHECK='poke popup; poke picker'
run() {
    local sd="$1" script="$2"
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "$script" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] || : >"$WORK/poke.out"
    # one "popup=<text|none> engine=<name> cursor=<row>" line per CHECK, in script order
    awk '/^popup/ { p=$0; sub(/^popup /,"",p); gsub(/\x27/,"",p) }
         /^picker/ { c=$5 }
         /^part 0 / { print "popup=" p " engine=" $NF " cursor=" c }' "$WORK/poke.out" >"$WORK/obs.txt"
    # the same with the load counter ("poke loads" before "poke popup"): "<obs> loads=<n> last=<ms after the mark, -1 for none>"
    awk '/^loads/ { n=$2; t=$4 }
         /^popup/ { p=$0; sub(/^popup /,"",p); gsub(/\x27/,"",p) }
         /^picker/ { c=$5 }
         /^part 0 / { print "popup=" p " engine=" $NF " cursor=" c " loads=" n " last=" t }' "$WORK/poke.out" >"$WORK/obsl.txt"
}
obs() { sed -n "${1}p" "$WORK/obs.txt"; }   # the Nth observation
obsl() { sed -n "${1}p" "$WORK/obsl.txt"; } # the Nth observation with the load counter (CHECKL)
CHECKL="poke loads; $CHECK"
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 10"; }
MSG='Init patch loaded'

echo "### scenario 1: the message of an engine change does not stay up while the list moves on"
sd="$WORK/s1"; stage "$sd"
run "$sd" "$SYNTH; tap Down; wait 100; $CHECK; wait 1000; $CHECK; tap Down; $CHECK; wait 300; $CHECK; wait 300; $CHECK; wait 700; $CHECK; $(shot s1-installed)"
expect "WTFM -> PERC (built in): the list moves, the part waits on WTFM, no message yet" "popup=none engine=WTFM cursor=4" "$(obs 1)"
expect "after the 1 s rest: PERC is picked with its message" "popup=$MSG engine=PERC cursor=4" "$(obs 2)"
expect "-> CHIP (not installed): the list moves, the part stays on PERC, no message (the one of PERC is gone)" "popup=none engine=PERC cursor=5" "$(obs 3)"
expect "300 ms later: still nothing" "popup=none engine=PERC cursor=5" "$(obs 4)"
expect "600 ms of rest: still nothing, the part is on PERC" "popup=none engine=PERC cursor=5" "$(obs 5)"
expect "after the 1 s rest: CHIP is installed and picked, now the message shows" "popup=$MSG engine=CHIP cursor=5" "$(obs 6)"

echo "### scenario 2: skipping through engines with short rests installs nothing and shows no message"
sd="$WORK/s2"; stage "$sd"
run "$sd" "$SYNTH; tap Down; wait 3300; $CHECK; tap Down; wait 400; $CHECK; tap Down; wait 400; $CHECK; tap Down; wait 400; $CHECK; tap Up; wait 400; $CHECK; tap Up; wait 400; $CHECK; wait 2000; $CHECK"
expect "PERC, picked after its rest, message expired" "popup=none engine=PERC cursor=4" "$(obs 1)"
expect "Down onto CHIP, 400 ms" "popup=none engine=PERC cursor=5" "$(obs 2)"
expect "Down onto JU6, 400 ms" "popup=none engine=PERC cursor=6" "$(obs 3)"
expect "Down onto FM4, 400 ms" "popup=none engine=PERC cursor=7" "$(obs 4)"
expect "Up onto JU6, 400 ms" "popup=none engine=PERC cursor=6" "$(obs 5)"
expect "Up onto CHIP, 400 ms" "popup=none engine=PERC cursor=5" "$(obs 6)"
expect "the rest on CHIP ends: installed, message" "popup=$MSG engine=CHIP cursor=5" "$(obs 7)"

echo "### scenario 3: back on the current engine before the rest ends: nothing is installed"
sd="$WORK/s3"; stage "$sd"
run "$sd" "$SYNTH; tap Down; wait 3300; tap Down; wait 500; $CHECK; tap Up; wait 200; $CHECK; wait 2500; $CHECK"
expect "on CHIP, 500 ms" "popup=none engine=PERC cursor=5" "$(obs 1)"
expect "back on PERC: no message, no pending pick" "popup=none engine=PERC cursor=4" "$(obs 2)"
expect "2.5 s later: the part is still on PERC, nothing was installed" "popup=none engine=PERC cursor=4" "$(obs 3)"

echo "### scenario 4: every engine waits the rest of 1 s, one that is built in or attached too"
sd="$WORK/s4"; stage "$sd"
run "$sd" "$SYNTH; tap Down 2; wait 700; $CHECK; wait 500; $CHECK; wait 2600; tap Up; wait 100; $CHECK; wait 1000; $CHECK; wait 2600; tap Down; wait 100; $CHECK; wait 1000; $CHECK"
expect "Down twice (PERC, then CHIP): nothing is picked, the part stays on WTFM" "popup=none engine=WTFM cursor=5" "$(obs 1)"
expect "1.2 s: CHIP installed and picked, message" "popup=$MSG engine=CHIP cursor=5" "$(obs 2)"
expect "Up onto PERC (built in): the list moves, the part waits on CHIP" "popup=none engine=CHIP cursor=4" "$(obs 3)"
expect "after the rest: PERC is picked" "popup=$MSG engine=PERC cursor=4" "$(obs 4)"
expect "Down onto CHIP again (attached by now): the list moves, the part waits on PERC" "popup=none engine=PERC cursor=5" "$(obs 5)"
expect "after the rest: CHIP is picked, no new install" "popup=$MSG engine=CHIP cursor=5" "$(obs 6)"

# Scenarios 5 to 8: the rest restarts on every step. Steps 930 ms apart (80 ms tap + 850 ms), less than the 1 s rest, 5 of them
# (CHIP, JU6, FM4, JU6, CHIP: only CHIP installs on the host, so the one load is observable). The first step is 3.7 s before the last,
# a timer that kept the first step (or any early one) would have fired in between. "loads mark" is the instant of the last step.
GAP='wait 850'
steps() {   # steps DOWN_CMD UP_CMD: five steps, check after each, mark before the last one, then the rest after the last
    local dn="$1" up="$2"
    echo "$SYNTH; tap Down; wait 2600; poke loads reset; $dn; $GAP; $CHECKL; $dn; $GAP; $CHECKL; $dn; $GAP; $CHECKL; $up; $GAP; $CHECKL; poke loads mark; $up; $GAP; $CHECKL; wait 100; $CHECKL; wait 2000; $CHECKL"
}
steps_expect() {   # steps_expect LABEL: the 8 observations of steps()
    local L="$1"
    expect "$L: onto CHIP, 930 ms: still PERC" "popup=none engine=PERC cursor=5 loads=0 last=-1" "$(obsl 1)"
    expect "$L: onto JU6, 930 ms: still PERC" "popup=none engine=PERC cursor=6 loads=0 last=-1" "$(obsl 2)"
    expect "$L: onto FM4, 930 ms: still PERC" "popup=none engine=PERC cursor=7 loads=0 last=-1" "$(obsl 3)"
    expect "$L: back onto JU6, 930 ms (3.7 s after the first step): still PERC" "popup=none engine=PERC cursor=6 loads=0 last=-1" "$(obsl 4)"
    expect "$L: onto CHIP (the last step), 930 ms: still PERC, no load" "popup=none engine=PERC cursor=5 loads=0 last=-1" "$(obsl 5)"
    local o; o="$(obsl 6)"
    case "$o" in
        "popup=$MSG engine=CHIP cursor=5 loads=1 last="*)
            local t="${o##*last=}"
            if [ "$t" -ge 1000 ] && [ "$t" -le 1060 ]; then pass "$L: exactly one load of CHIP, ${t} ms after the last step (rest 1000 ms)"
            else fail "$L: the load comes 1000 to 1060 ms after the last step" "got ${t} ms: $o"; fi ;;
        *) fail "$L: 1030 ms after the last step: CHIP installed and picked once, with the message" "got: $o" ;;
    esac
    case "$(obsl 7)" in
        "popup=none engine=CHIP cursor=5 loads=1 last="*) pass "$L: still one load and CHIP 3 s after the last step (message expired)" ;;
        *) fail "$L: still one load and CHIP 3 s after the last step" "got: $(obsl 7)" ;;
    esac
}

echo "### scenario 5: Down arrow, steps shorter than the rest apart: the load comes one full rest after the last step"
sd="$WORK/s5"; stage "$sd"
run "$sd" "$(steps 'tap Down' 'tap Up')"
steps_expect "arrows"

echo "### scenario 6: encoder steps, the same"
sd="$WORK/s6"; stage "$sd"
run "$sd" "$(steps 'enc 1' 'enc -1')"
steps_expect "encoder"

echo "### scenario 7: a held arrow, the key repeat events (buttonHold, 380 ms apart) are steps like the presses"
# Down held: the press and 3 repeats (CHIP, JU6, FM4, row 8), then Up held: the press and 2 repeats (rows 7, 6, CHIP). 8 events in 3 s.
sd="$WORK/s7"; stage "$sd"
run "$sd" "$SYNTH; tap Down; wait 2600; poke loads reset; hold Down; wait 340; repeat Down; wait 340; repeat Down; wait 340; repeat Down; wait 340; unhold Down; $CHECKL; hold Up; wait 340; repeat Up; wait 340; $CHECKL; poke loads mark; repeat Up; wait 340; unhold Up; wait 500; $CHECKL; wait 100; $CHECKL; wait 2000; $CHECKL"
expect "held Down, 4 steps over 1.5 s: still PERC, cursor on row 8" "popup=none engine=PERC cursor=8 loads=0 last=-1" "$(obsl 1)"
expect "held Up, 3 steps: still PERC (3 s after the first step)" "popup=none engine=PERC cursor=6 loads=0 last=-1" "$(obsl 2)"
expect "900 ms after the last repeat: still PERC" "popup=none engine=PERC cursor=5 loads=0 last=-1" "$(obsl 3)"
o="$(obsl 4)"
case "$o" in
    "popup=$MSG engine=CHIP cursor=5 loads=1 last="*)
        t="${o##*last=}"
        if [ "$t" -ge 1000 ] && [ "$t" -le 1060 ]; then pass "exactly one load of CHIP, ${t} ms after the last repeat"
        else fail "the load comes 1000 to 1060 ms after the last repeat" "got ${t} ms: $o"; fi ;;
    *) fail "1 s after the last repeat: CHIP installed and picked once" "got: $o" ;;
esac
expect "3 s later: still one load" "popup=none engine=CHIP cursor=5 loads=1 last=${o##*last=}" "$(obsl 5)"

echo "### scenario 8: a step right after the load starts a new rest, and the load before it is the only one"
# the load of CHIP ends a rest. A step 100 ms later (JU6, not installed) waits a full rest of its own, the part stays on CHIP.
sd="$WORK/s8"; stage "$sd"
run "$sd" "$SYNTH; tap Down; wait 3300; poke loads reset; tap Down; wait 1000; $CHECKL; tap Down; wait 850; $CHECKL; tap Up; wait 850; $CHECKL; wait 2000; $CHECKL"
nolast() { obsl "$1" | sed 's/ last=.*//'; }
expect "CHIP loaded after its rest" "popup=$MSG engine=CHIP cursor=5 loads=1" "$(nolast 1)"
expect "Down onto JU6 (not installed) right after the load: the part stays on CHIP" "popup=none engine=CHIP cursor=6 loads=1" "$(nolast 2)"
expect "Up onto CHIP, the current one: no load, one load in all" "popup=none engine=CHIP cursor=5 loads=1" "$(nolast 3)"
expect "2 s later: still one load" "popup=none engine=CHIP cursor=5 loads=1" "$(nolast 4)"

# Scenarios 9 and 10: the engines that are built in (or attached) wait like the others. Stepping over them used to switch each at once.
echo "### scenario 9: steps over built in engines and plugin engines alternate: one load, of the last, one rest after the last step"
# WTFM -> PERC (built in) -> CHIP -> JU6 -> CHIP (not installed) -> PERC (built in, the last), 930 ms apart
sd="$WORK/s9"; stage "$sd"
run "$sd" "$SYNTH; poke loads reset; tap Down; wait 850; $CHECKL; tap Down; wait 850; $CHECKL; tap Down; wait 850; $CHECKL; tap Up; wait 850; $CHECKL; poke loads mark; tap Up; wait 850; $CHECKL; wait 100; $CHECKL; wait 2000; $CHECKL"
expect "onto PERC (built in), 930 ms: the part stays on WTFM" "popup=none engine=WTFM cursor=4 loads=0 last=-1" "$(obsl 1)"
expect "onto CHIP, 930 ms: still WTFM" "popup=none engine=WTFM cursor=5 loads=0 last=-1" "$(obsl 2)"
expect "onto JU6, 930 ms: still WTFM" "popup=none engine=WTFM cursor=6 loads=0 last=-1" "$(obsl 3)"
expect "back onto CHIP, 930 ms: still WTFM" "popup=none engine=WTFM cursor=5 loads=0 last=-1" "$(obsl 4)"
expect "onto PERC (the last step), 930 ms: still WTFM, no load" "popup=none engine=WTFM cursor=4 loads=0 last=-1" "$(obsl 5)"
o="$(obsl 6)"
case "$o" in
    "popup=$MSG engine=PERC cursor=4 loads=1 last="*)
        t="${o##*last=}"
        if [ "$t" -ge 1000 ] && [ "$t" -le 1060 ]; then pass "exactly one load, PERC, ${t} ms after the last step"
        else fail "the load comes 1000 to 1060 ms after the last step" "got ${t} ms: $o"; fi ;;
    *) fail "1030 ms after the last step: PERC picked once, with the message" "got: $o" ;;
esac
expect "3 s later: still one load, CHIP was never installed" "popup=none engine=PERC cursor=4 loads=1 last=${o##*last=}" "$(obsl 7)"

echo "### scenario 10: scrolling over built in engines only: Up from WTFM past VAP and FAT to ACD, one load"
sd="$WORK/s10"; stage "$sd"
run "$sd" "$SYNTH; poke loads reset; tap Up; wait 850; $CHECKL; tap Up; wait 850; $CHECKL; poke loads mark; tap Up; wait 850; $CHECKL; wait 100; $CHECKL"
expect "onto VAP, 930 ms: still WTFM" "popup=none engine=WTFM cursor=2 loads=0 last=-1" "$(obsl 1)"
expect "onto FAT, 930 ms: still WTFM" "popup=none engine=WTFM cursor=1 loads=0 last=-1" "$(obsl 2)"
expect "onto ACD (the last step), 930 ms: still WTFM" "popup=none engine=WTFM cursor=0 loads=0 last=-1" "$(obsl 3)"
o="$(obsl 4)"
case "$o" in
    "popup=$MSG engine=ACD cursor=0 loads=1 last="*)
        t="${o##*last=}"
        if [ "$t" -ge 1000 ] && [ "$t" -le 1060 ]; then pass "exactly one load, ACD, ${t} ms after the last step"
        else fail "the load comes 1000 to 1060 ms after the last step" "got ${t} ms: $o"; fi ;;
    *) fail "1030 ms after the last step: ACD picked once, with the message" "got: $o" ;;
esac

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
