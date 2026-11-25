#!/bin/bash
# modit_popup_sim.sh [--update]
#
# The popups and the first use load of extras.plg (MOD import, IT import, IT export) on the EXTHOOKS=1 ui_shot binary: the real Project screen,
# the real loader rules (synth-open/ext_loader.h extrasLoad) on a staged card, the host standing in for the module's code (ext_module.cpp
# enterModit). Each case taps File and then Import Mod (F3) or Export (F6) and Mod (F5) and compares the screen with a golden PNG
# (synth-open/tools/modit/goldens/popups/):
#   P1  no modit.plg                       "MOD/IT module missing"
#   P2  a file for another firmware image  "extras.plg is for another firmware"
#   P3  a damaged file (bad CRC)           "extras.plg is damaged"
#   P4  develop mode, inside the 4 s key window of the boot (UISHOT_NO_SETTLE): extras.plg has a region of its own, MOD/IT works (no busy popup)
#   P5  a good file: the Import Mod browser opens (no popup), and a second click needs no second load
#   P6  export, no file                    the same popup from the Export window
#   P7  export, a good file                the export runs and Mod/<project>.it is written
#   P8  develop mode, Shift held at power on (UISHOT_SHIFT_BOOT): the console module is skipped, MOD/IT works (browser opens)
#   P9  develop mode, past the key window: the console module found nothing, MOD/IT works
# --update rewrites the goldens. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
MODIT="$ROOT/synth-open/tools/modit"
BIN="${BIN:-$ROOT/build/ui-shot-ext/ui_shot}"
GOLD="$MODIT/goldens/popups"
UPDATE=0; [ "${1:-}" = "--update" ] && UPDATE=1
WORK="$(mktemp -d "${TMPDIR:-/tmp}/modit_popup.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -n "${UI_SHOT_BIN:-}" ]; then BIN="$UI_SHOT_BIN"; BIN_PREBUILT=1; fi   # UI_SHOT_BIN: a prebuilt EXTHOOKS=1 binary, no make
if [ -z "${BIN_PREBUILT:-}" ]; then
    mkdir -p "$ROOT/build"
    make -s -C "$HERE" EXTHOOKS=1 -j"$(sh "$HERE/../jobs.sh" n)" >"$ROOT/build/ui-shot-ext.make.log" 2>&1 || { tail -30 "$ROOT/build/ui-shot-ext.make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "no ui_shot binary $BIN"; exit 2; }

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }

# Capture is on by default on every card (its square in the title bar): off in every card here, these cases compare the MOD/IT screens with each other
nocap() { mkdir -p "$1/Features"; printf 'feature 1\non 0\n' > "$1/Features/capture.cfg"; }

# card NAME [module args of make_host_module.py, "none" for no file]: a staged card with the fixtures and the module file
card() {
    local sd="$WORK/$1"; shift; rm -rf "$sd"; mkdir -p "$sd/Mods"; cp "$MODIT"/fixtures/golden.* "$sd/Mods/"; nocap "$sd"
    if [ "${1:-}" != none ]; then mkdir -p "$sd/Plugins/Ext"; python3 -I "$MODIT/make_host_module.py" "$sd/Plugins/Ext/extras.plg" "$@"; fi
    echo "$sd"
}
# shot NAME SD SCRIPT [ENV=VAL...]: runs the script, the screen shot is $WORK/NAME.png
shot() {
    local name="$1" sd="$2" script="$3"; shift 3
    env "$@" ASAN_OPTIONS=detect_leaks=0 "$BIN" "$sd" "$script; shot $WORK/$name.png" >"$WORK/$name.log" 2>&1
}
# golden NAME SHOT: the screen of the case SHOT against goldens/popups/NAME.png
golden() {
    local g="$GOLD/$1.png"
    [ -f "$WORK/$2.png" ] || { fail "$1" "no screen shot of $2: $(tail -3 "$WORK/$2.log")"; return; }
    if [ "$UPDATE" = 1 ]; then mkdir -p "$GOLD"; cp "$WORK/$2.png" "$g"; pass "$1 golden written"; return; fi
    if [ ! -f "$g" ]; then fail "$1" "no golden $g (run with --update)"; return; fi
    if cmp -s "$g" "$WORK/$2.png"; then pass "$1"; else fail "$1 differs from the golden" "$g"; fi
}

same() { if [ -f "$WORK/$2.png" ] && cmp -s "$WORK/$1.png" "$WORK/$2.png"; then pass "$3"; else fail "$3" "$1 and $2 differ"; fi; }
OPEN='wait 500; tap File; wait 600; tap F3; wait 800'

echo "-- P1: no module file"
sd="$(card p1 none)"; shot p1 "$sd" "$OPEN" X=1; golden popup_missing p1
echo "-- P2: a module of another image"
sd="$(card p2 --image-id 0x12345678)"; shot p2 "$sd" "$OPEN" X=1; golden popup_otherimage p2
echo "-- P2b: a module linked for another address"
sd="$(card p2b --run 0x70D80000)"; shot p2b "$sd" "$OPEN" X=1
echo "-- P3: a damaged module (bad CRC)"
sd="$(card p3 --bad-crc)"; shot p3 "$sd" "$OPEN" X=1; golden popup_damaged p3
same p2b p3 "P2b another address: the damaged popup"
echo "-- P3b: not a module at all"
sd="$(card p3b --bad-magic)"; shot p3b "$sd" "$OPEN" X=1
same p3b p3 "P3b a file with no module header: the damaged popup"
sd="$(card p3c --cut 200)"; shot p3c "$sd" "$OPEN" X=1
same p3c p3 "P3c a file cut short: the damaged popup"

echo "-- P4: develop mode inside the key window of the boot"
sd="$(card p4)"; : >"$sd/.develop"; nocap "$sd"; shot p4 "$sd" "tap File; wait 300; tap F3; wait 800" UISHOT_NO_SETTLE=1; golden browser_open_keywindow p4

echo "-- P5: a good module"
sd="$(card p5)"; shot p5 "$sd" "$OPEN" X=1; golden browser_open p5
# the second click: the module file is not needed again, the card no longer has it and the browser still opens
sd="$(card p5b)"; shot p5b "$sd" "$OPEN; rm $sd/Plugins/Ext/extras.plg; tap File; wait 300; tap File; wait 300; tap F3; wait 800" X=1
same p5b p5 "second click: loaded once, no second read"

echo "-- P6: export, no module file"
sd="$(card p6 none)"; shot p6 "$sd" "wait 500; tap File; wait 600; tap F6; wait 600; tap F5; wait 800" X=1; golden popup_missing_export p6

echo "-- P7: export, a good module"
sd="$(card p7)"; shot p7 "$sd" "wait 500; tap File; wait 600; tap F6; wait 600; tap F5; wait 600; tap F7; wait 4000" X=1   # F7 = Don't Save of the unsaved project question
n=$(ls "$sd"/Mod/*.it 2>/dev/null | wc -l | tr -d ' ')
[ "$n" = 1 ] && [ "$(stat -f %z "$sd"/Mod/*.it)" -gt 1000 ] && pass "export wrote one .it ($(stat -f %z "$sd"/Mod/*.it) B)" || fail "export" "files in Mod/: $n"

echo "-- P8: develop mode, Shift held at power on"
sd="$(card p8)"; : >"$sd/.develop"; nocap "$sd"; shot p8 "$sd" "$OPEN" UISHOT_SHIFT_BOOT=1
same p8 p5 "Shift at boot: MOD/IT opens its browser"

echo "-- P9: develop mode, past the key window, no console module on the card"
sd="$(card p9)"; : >"$sd/.develop"; nocap "$sd"; shot p9 "$sd" "$OPEN" X=1
same p9 p5 "develop mode, window closed: MOD/IT opens its browser"
echo "-- P10: develop mode, the console module loaded (stand-in, UISHOT_HOST_CONSOLE), then MOD/IT"
sd="$(card p10)"; : >"$sd/.develop"; nocap "$sd"; python3 -I "$MODIT/make_host_module.py" "$sd/Plugins/Ext/module.plg" --run 0x70D8A000
shot p10 "$sd" "$OPEN" X=1 UISHOT_HOST_CONSOLE=1
same p10 p5 "console loaded: MOD/IT still opens its browser"
grep -q 'update hook console, log hook console, modit hook set' "$WORK/p10.log" && case "$(cat "$sd/Plugins/Ext/status.txt")" in loaded*) true;; *) false;; esac \
    && pass "console loaded and kept its hooks beside extras.plg" || fail "P10 coexistence" "$(grep 'host console' "$WORK/p10.log") | $(cat "$sd/Plugins/Ext/status.txt")"

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
