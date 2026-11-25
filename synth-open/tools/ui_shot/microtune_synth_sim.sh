#!/bin/bash
# microtune_synth_sim.sh [EVIDENCE_DIR]
#
# Microtuning phase 1 on SYNTH parts (core/source/mtTuning.h, synth-open/open_synth.cpp noteOnNow): the engine's note pitch n.pitch moves by P(k) - k, 0 for the
# default. Part 1 plays ACD (a float pitch engine) on track 1, the master mix is measured by the zero crossings of the left channel ("audiostat lrdump") and
# a key is judged by its ratio to the 1/1 key (1 % tolerance, a 31 EDO step is 2.3 %).
#   S1  12 EDO (no "tune" command): the rendered blocks of 12 keys hash to goldens/microtune_synth_12edo.txt, written by the plain ui_shot of
#       release-1.4.1-int 7582dc2e (UPDATE=1 BIN=<that binary>): a project without a tuning sounds as before, to the bit
#   S2  19 EDO, 31 EDO and 13 notes over 1901.955 cents: ratio to key 60
#   S3  1/1 Hz (440 Hz is x1.682) and 1/1 key (48: key 48 plays what key 60 played)
#   S4  PERC (a kit, the note picks the drum) renders the same bytes in 31 EDO as in 12 EDO, ACD does not (the control)
# CHIP (reads the integer note only: its offset rides on the bend) is a plugin: microtune_chip_sim.sh runs it on the bridge build.
# Host only, plain ui_shot (build/ui-shot, the build run_tests.sh makes), BIN overrides it. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
EVID="${1:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/microtune_synth_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN:-}" ]; then
    BIN="${UI_SHOT_BIN:-$ROOT/build/ui-shot${SAN:+-san}/ui_shot}"
    [ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "microtune_synth_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
[ -n "$EVID" ] && mkdir -p "$EVID"
GOLD="$HERE/goldens/microtune_synth_12edo.txt"
fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
# play NAME "setup" -> $WORK/NAME.lr: the master blocks of 600 ms of the part (ACD unless the setup says otherwise)
play() {
    local sd="$WORK/$1"; rm -rf "$sd"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; instr 64; poke voicecount 0 1; $2 audio 1; audiostat 1; tap Play; wait 600; tap Play; audiostat 0; audiostat lrdump" \
        2>"$WORK/$1.err" >"$WORK/$1.raw"
    grep -E '^lr ' "$WORK/$1.raw" >"$WORK/$1.lr"
}
hz() { awk '$4 > 300 {v++; if (v > 5) {z += $8; n++}} END {printf "%.2f", n ? z / 2 / (n * 128 / 44117.647) : 0}' "$WORK/$1.lr"; }
near() { awk -v a="$1" -v b="$2" -v tol="${3:-0.01}" 'BEGIN {d = a - b; if (d < 0) d = -d; exit !(b > 0 && d <= b * tol)}'; }
note() { echo "setstep 0 0 $1 64 0 0 0 0;"; }
# the fine Tuning rows through the real UI (File F7, the Tuning group): ed ROW DELTA opens the list of a row, moves DELTA positions (enc moves at most 3 a call)
# and applies. Rows: 0 Notes, 4..7 Period 100c / 1c / .01c / .001c, 8..11 Hz 100 / 1 / .01 / .0001. 13 notes over 1900.000 cents (not a preset) and 264.0000 Hz
ed() {
    local r=$1 d=$2 t="tap Up 20; tap Down $1; tap F8; wait 200;"
    while [ "$d" -ge 3 ]; do t="$t enc 3;"; d=$((d-3)); done
    while [ "$d" -le -3 ]; do t="$t enc -3;"; d=$((d+3)); done
    [ "$d" -ne 0 ] && t="$t enc $d;"
    echo "$t tap F8; wait 200;"
}
UITUNE="tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300; $(ed 0 1) $(ed 4 7) $(ed 9 3) $(ed 10 -62) $(ed 11 -56) tap F7; wait 300; tap F7; wait 800;"

# S1
tab="$WORK/s1.txt"; : > "$tab"
for k in 0 12 24 36 48 60 61 72 84 96 108 119 127; do play "s1-$k" "$(note $k)"; echo "$k $(cksum < "$WORK/s1-$k.lr" | cut -d' ' -f1)" >> "$tab"; done
[ -n "${UPDATE:-}" ] && { cp "$tab" "$GOLD"; echo "wrote $GOLD"; }
[ -n "$EVID" ] && cp "$tab" "$EVID/s1-12edo-keys.txt"
if [ -f "$GOLD" ] && cmp -s "$tab" "$GOLD"; then check "S1 12 EDO: the master blocks of 13 keys (ACD) are byte identical to the firmware before the tuning" ok
else check "S1 12 EDO golden" "$(diff "$tab" "$GOLD" 2>&1 | head -4 | tr '\n' ' ')"; fi
check "S1 the key 60 run sounds ($(hz s1-60) Hz)" "$(near "$(hz s1-60)" 261.6 0.03 && echo ok || echo "$(hz s1-60) Hz")"

# S2
tuned() {   # NAME LABEL "tune args" PERIODCENTS N KEYS...
    local name="$1" label="$2" args="$3" pc="$4" n="$5"; shift 5
    play "$name-ref" "tune $args; $(note 60)"; local ref; ref=$(hz "$name-ref")
    for k in "$@"; do
        play "$name-$k" "tune $args; $(note $k)"; local t; t=$(hz "$name-$k")
        local e; e=$(awk -v r="$ref" -v k="$k" -v pc="$pc" -v n="$n" 'BEGIN {printf "%.2f", r * 2 ^ ((k - 60) * pc / n / 1200)}')
        [ -n "$EVID" ] && printf '%-8s key %3s want %9s Hz master %9s Hz\n' "$name" "$k" "$e" "$t" >> "$EVID/s2-table.txt"
        if near "$t" "$e"; then check "S2 $label key $k sounds $e Hz" ok; else check "S2 $label key $k" "want $e Hz, got $t Hz (ref $ref)"; fi
    done
}
[ -n "$EVID" ] && : > "$EVID/s2-table.txt"
tuned e19 "19 EDO" "19 1200000 60 2616256" 1200 19 41 50 59 61 62 79 98
tuned e31 "31 EDO" "31 1200000 60 2616256" 1200 31 38 59 61 62 91 100
tuned bp "13 notes over 1901.955 cents" "13 1901955 60 2616256" 1901.955 13 50 59 61 73 86 90

# S3
play s3-ref "$(note 60)"; r0=$(hz s3-ref)
play s3-hz "tune 12 1200000 60 4400000; $(note 60)"; t=$(hz s3-hz)
check "S3 1/1 Hz 440: key 60 is x1.6818 ($t Hz vs $r0 Hz)" "$(near "$t" "$(awk -v r="$r0" 'BEGIN {printf "%.2f", r * 440 / 261.6256}')" && echo ok || echo bad)"
play s3-root "tune 12 1200000 48; $(note 48)"; t=$(hz s3-root)
check "S3 1/1 key 48: key 48 plays what key 60 played ($t Hz vs $r0 Hz)" "$(near "$t" "$r0" && echo ok || echo bad)"
play s3-root2 "tune 12 1200000 48; $(note 60)"; t=$(hz s3-root2)
check "S3 1/1 key 48: key 60 is an octave up ($t Hz)" "$(near "$t" "$(awk -v r="$r0" 'BEGIN {print r * 2}')" && echo ok || echo bad)"
play s3-def "tune reset; $(note 61)"; play s3-def2 "$(note 61)"
check "S3 'tune reset' is the default (key 61 renders the same bytes)" "$([ "$(cksum < "$WORK/s3-def.lr")" = "$(cksum < "$WORK/s3-def2.lr")" ] && echo ok || echo differ)"

# S4: PERC (engine 4 of the built in list) is a kit
play s4-perc31 "poke synthlib engine 0 4; tune 31; $(note 36)"; play s4-perc12b "poke synthlib engine 0 4; $(note 36)"
check "S4 PERC: key 36 renders the same bytes in 31 EDO as in 12 EDO (the note picks the drum, no tuning)" "$([ "$(cksum < "$WORK/s4-perc31.lr")" = "$(cksum < "$WORK/s4-perc12b.lr")" ] && echo ok || echo differ)"
check "S4 PERC does sound (control: not silent)" "$(awk '$4 > 300 {n++} END {exit !(n > 20)}' "$WORK/s4-perc12b.lr" && echo ok || echo silent)"
check "S4 ACD control: key 61 renders other bytes in 31 EDO than in 12 EDO" "$([ "$(cksum < "$WORK/e31-61.lr")" != "$(cksum < "$WORK/s1-61.lr")" ] && echo ok || echo same)"

# S5: the same through the UI with a value no preset has: 13 notes over 1900.000 cents (the fine Period rows) and 264.0000 Hz (the fine Hz rows). Key 60 is
# x1.0091 of the default, key 73 is the 1900 cent period over it
play s5-60 "$UITUNE $(note 60)"; play s5-73 "$UITUNE $(note 73)"
t60=$(hz s5-60); t73=$(hz s5-73)
check "S5 UI fine rows, Hz 264.0000: key 60 is x1.0091 ($t60 Hz vs $r0 Hz)" "$(near "$t60" "$(awk -v r="$r0" 'BEGIN {printf "%.2f", r * 264 / 261.6256}')" && echo ok || echo bad)"
check "S5 UI fine rows, 13 notes over 1900.000 cents: key 73 is 2^(1900/1200) over key 60 ($t73 Hz over $t60 Hz)" "$(near "$t73" "$(awk -v r="$t60" 'BEGIN {printf "%.2f", r * 2 ^ (1900 / 1200)}')" && echo ok || echo bad)"

errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
check "no sanitizer report in any run" "$([ "$errs" = 0 ] && echo ok || echo "$errs reports")"
echo
if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; exit 0; fi
echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1
