#!/bin/bash
# ui_shot.sh <page> [state] [-o FILE.png]   |   ui_shot.sh all   |   ui_shot.sh list
#
# Renders a screen of the real Tracker UI (core/ and synth-open/ of this tree, PLUG_HOST configuration) as a PNG on the Mac:
# the firmware's own UI code, linked for the host, draws through the FT812 model (eve_model.cpp) instead of the chip.
# Host only, nothing here is in the firmware image. PNGs go to $OUT_DIR (default ~/prj/poly/build/ui-shot).
#
# Plugins on the staged SD card: $PLG_DIR (default: the newest of ~/prj/poly/build/cpu-adopt, plugin-synth) with *.plg and *.plgi.
# Master insert plugins run from native twins of their sources (host_twins.cpp), synth engine plugins are listed and show their card
# logo but cannot install (the host has no FlexNVM bank): their part says "Missing: <name>".
# The modules of the pinned module repositories (engines/external.lock.json): PHZ, PLK, Console and Tube .plg/.plgi from $EXTERNAL_DIR (a
# directory with the published files, checked against the pins) or else from the local build directories of the private repositories.
# The sources of the insert twins at the pinned tags of local checkouts: Tape, Lo-Fi, BBD, Phaser and the Dirt wrapper from
# poly-effects-os ($EXTERNAL_SRC_EFFECTS_OS, default ~/prj/poly-effects-os), Console and Tube from poly-effects ($EXTERNAL_SRC_EFFECTS,
# default ~/prj/poly-effects). Without them the pages still render and show those modules as missing.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="$ROOT/build/ui-shot"
OUT_DIR="${OUT_DIR:-$HOME/prj/poly/build/ui-shot}"
BIN="$BUILD/ui_shot"
EXTERNAL="$ROOT/engines/external.py"
EXT_SRC="$BUILD/external"      # the insert twin sources at the pinned tags (or stubs), compiled by the Makefile
EXT_PLG="${EXTERNAL_DIR:-}"    # the published .plg and .plgi of PHZ, PLK, Console and Tube

# page[:state]  ->  "setup commands"   (a ';' list for ui_shot, see ui_shot_main.cpp). Every entry ends with the screen shot.
# the Saturation list on the Master screen runs on into Inserts (F6 selects the column, five more presses from Soft Clip), then Shift + F6 opens the page
INS_ON='tap Master; wait 800; tap F6; tap F6 5; wait 600'
INS_OPEN="$INS_ON; hold Shift; tap F6; unhold Shift; wait 800"
# the project's plugin sidecar (Workspace/plugins.txt, synth-open/plug_sidecar.h): mixer stage Soft Knee, Insert 1 = Dirt (Fold, Drive 62,
# 12 bit, Rate 2, Tone 40), Insert 2 = BBD (Mix 45, Width 70). The uids are those of the .plg files
SIDECAR='tffplug 1
format 2
mixer 10
slot 1 92b586d4 00010000 Dirt
p Type=2
p Drive=62
p Bits=12
p Rate=2
p Tone=40
p Mix=100
slot 2 b75e3097 00010000 BBD
p Mix=45
p Width=70'
# only Dirt in the project: with Shift at power up it stays "Safe", and BBD picked by hand in that session (the plugin host loads a plugin the
# user picks even in safe mode) runs and shows its parameters
SIDECAR_DIRT='tffplug 1
format 2
mixer 10
slot 1 92b586d4 00010000 Dirt
p Type=2
p Drive=62
p Bits=12
p Rate=2
p Tone=40
p Mix=100'
SYNTH_PAGES='instr 64; tap Params; wait 1200'

script_for() {   # page[:state] -> commands, empty when the page does not exist (bash 3.2: no associative arrays)
    case "$1" in
        pattern) echo 'wait 500' ;;
        master) echo 'tap Master; wait 1000' ;;
        song) echo 'tap Song; wait 1000' ;;
        config) echo 'tap Config; wait 1000' ;;
        performance) echo 'tap Performance; wait 1000' ;;
        sample-play) echo 'tap SamplePlay; wait 1000' ;;
        sample-load) echo 'tap SampleLoad; wait 1500' ;;
        instrument) echo 'instr 0; tap Params; wait 1200' ;;
        master:inserts) echo "$INS_ON" ;;
        inserts:empty) echo "$INS_OPEN" ;;
        inserts|inserts:running|inserts:safe) echo "$INS_OPEN" ;;
        inserts:bbd) echo "$INS_OPEN; tap Right; wait 600" ;;
        inserts:safe-mixed) echo "$INS_OPEN; tap Right; tap Down; wait 800" ;;
        inserts:dirt2) echo "$INS_OPEN; tap F2; wait 600" ;;
        engines) echo "$SYNTH_PAGES; wait 2500" ;;
        engines:library) echo "$SYNTH_PAGES; tap Down 9; wait 3500" ;;
        synth-params) echo "$SYNTH_PAGES; wait 2500; tap Right; tap Right; wait 500" ;;
        synth-edit) echo "$SYNTH_PAGES; tap F8; wait 2000" ;;
        synth-edit:filter) echo "$SYNTH_PAGES; tap F8; wait 1500; tap Right; wait 400; tap Down; wait 1200" ;;
        synth-macros) echo "$SYNTH_PAGES; tap Params; wait 1500" ;;
        automation) echo "$SYNTH_PAGES; tap Params; wait 800; tap Params; wait 1500" ;;
        automation:lfo) echo "$SYNTH_PAGES; tap Params; wait 800; tap Params; wait 1000; tap Right; tap Down 2; tap Right; tap Down; tap Right; wait 500; enc 2; wait 1500" ;;
        *) echo "" ;;
    esac
}
ALL_PAGES="pattern master master:inserts song config performance sample-play sample-load instrument inserts:empty inserts:running inserts:bbd inserts:dirt2 inserts:safe inserts:safe-mixed engines engines:library synth-params synth-edit synth-edit:filter synth-macros automation automation:lfo"

# the private modules: the twin sources of Console and Tube at the pinned tag, the published files of all four. Anything missing is skipped
# with a note, the pages then show those modules as missing
fetch_private() {
    mkdir -p "$EXT_SRC"
    if ! python3 -I "$EXTERNAL" source effects-os "$EXT_SRC" plug/fx/ot_tape_plug.c plug/fx/ot_lofi_plug.c plug/fx/ot_bbd_plug.c plug/fx/ot_phaser_plug.c plug/fx_dirt_plug.cpp; then
        echo "ui_shot.sh: no poly-effects-os checkout, Tape, Lo-Fi, BBD, Phaser and Dirt will fail to load on the pages" >&2
        rm -f "$EXT_SRC/fx_dirt_plug.cpp"   # gen_overlay.py writes the Dirt stub, the Makefile the others
    fi
    if ! python3 -I "$EXTERNAL" source effects "$EXT_SRC" ff_console_plug.c ff_tube_plug.c; then
        echo "ui_shot.sh: no private source for the Console and Tube twins, they will fail to load on the pages" >&2
        for f in ff_console_plug ff_tube_plug; do   # a stub with the same entry point, rewritten only when it changes (make tracks the timestamp)
            printf '/* stub: the private source of %s was not available */\nconst void *plug_entry(const void *host, unsigned abi) { (void)host; (void)abi; return 0; }\n' "$f" >"$EXT_SRC/$f.c.new"
            if cmp -s "$EXT_SRC/$f.c.new" "$EXT_SRC/$f.c"; then rm "$EXT_SRC/$f.c.new"; else mv "$EXT_SRC/$f.c.new" "$EXT_SRC/$f.c"; fi
        done
    fi
    if [ -n "$EXT_PLG" ]; then
        python3 -I "$EXTERNAL" check "$EXT_PLG" || { echo "ui_shot.sh: EXTERNAL_DIR does not match engines/external.lock.json" >&2; exit 1; }
    else
        EXT_PLG="$BUILD/external-plg"; rm -rf "$EXT_PLG"
        python3 -I "$EXTERNAL" fetch "$EXT_PLG" --from-local || { echo "ui_shot.sh: no private .plg files (set EXTERNAL_DIR), PHZ, PLK, Console and Tube will be missing" >&2; rm -rf "$EXT_PLG"; EXT_PLG=""; }
    fi
}

build() { make -s -C "$HERE" -j"$(sysctl -n hw.ncpu)" >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 1; }; }

stage_sd() {   # stage_sd DIR
    local sd="$1" plg="${PLG_DIR:-}"
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    if [ -z "$plg" ]; then for d in "$HOME/prj/poly/build/cpu-adopt" "$HOME/prj/poly/build/plugin-synth"; do [ -f "$d/dirt.plg" ] && { plg="$d"; break; }; done; fi
    [ -f "$plg/dirt.plg" ] || { echo "ui_shot.sh: no plugins found (set PLG_DIR to a folder with dirt.plg ...)" >&2; exit 1; }
    for f in dirt bbd tape lofi phaser; do cp "$plg/$f.plg" "$sd/Plugins/Master/"; done
    for f in "$plg"/*.plg "$plg"/*.plgi; do
        case "$(basename "$f")" in dirt.plg|bbd.plg|tape.plg|lofi.plg|console.plg|tube.plg|phaser.plg|phz.*|pluck.*) ;; *) cp "$f" "$sd/Plugins/Synth/" ;; esac
    done
    if [ -n "$EXT_PLG" ]; then   # the pinned private files, whatever $PLG_DIR holds of the same names is not used
        for f in console tube; do cp "$EXT_PLG/$f.plg" "$sd/Plugins/Master/"; done
        for f in phz pluck; do cp "$EXT_PLG/$f.plg" "$EXT_PLG/$f.plgi" "$sd/Plugins/Synth/"; done
    fi
}

needs_sidecar() { case "$1" in master:inserts|inserts|inserts:running|inserts:bbd|inserts:dirt2|inserts:safe|inserts:safe-mixed) return 0 ;; *) return 1 ;; esac; }

render() {   # render page[:state] out.png
    local key="$1" out="$2" script
    script="$(script_for "$key")"
    [ -n "$script" ] || { echo "ui_shot.sh: unknown page '$key' (ui_shot.sh list)" >&2; return 2; }
    local sd="$BUILD/sd/${key//[:]/_}"
    stage_sd "$sd"
    mkdir -p "$(dirname "$out")"
    if needs_sidecar "$key"; then
        # a first power up writes the Workspace project, the sidecar goes next to it, the real run then opens it
        "$BIN" "$sd" "wait 100" >"$BUILD/log-prime.txt" 2>&1 || { echo "FAIL $key (priming run)"; tail -5 "$BUILD/log-prime.txt"; return 1; }
        if [ "$key" = inserts:safe-mixed ]; then printf '%s\n' "$SIDECAR_DIRT"; else printf '%s\n' "$SIDECAR"; fi >"$sd/Workspace/plugins.txt"
    fi
    local env=()
    case "$key" in inserts:safe|inserts:safe-mixed) env=(UISHOT_SHIFT_BOOT=1) ;; esac
    env "${env[@]+"${env[@]}"}" "$BIN" "$sd" "$script; shot $out" >"$BUILD/log-${key//[:]/_}.txt" 2>&1 || { echo "FAIL $key"; tail -5 "$BUILD/log-${key//[:]/_}.txt"; return 1; }
    echo "ok   $key -> $out"
}

cmd="${1:-}"; shift || true
case "$cmd" in
    ""|-h|--help) sed -n 2,12p "$0"; exit 0 ;;
    list) printf '%s\n' $ALL_PAGES; exit 0 ;;
esac
mkdir -p "$BUILD"
fetch_private
build
if [ "$cmd" = all ]; then
    fails=0
    for k in $ALL_PAGES; do render "$k" "$OUT_DIR/${k//[:]/-}.png" || fails=$((fails+1)); done
    [ "$fails" -eq 0 ] || { echo "$fails page(s) failed"; exit 1; }
    files=(); for k in $ALL_PAGES; do files+=("$OUT_DIR/${k//[:]/-}.png"); done
    python3 -I "$HERE/png_cmp.py" --check "${files[@]}" || exit 1
    echo "all ${#files[@]} pages rendered and open as 800x480 PNG"
    exit 0
fi
state="${1:-}"; [ "${state:0:1}" = "-" ] && state=""
[ -n "$state" ] && [ "$state" != "-o" ] && shift || state=""
out=""
if [ "${1:-}" = "-o" ]; then out="${2:?-o needs a file}"; fi
key="$cmd"; [ -n "$state" ] && key="$cmd:$state"
[ -n "$out" ] || out="$OUT_DIR/${key//[:]/-}.png"
render "$key" "$out"
