#!/bin/bash
# modit_golden_sim.sh [--update]
#
# MOD import, IT import and IT export (core/FileManager/fileManagerImportMod.cpp, fileManagerImportIt.cpp, fileManagerExportToMod.cpp) on the
# ui_shot host binary, compared with goldens (synth-open/tools/modit/goldens/) that were captured from the built in code of release-1.4.0-int
# d471709, before the three files left the image for modit.plg. The inputs are written by synth-open/tools/modit/make_fixtures.py and checked
# in (synth-open/tools/modit/fixtures/). Each scenario stages its own card, runs the script command of ui_shot_main.cpp (modimport, modexport:
# the file manager calls the Project screen makes, run until idle) and reduces the result to a manifest, one line "<size> <sha256> <path>" per
# file, sorted:
#   G1  import golden.mod     manifest of Workspace/ (project.mt, the instruments, the patterns, the song)
#   G2  import golden.it      the same
#   G3  export IT             Mod/golden.it written from the project of G2 (one file)
#   G4  reimport of G3        the exported file read back: Workspace/ manifest
#   G5  a zero filled .mod, a truncated .it and a file that is not there: what the failed import leaves in Workspace/
# In the firmware the same code runs from the module (modit.plg) through the five forwarders and ext_hooks.modit, here from the image through
# the same forwarders, so a difference means the glue or the sources changed. --update rewrites the goldens (only for a reviewed change of the
# sources). Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
#   BIN   the ui_shot binary, default build/ui-shot/ui_shot (make -C synth-open/tools/ui_shot)
#   EXT=1 the EXTHOOKS=1 binary (build/ui-shot-ext/ui_shot): the sources behind the renamed members, modit_dispatch.cpp and the five forwarders of
#         the image, as in the release image. The card gets Plugins/Ext/modit.plg (make_host_module.py), the goldens are the same files
#         MODIT_PLG=FILE  the card gets that file instead (the release image's own modit.plg: with BIN_PREBUILT=1 BIN=<pinned emulator ui_shot>, whose
#         host loader rules (MODIT_ID, MODIT_RUN of emu-release.sh) accept only the file of its release image, not the stand-in)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
MODIT="$ROOT/synth-open/tools/modit"
EXT="${EXT:-}"
BIN="${BIN:-$ROOT/build/ui-shot${EXT:+-ext}/ui_shot}"
UPDATE=0; [ "${1:-}" = "--update" ] && UPDATE=1
WORK="$(mktemp -d "${TMPDIR:-/tmp}/modit_golden.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN_PREBUILT:-}" ]; then
    mkdir -p "$ROOT/build"
    env -u EXT make -s -C "$HERE" ${EXT:+EXTHOOKS=1} -j4 >"$ROOT/build/ui-shot.make.log" 2>&1 || { tail -30 "$ROOT/build/ui-shot.make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "no ui_shot binary $BIN"; exit 2; }

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }

# a staged card: Mods/ holds the fixtures (and the extra files of a scenario)
card() {
    local sd="$WORK/$1"; rm -rf "$sd"; mkdir -p "$sd/Mods"; cp "$MODIT"/fixtures/golden.* "$sd/Mods/"
    if [ -n "$EXT" ]; then mkdir -p "$sd/Plugins/Ext"; if [ -n "${MODIT_PLG:-}" ]; then cp "$MODIT_PLG" "$sd/Plugins/Ext/modit.plg"; else python3 -I "$MODIT/make_host_module.py" "$sd/Plugins/Ext/modit.plg"; fi; fi
    echo "$sd"
}
manifest() { ( cd "$1" && find "$2" -type f ! -name '._*' | LC_ALL=C sort | while read -r f; do printf '%s %s %s\n' "$(stat -f %z "$f")" "$(shasum -a 256 "$f" | cut -c1-64)" "$f"; done ); }
run() { ASAN_OPTIONS=detect_leaks=0 "$BIN" "$1" "wait 500; $2" 2>"$WORK/err.txt" | grep -E '^mod(import|export) '; }
# compare NAME MANIFEST_FILE: against goldens/NAME.txt, or write it with --update
golden() {
    local g="$MODIT/goldens/$1.txt"
    if [ "$UPDATE" = 1 ]; then mkdir -p "$MODIT/goldens"; cp "$2" "$g"; pass "$1 golden written ($(wc -l <"$g" | tr -d ' ') lines)"; return; fi
    if [ ! -f "$g" ]; then fail "$1" "no golden $g (run with --update on the base)"; return; fi
    if cmp -s "$g" "$2"; then pass "$1 ($(wc -l <"$g" | tr -d ' ') files equal)"; else fail "$1 differs from the built in output" "$(diff "$g" "$2" | head -8)"; fi
}

echo "-- G1: MOD import"
sd="$(card g1)"; out="$(run "$sd" "modimport Mods golden.mod")"; echo "   $out"
case "$out" in *started*) manifest "$sd" Workspace >"$WORK/g1.txt"; golden import_mod "$WORK/g1.txt";; *) fail "G1 import" "$out";; esac

echo "-- G2: IT import"
sd="$(card g2)"; out="$(run "$sd" "modimport Mods golden.it")"; echo "   $out"
case "$out" in *started*) manifest "$sd" Workspace >"$WORK/g2.txt"; golden import_it "$WORK/g2.txt";; *) fail "G2 import" "$out";; esac

echo "-- G3: IT export of the project of G2"
sd="$(card g3)"; out="$(run "$sd" "modimport Mods golden.it; modexport")"; echo "   $out"
case "$out" in *"modexport"*started*) manifest "$sd" Mod >"$WORK/g3.txt"; golden export_it "$WORK/g3.txt";; *) fail "G3 export" "$out";; esac

echo "-- G4: the exported file read back"
sd="$(card g4)"; out="$(run "$sd" "modimport Mods golden.it; modexport")"
cp "$sd/Mod/golden.it" "$sd/Mods/exported.it" 2>/dev/null
out="$(run "$sd" "modimport Mods exported.it")"; echo "   $out"
case "$out" in *started*) manifest "$sd" Workspace >"$WORK/g4.txt"; golden reimport_it "$WORK/g4.txt";; *) fail "G4 reimport" "$out";; esac

echo "-- G5: not a MOD or IT, a truncated IT, a file that is not there (the Interface acknowledges the error status inside the run, the card is what is compared)"
sd="$(card g5a)"; head -c 4000 /dev/zero >"$sd/Mods/junk.mod"; out="$(run "$sd" "modimport Mods junk.mod")"; echo "   $out"
case "$out" in *started*) manifest "$sd" Workspace >"$WORK/g5a.txt"; golden import_junk_mod "$WORK/g5a.txt";; *) fail "G5a" "$out";; esac
sd="$(card g5b)"; printf 'IMPM' >"$sd/Mods/short.it"; out="$(run "$sd" "modimport Mods short.it")"; echo "   $out"
case "$out" in *started*) manifest "$sd" Workspace >"$WORK/g5b.txt"; golden import_short_it "$WORK/g5b.txt";; *) fail "G5b" "$out";; esac
sd="$(card g5c)"; out="$(run "$sd" "modimport Mods nothere.it")"; echo "   $out"
case "$out" in *started*) manifest "$sd" Workspace >"$WORK/g5c.txt"; golden import_missing "$WORK/g5c.txt";; *) fail "G5c" "$out";; esac

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
