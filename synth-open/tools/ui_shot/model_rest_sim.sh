#!/bin/bash
# model_rest_sim.sh [PNG_DIR]
#
# When the "Init patch loaded" message of the Synth Model list appears, run on the real UI code of this tree (ui_shot, the PLUG_HOST
# configuration with the CHIP host twin) on the Mac. Host only: no device, every scenario has its own SD card folder.
# The message belongs to the engine change. A plugin engine that is not installed is picked after the list rests on it for kModelRestMs
# (1 s), an engine that is built in or already attached switches at once. Scrolling past engines with short rests installs nothing
# and shows no message. Reads the popup on screen ("poke popup", host_poke.cpp) and the engine the part stores ("poke picker").
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
}
obs() { sed -n "${1}p" "$WORK/obs.txt"; }   # the Nth observation
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 10"; }
MSG='Init patch loaded'

echo "### scenario 1: the message of a built in engine does not stay up while the list moves to an engine that needs an install"
sd="$WORK/s1"; stage "$sd"
run "$sd" "$SYNTH; tap Down; wait 100; $CHECK; wait 300; tap Down; $CHECK; wait 300; $CHECK; wait 300; $CHECK; wait 700; $CHECK; $(shot s1-installed)"
expect "WTFM -> PERC (built in): the part switches at once with its message" "popup=$MSG engine=PERC cursor=4" "$(obs 1)"
expect "-> CHIP (not installed): the list moves, the part stays on PERC, no message (the one of PERC is gone)" "popup=none engine=PERC cursor=5" "$(obs 2)"
expect "300 ms later: still nothing" "popup=none engine=PERC cursor=5" "$(obs 3)"
expect "600 ms of rest: still nothing, the part is on PERC" "popup=none engine=PERC cursor=5" "$(obs 4)"
expect "after the 1 s rest: CHIP is installed and picked, now the message shows" "popup=$MSG engine=CHIP cursor=5" "$(obs 5)"

echo "### scenario 2: skipping through engines with short rests installs nothing and shows no message"
sd="$WORK/s2"; stage "$sd"
run "$sd" "$SYNTH; tap Down; wait 2600; $CHECK; tap Down; wait 400; $CHECK; tap Down; wait 400; $CHECK; tap Down; wait 400; $CHECK; tap Up; wait 400; $CHECK; tap Up; wait 400; $CHECK; wait 2000; $CHECK"
expect "PERC, message expired" "popup=none engine=PERC cursor=4" "$(obs 1)"
expect "Down onto CHIP, 400 ms" "popup=none engine=PERC cursor=5" "$(obs 2)"
expect "Down onto JU6, 400 ms" "popup=none engine=PERC cursor=6" "$(obs 3)"
expect "Down onto FM4, 400 ms" "popup=none engine=PERC cursor=7" "$(obs 4)"
expect "Up onto JU6, 400 ms" "popup=none engine=PERC cursor=6" "$(obs 5)"
expect "Up onto CHIP, 400 ms" "popup=none engine=PERC cursor=5" "$(obs 6)"
expect "the rest on CHIP ends: installed, message" "popup=$MSG engine=CHIP cursor=5" "$(obs 7)"

echo "### scenario 3: back on the current engine before the rest ends: nothing is installed"
sd="$WORK/s3"; stage "$sd"
run "$sd" "$SYNTH; tap Down; wait 2600; tap Down; wait 500; $CHECK; tap Up; wait 200; $CHECK; wait 2500; $CHECK"
expect "on CHIP, 500 ms" "popup=none engine=PERC cursor=5" "$(obs 1)"
expect "back on PERC: no message, no pending pick" "popup=none engine=PERC cursor=4" "$(obs 2)"
expect "2.5 s later: the part is still on PERC, nothing was installed" "popup=none engine=PERC cursor=4" "$(obs 3)"

echo "### scenario 4: a rest of 1 s picks the engine, an engine that is attached switches at once"
sd="$WORK/s4"; stage "$sd"
run "$sd" "$SYNTH; tap Down 2; wait 700; $CHECK; wait 500; $CHECK; wait 2600; tap Up; wait 100; $CHECK; wait 2600; tap Down; wait 100; $CHECK"
expect "Down twice (PERC at once, then CHIP): the message of PERC is down, CHIP waits" "popup=none engine=PERC cursor=5" "$(obs 1)"
expect "1.2 s: CHIP installed and picked, message" "popup=$MSG engine=CHIP cursor=5" "$(obs 2)"
expect "Up onto PERC (built in): at once" "popup=$MSG engine=PERC cursor=4" "$(obs 3)"
expect "Down onto CHIP again (attached by now): at once, no rest" "popup=$MSG engine=CHIP cursor=5" "$(obs 4)"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
