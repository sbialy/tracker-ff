# microtune_p3_lib.sh - shared by the microtuning phase 3 sims (microtune_names_sim.sh, microtune_snap_sim.sh, microtune_fine_sim.sh, microtune_chord_sim.sh).
# Sourced, not run. The caller sets NAME (for messages). Gives BIN (ui_shot, BIN or UI_SHOT_BIN or build/ui-shot), WORK, ROOT, HERE, check, near, ed, GO, newsd, runui,
# mtff, pitch. PLAT=og|PLUS|MINI only labels, LISTKEYS=tap moves a list by Down and Up (the Mini has no encoder in its lists).
HERE="$(cd "$(dirname "${BASH_SOURCE[1]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
PLAT="${PLAT:-og}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/${NAME:-microtune_p3}.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN:-}" ]; then
    BIN="${UI_SHOT_BIN:-$ROOT/build/ui-shot${SAN:+-san}/ui_shot}"
    [ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "${NAME:-microtune_p3}: no ui_shot at $BIN" >&2; exit 99; }
fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
near() { awk -v a="$1" -v b="$2" -v tol="${3:-0.004}" 'BEGIN {d = a - b; if (d < 0) d = -d; exit !(b > 0 && d <= b * tol)}'; }
# the Project Settings page: File F7, the Tuning group is the row above the last one (MPE out is last, so Down 20 then Up) (row 12 Scale file, row 13 Note names). ed ROW DELTA opens the list of a row, moves it by DELTA, applies
GO="wait 500; tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300;"
ed() { local r=$1 d=$2 t="tap Up 20; tap Down $1; tap F8; wait 200;"
    if [ "${LISTKEYS:-enc}" = tap ]; then
        while [ "$d" -gt 0 ]; do t="$t tap Down; wait 50;"; d=$((d-1)); done
        while [ "$d" -lt 0 ]; do t="$t tap Up; wait 50;"; d=$((d+1)); done
    else
        while [ "$d" -ge 3 ]; do t="$t enc 3;"; d=$((d-3)); done
        while [ "$d" -le -3 ]; do t="$t enc -3;"; d=$((d+3)); done
        [ "$d" -ne 0 ] && t="$t enc $d;"
    fi
    echo "$t tap F8; wait 200;"; }
newsd() {   # NAME FILES...: a card folder with those files in /Tunings
    local sd="$WORK/$1"; shift; rm -rf "$sd"; mkdir -p "$sd/Tunings" "$sd/Plugins/Master" "$sd/Plugins/Synth"
    for f in "$@"; do cp "$f" "$sd/Tunings/"; done; echo "$sd"; }
runui() {   # SD "ui commands": the page work in one process, then the project is saved into the Workspace. stdout to $WORK/ui.out
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$1" "$GO $2 poke projsave" >"$WORK/ui.out" 2>>"$WORK/ui.err" || true; }
mtff() { grep '^tune\.' "$1/Workspace/project.mtff" 2>/dev/null | tr '\n' '|'; }
mkws() {   # SD "mtff text": a card whose Workspace is a saved default project with this project.mtff
    rm -rf "$1"; mkdir -p "$1"; rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$1" "wait 200; poke projsave" >/dev/null 2>&1 || true; printf '%b' "$2" > "$1/Workspace/project.mtff"; }
# pitch on the voice tap of a sample track: instrument 0 is a 441 Hz sine, key K on track 1. SD K [MS] [EXTRA ui commands before the note] -> Hz
pitch() {
    local sd=$1 k=$2 ms=${3:-600} x=${4:-}
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; sampletone 0; instr 0; $x setstep 0 0 $k 0 0 0 0 0; audio 1; audiostat tap 0; audiostat 1; tap Play; wait $ms; tap Play; audiostat 0; audiostat tapdump" \
        2>>"$WORK/pitch.err" | grep -E '^blk ' | awk '$4 > 500 {v++; if (v > 2) {z += $6; n++}} END {printf "%.2f", n ? z / 2 / (n * 128 / 44117.647) : 0}'; }
finish() {
    local errs; errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
    check "no sanitizer report in any run" "$([ "$errs" = 0 ] && echo ok || echo "$errs reports")"
    echo
    if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; exit 0; fi
    echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1
}
