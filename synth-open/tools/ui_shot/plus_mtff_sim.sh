#!/bin/bash
# plus_mtff_sim.sh [PLUS_BIN [OG_BIN]]   (ref/ff-imx-port-plan.md P3)
# project.mtff in the core (core/FileManager/projectSidecar.cpp, FEATURE_PROJECT_STORE) on the Tracker+ sim, which has no plugin host: the FF settings that are core
# objects (seed, groove, follow chord, 4 FX switch, stereo pairs, Strum ramp) are set with `poke mtff`, the real file manager saves the project
# (Save As, Workspace save, project.mtff next to project.mt) and opens it again in a fresh run, all kept. The scale snap table and the Parts list
# are still behind FEATURE_PLUG_HOST / FEATURE_PARTS on IMX (P4): their keys are proven as bytes of the file, a load and save on IMX keeps them.
# With OG_BIN (the MK66 configuration of the sim, default build/ui-shot/ui_shot when it exists) the same script runs there and both
# project.mtff files must be byte identical (the file bytes of MK66 are the contract).
# Host only. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PLUS="${1:-$ROOT/build/ui-shot-plus-console-bridge/ui_shot}"
OG="${2:-$ROOT/build/ui-shot/ui_shot}"
[ -x "$PLUS" ] || { echo "plus_mtff_sim.sh: no $PLUS (plus_smoke.sh builds it)" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/plus_mtff_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
check() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }
run() {   # run BIN SD SCRIPT -> poke.out
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$1" "$2" "$3" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] || : >"$WORK/poke.out"
}
get() { grep '^mtff ' "$WORK/poke.out" | tail -1; }
fresh() { rm -rf "$1"; mkdir -p "$1/Plugins/Master" "$1/Plugins/Synth"; }
WANT_OG="mtff seed 7 fx4 1 pairs 3 strum 60 groove 0,0,20,0,0,0,0,0,0,0,0,0,0,0,0,0 follow 0:0,0:0,1:4,0:0,0:0,0:0,0:0,0:0,0:0,0:0,0:0,0:0,0:0,0:0,0:0,0:0"
WANT_PLUS="${WANT_OG/pairs 3/pairs 0}"   # stereo pairs are an MK66 feature: the Tracker+ engine has no linked pair, the file keeps the keys it read
NEST="/Projects/Plus_P3"

one() {   # one BIN LABEL
    local bin="$1" lab="$2" sd="$WORK/$2" WANT="$WANT_OG"
    [ "$lab" = plus ] && WANT="$WANT_PLUS"
    echo "### $lab: set 4 FX, seed, groove, follow, pairs, Strum ramp, save as a project, open it in a fresh run"
    fresh "$sd"
    run "$bin" "$sd" "wait 500; poke mtff fx4 1; poke mtff seed 7; poke mtff groove 2 20; poke mtff follow 2 1 4; poke mtff pairs 3; poke mtff strum 60; poke mtff get; poke fmsave - $(basename $NEST); wait 4000"
    check "$lab: the settings took" "$WANT" "$(get)"
    [ -f "$sd/Workspace/project.mtff" ] && pass "$lab: Workspace/project.mtff written by the Workspace save" || fail "$lab: no Workspace/project.mtff" ""
    cp "$sd/Workspace/project.mtff" "$WORK/$lab.ws.mtff" 2>/dev/null
    local pf; pf=$(find "$sd/$(basename $NEST)" -name project.mtff 2>/dev/null | head -1)
    [ -n "$pf" ] && pass "$lab: project.mtff copied next to the project in Projects" || fail "$lab: no project.mtff in Projects" ""
    # fresh run on a new card state: wipe the Workspace and the live state, open the saved project
    rm -rf "$sd/Workspace"
    run "$bin" "$sd" "wait 500; poke mtff get; poke fmopen - $(basename $NEST); wait 6000; poke mtff get"
    check "$lab: all kept after the reopen" "$WANT" "$(get)"
    grep '^mtff ' "$WORK/poke.out" | head -1 | grep -q "seed 0 " && pass "$lab: the run before the open had none of it (the open did the work)" || fail "$lab: state before the open was not clean" "$(grep '^mtff ' "$WORK/poke.out" | head -1)"
    cp "$sd/Workspace/project.mtff" "$WORK/$lab.re.mtff" 2>/dev/null
    echo "### $lab 2: keys of the features that are still guarded on IMX (scale snap, Parts) are kept by a load and save"
    printf 'tffproj 1\nstrum.ramp 40\ntrack.1.snap 2\ntrack.3.snap 1\ntrack.1.pair 1\nx.future 5\n' >"$sd/Workspace/project.mtff"
    run "$bin" "$sd" "wait 500; poke mtff seed 9; poke mtff get; wait 15000"
    check "$lab 2: the seed was set" "mtff seed 9" "$(get | cut -d' ' -f1-3)"
    check "$lab 2: the snap and pair keys and the unknown line stay in the file after the save" "4" "$(grep -c -E '^(track\.1\.snap 2|track\.3\.snap 1|track\.1\.pair 1|x\.future 5)$' "$sd/Workspace/project.mtff")"
    check "$lab 2: and the seed is in it" "1" "$(grep -c '^seed.take 9$' "$sd/Workspace/project.mtff")"
}
one "$PLUS" plus
if [ -x "$OG" ]; then
    one "$OG" og
    # the file bytes of the two configurations: only the header's build token may differ (the stamp is the project.mt CRC, same in both)
    for k in ws re; do
        a="$(grep -v -E '^(tffproj|mt |track\.[0-9]+\.pair)' "$WORK/plus.$k.mtff" 2>/dev/null)"; b="$(grep -v -E '^(tffproj|mt |track\.[0-9]+\.pair)' "$WORK/og.$k.mtff" 2>/dev/null)"
        check "plus and MK66 sim write the same project.mtff lines ($k)" "$b" "$a"
    done
fi
[ "$fails" = 0 ] && { echo "RESULT: GREEN ($checks checks)"; exit 0; } || { echo "RESULT: RED ($fails failing of $checks)"; exit 1; }
