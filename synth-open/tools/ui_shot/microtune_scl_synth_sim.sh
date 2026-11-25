#!/bin/bash
# microtune_scl_synth_sim.sh [EVIDENCE_DIR]
#
# Microtuning phase 2 on SYNTH parts: a Scala file picked in Project Settings > Tuning > Scale file moves the float pitch of ACD (open_synth.cpp noteOnNow adds
# P(k) - k to n.pitch, and P comes from the same rel() the sample tracks use). Part 1 plays ACD on track 1, the master mix is measured by the zero crossings of
# the left channel (audiostat lrdump) and a key is judged by its ratio to the 1/1 key (1 % tolerance, so the files below differ from 12 EDO by more than that).
#   Y1  a septimal 5 note scale (8/7 6/5 3/2 7/4 2/1, written by this script into /Tunings): keys 61 .. 65 and the period above and below
#   Y2  Bohlen-Pierce just (the release example, 3/1 non octave): key 73 is x3, key 47 is x1/3 (key 64 is left out: its ACD zero crossing count reads 1.9 % low, neighbours 0.9 %)
#   Y3  a project with no scale file and one with the file picked then "EDO" again render the same bytes (the row is reversible)
#   Y4  PERC (a kit, the note picks the drum) renders the same bytes with a Scala tuning as with none, ACD does not (the control)
# CHIP (plugin, bend) is microtune_scl_chip_sim.sh on the bridge build. Host only, plain ui_shot (build/ui-shot), BIN or UI_SHOT_BIN overrides it.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
EVID="${1:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/microtune_scl_synth_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN:-}" ]; then
    BIN="${UI_SHOT_BIN:-$ROOT/build/ui-shot${SAN:+-san}/ui_shot}"
    [ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "microtune_scl_synth_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
[ -n "$EVID" ] && mkdir -p "$EVID"
TUN="$ROOT/releases/tunings"
fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
near() { awk -v a="$1" -v b="$2" -v tol="${3:-0.01}" 'BEGIN {d = a - b; if (d < 0) d = -d; exit !(b > 0 && d <= b * tol)}'; }
ed() {
    local r=$1 d=$2 t="tap Up 20; tap Down $1; tap F8; wait 200;"
    while [ "$d" -ge 3 ]; do t="$t enc 3;"; d=$((d-3)); done
    while [ "$d" -le -3 ]; do t="$t enc -3;"; d=$((d+3)); done
    [ "$d" -ne 0 ] && t="$t enc $d;"
    echo "$t tap F8; wait 200;"
}
# the picker: Scale file row 12, list position 1 is the only file of the card. Then back to the pattern editor
PICK="tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300; $(ed 12 1) tap F7; wait 300; tap F7; wait 800;"
card() {   # NAME FILES...
    local sd="$WORK/$1"; shift; rm -rf "$sd"; mkdir -p "$sd/Tunings"; for f in "$@"; do cp "$f" "$sd/Tunings/"; done; echo "$sd"; }
printf 'septimal 5 notes\n5\n8/7\n6/5\n3/2\n7/4\n2/1\n' > "$WORK/septimal5.scl"
play() {   # NAME CARD "setup" -> $WORK/NAME.lr: the master blocks of 600 ms of part 1 (ACD unless the setup says otherwise)
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$2" "wait 500; instr 64; poke voicecount 0 1; $3 audio 1; audiostat 1; tap Play; wait 600; tap Play; audiostat 0; audiostat lrdump" \
        2>"$WORK/$1.err" >"$WORK/$1.raw"
    grep -E '^lr ' "$WORK/$1.raw" >"$WORK/$1.lr"
}
hz() { awk '$4 > 300 {v++; if (v > 5) {z += $8; n++}} END {printf "%.2f", n ? z / 2 / (n * 128 / 44117.647) : 0}' "$WORK/$1.lr"; }
note() { echo "setstep 0 0 $1 64 0 0 0 0;"; }
[ -n "$EVID" ] && : > "$EVID/synth-table.txt"
ratios() {   # LABEL CARD REF "KEY NUM/DEN"...
    local label=$1 sd=$2; shift 2
    play "$label-60" "$sd" "$PICK $(note 60)"; local ref; ref=$(hz "$label-60")
    check "$label key 60 (the 1/1) sounds ($ref Hz)" "$(awk -v r="$ref" 'BEGIN {exit !(r > 100)}' && echo ok || echo silent)"
    for e in "$@"; do
        local k=${e%% *} r=${e#* } w
        w=$(awk -v r="$r" 'BEGIN {split(r, a, "/"); printf "%.5f", a[1] / a[2]}')
        play "$label-$k" "$sd" "$PICK $(note $k)"; local t; t=$(hz "$label-$k")
        [ -n "$EVID" ] && printf '%-10s key %3s want x%-9s master %9s Hz ref %9s Hz\n' "$label" "$k" "$w" "$t" "$ref" >> "$EVID/synth-table.txt"
        if near "$t" "$(awk -v r="$ref" -v w="$w" 'BEGIN {printf "%.3f", r * w}')" 0.012; then check "$label key $k sounds x$r of the 1/1" ok; else check "$label key $k" "want x$r ($w) of $ref Hz, got $t Hz"; fi
    done
}
sd=$(card y1 "$WORK/septimal5.scl")
ratios Y1 "$sd" "61 8/7" "62 6/5" "63 3/2" "64 7/4" "65 2/1" "70 4/1" "59 7/8" "55 1/2" "56 4/7" "50 1/4"
sd=$(card y2 "$TUN/bohlen-pierce-just.scl")
ratios Y2 "$sd" "61 27/25" "62 25/21" "65 75/49" "66 5/3" "73 3/1" "47 1/3"

# Y3: file picked, then the row back on EDO (list position 0): the same bytes as 7 equal steps (the Scale file row keeps the note count of the file)
sd=$(card y3 "$TUN/just-major-5limit.scl")
play y3-def "$sd" "tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300; tap F7; wait 300; tap F7; wait 800; tune 7; $(note 62)"   # the same page walk, the file never picked: 7 equal steps
play y3-back "$sd" "tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300; $(ed 12 1) $(ed 12 -1) tap F7; wait 300; tap F7; wait 800; $(note 62)"
play y3-on "$sd" "$PICK $(note 62)"
check "Y3 Scale file picked and put back to EDO renders the bytes of 7 equal steps (key 62)" "$([ "$(cksum < "$WORK/y3-def.lr")" = "$(cksum < "$WORK/y3-back.lr")" ] && echo ok || echo differ)"
check "Y3 the picked just major renders other bytes than 7 equal steps (key 62 is 5/4, not 2^(2/7))" "$([ "$(cksum < "$WORK/y3-def.lr")" != "$(cksum < "$WORK/y3-on.lr")" ] && echo ok || echo same)"

# Y4: PERC is a kit
sd=$(card y4 "$WORK/septimal5.scl")
play y4-perc "$sd" "poke synthlib engine 0 4; $PICK $(note 36)"; play y4-perc12 "$sd" "poke synthlib engine 0 4; $(note 36)"
check "Y4 PERC: key 36 renders the same bytes with a Scala tuning as without (the note picks the drum)" "$([ "$(cksum < "$WORK/y4-perc.lr")" = "$(cksum < "$WORK/y4-perc12.lr")" ] && echo ok || echo differ)"
check "Y4 PERC does sound (control)" "$(awk '$4 > 300 {n++} END {exit !(n > 20)}' "$WORK/y4-perc12.lr" && echo ok || echo silent)"

errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
check "no sanitizer report in any run" "$([ "$errs" = 0 ] && echo ok || echo "$errs reports")"
echo
if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; exit 0; fi
echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1
