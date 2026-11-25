#!/bin/bash
# sidecar_path_sim.sh [BIN]
#
# The project sidecar hooks of the plugin host (synth-open/plug_host.cpp: sidecarOpen, sidecarToProject, projectSave) with the folder paths of
# projects saved in NESTED folders, on the real plug_host.cpp of this tree (ui_shot, the PLUG_HOST configuration) over a card folder on the Mac.
# The file manager hands the hooks project folders of up to PATCH_SIZE (255) bytes. The sidecar paths used to be built in 64 byte buffers
# without a truncation check: "<folder>/plugins.txt" of a folder of 52 or more characters was cut, the file went to a wrong name (a prefix
# of the folder path), and two projects that share their first 63 characters shared one file (a Save of the one without inserts removed
# the other's). Now a path that does not fit (255 characters) is refused: no read, no write, an error return.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)". Build first (ui_shot.sh pattern builds build/ui-shot/ui_shot).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-${UI_SHOT_BIN:-$ROOT/build/ui-shot/ui_shot}}"
[ -x "$BIN" ] || { echo "sidecar_path_sim.sh: no $BIN (ui_shot.sh pattern builds it)" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/sidecar_path_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
check() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }

SIDECAR="tffplug 1
format 2
mixer 10
slot 1 92b586d4 00010000 Dirt
p Type=2
p Drive=62
p Mix=100"

stage() {   # stage SD: a card with the Workspace project and a Workspace sidecar (the real run reads it at boot)
    local sd="$1"
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    "$BIN" "$sd" "wait 100" >/dev/null 2>&1 || { echo "priming run failed"; exit 2; }
    rm -f "$sd/Workspace/project.mtff"   # the new project of the priming run has one (project.mtff, mtff_sim.sh): this sim is about plugins.txt
    printf '%s\n' "$SIDECAR" >"$sd/Workspace/plugins.txt"
}
run() {   # run SD POKE... : every poke at its place in one run, results in $WORK/poke.out
    local sd="$1" script="wait 200" l; shift
    for l in "$@"; do script="$script; poke $l"; done
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "$script" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] || : >"$WORK/poke.out"
}
rc_of() { grep "^sidecar $1 rc=" "$WORK/poke.out" | tail -1 | sed 's/.*rc=//'; }
tree() { (cd "$1" && find "${2:-.}" -type f | LC_ALL=C sort); }

A="Projects/Albums_2026/Summer_Tour_Warmup_Sessions/Rehearsal_Take_One"
B="Projects/Albums_2026/Summer_Tour_Warmup_Sessions/Rehearsal_Take_Two"
echo "folder A is ${#A} characters, B ${#B}, and they share the first $(( ${#A} - 3 ))"

echo "### 1: Save into a nested folder (folder of ${#A} characters)"
sd="$WORK/s1"; stage "$sd"; mkdir -p "$sd/$A"
run "$sd" "sidecar save Workspace $A"
check "the call reports success" "1" "$(rc_of save)"
check "the file is <folder>/plugins.txt, with the Workspace content" "$(printf '%s\n' "$SIDECAR")" "$(cat "$sd/$A/plugins.txt" 2>/dev/null)"
check "nothing else was written under Projects" "$A/plugins.txt" "$(tree "$sd" Projects | sed 's|^Projects/|Projects/|')"

echo "### 2: two projects of one nested folder, A with inserts, B without: B's Save must not touch A's file"
sd="$WORK/s2"; stage "$sd"; mkdir -p "$sd/$A" "$sd/$B"
run "$sd" "sidecar save Workspace $A"
rm -f "$sd/Workspace/plugins.txt"
run "$sd" "sidecar save Workspace $B"
check "A keeps its sidecar after B was saved" "$(printf '%s\n' "$SIDECAR")" "$(cat "$sd/$A/plugins.txt" 2>/dev/null)"
check "B has none" "absent" "$([ -e "$sd/$B/plugins.txt" ] && echo present || echo absent)"
check "no file under a cut name (both folders cut to the same 63 characters used to share one)" "$A/plugins.txt" "$(tree "$sd" Projects)"

echo "### 3: Open from a nested folder"
sd="$WORK/s3"; stage "$sd"; mkdir -p "$sd/$A"
printf '%s\n' "$SIDECAR" >"$sd/$A/plugins.txt"; rm -f "$sd/Workspace/plugins.txt"
run "$sd" "sidecar open $A Workspace"
check "the call reports success" "1" "$(rc_of open)"
check "the Workspace file is the project's" "$(printf '%s\n' "$SIDECAR")" "$(cat "$sd/Workspace/plugins.txt" 2>/dev/null)"

echo "### 4: the longest path that fits (folder of 243 characters, the file path is 255) and the shortest that does not (244)"
deep() {   # deep N: a folder path of exactly N characters under Projects/, components of 40 characters
    python3 -I -c '
import sys
n = int(sys.argv[1]); p = "Projects"
while len(p) < n:
    room = n - len(p) - 1
    p += "/" + ("d" * min(40, room))
print(p)' "$1"
}
D243="$(deep 243)"; D244="$(deep 244)"
sd="$WORK/s4"; stage "$sd"; mkdir -p "$sd/$D243" "$sd/$D244"
run "$sd" "sidecar save Workspace $D243"
check "243: written" "1 $(printf '%s\n' "$SIDECAR" | cksum)" "$(rc_of save) $(cat "$sd/$D243/plugins.txt" 2>/dev/null | cksum)"
run "$sd" "sidecar save Workspace $D244"
check "244: refused (rc 0)" "0" "$(rc_of save)"
check "244: nothing written, no cut name anywhere" "absent" "$([ -e "$sd/$D244/plugins.txt" ] && echo present || echo absent)"
check "244: the card holds only the 243 folder's file" "$D243/plugins.txt" "$(tree "$sd" Projects)"
rm -f "$sd/$D243/plugins.txt"
run "$sd" "sidecar wssave $D244"
check "244: the Workspace save of a sidecar is refused too (rc 0)" "0" "$(rc_of wssave)"
check "244: still nothing written" "" "$(tree "$sd" Projects)"

echo "### 5: Open from a folder that does not fit"
sd="$WORK/s5"; stage "$sd"; mkdir -p "$sd/$D244"
run "$sd" "sidecar open $D244 Workspace"
check "refused (rc 0)" "0" "$(rc_of open)"
check "the previous project's Workspace file is gone (the open is a failed copy)" "absent" "$([ -e "$sd/Workspace/plugins.txt" ] && echo present || echo absent)"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
