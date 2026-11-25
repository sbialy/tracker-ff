#!/bin/bash
# microtune_sim.sh [EVIDENCE_DIR]
#
# Microtuning phase 1 on SAMPLE tracks, proved by the pitch that is heard (ref/microtuning-scope.md, core/source/mtTuning.h). Instrument 0 is a 441 Hz sine
# ("sampletone 0", recorded pitch on key 60, 4 s single shot) on track 1, the sim plays it with the firmware audio callback ("audio 1") and measures the
# pitch at the sample voice itself ("audiostat tap 0", zero crossings of the blocks the voice sounds in, as scale_snap_sample_sim.sh: the master WAV of
# the sim carries no sample voices). The tap reads about 0.8 % low, so a key is judged by its RATIO to the key that holds the 1/1 (1 % tolerance, one
# 31 EDO step is 2.3 %), never by an absolute Hz.
# Sections:
#   M1  12 EDO (the default, no "tune" command) is bit identical to the firmware before the tuning: the voice tap blocks of keys 0..119 hash to
#       goldens/microtune_12edo.txt (written by the plain ui_shot of release-1.4.1-int 7582dc2e: UPDATE=1 BIN=<that binary> rewrites it)
#   M2  keys 120..127 (root 127 in 12 EDO plays key 127 at the recorded pitch): the old table ended at 119 and the tune range clamped them
#   M3  19 EDO, 31 EDO and a non octave tuning (13 notes over 1901.955 cents, Bohlen-Pierce): ratio of keys to the 1/1 key = 2^(degrees * period / N / 1200)
#   M4  the 1/1 Hz field (264 Hz = x1.0091) and the 1/1 key field (root 48: key 48 plays what key 60 played)
#   M5  note names (DD.O): 31 EDO, 19 EDO, 5 notes, 13 notes, N 1, 12 stays the stock names
#   M6  sample table: the 128 entries of the default are the old formula, a tuning change refills them and Reset gives the default back
#   M9  the fine Period and 1/1 Hz rows (Project Settings > Tuning) driven through the real UI: a non preset period and Hz, the tables, project.mtff only off the default
# Host only, plain ui_shot (build/ui-shot, the build run_tests.sh makes), BIN overrides it. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
EVID="${1:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/microtune_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN:-}" ]; then
    BIN="${UI_SHOT_BIN:-$ROOT/build/ui-shot${SAN:+-san}/ui_shot}"
    [ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "microtune_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
[ -n "$EVID" ] && mkdir -p "$EVID"
GOLD="$HERE/goldens/microtune_12edo.txt"
fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
# play NAME "setup" -> $WORK/NAME.blk (the tap blocks) and NAME.out (what the script printed)
play() {
    local sd="$WORK/$1"; rm -rf "$sd"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; sampletone 0; instr 0; $2 audio 1; audiostat tap 0; audiostat 1; tap Play; wait 600; tap Play; audiostat 0; audiostat tapdump" \
        2>"$WORK/$1.err" >"$WORK/$1.raw"
    grep -E '^blk ' "$WORK/$1.raw" >"$WORK/$1.blk"; grep -vE '^blk |^audiostat blocks|^wav ' "$WORK/$1.raw" >"$WORK/$1.out"
}
# the pitch: mean zero crossings of the blocks the voice sounds in (rms > 500), the first 2 of them left out
hz_tap() { awk '$4 > 500 {v++; if (v > 2) {z += $6; n++}} END {printf "%.2f", n ? z / 2 / (n * 128 / 44117.647) : 0}' "$WORK/$1.blk"; }
# key K of the run NAME played with the settings in "$2"
key_run() { play "$1" "$2 setstep 0 0 $3 0 0 0 0 0;"; hz_tap "$1"; }
near() { awk -v a="$1" -v b="$2" -v tol="${3:-0.01}" 'BEGIN {d = a - b; if (d < 0) d = -d; exit !(b > 0 && d <= b * tol)}'; }

# M1: 12 EDO golden
tab="$WORK/m1.txt"; : > "$tab"
for k in $(seq 0 119); do
    play "m1-$k" "setstep 0 0 $k 0 0 0 0 0;"
    echo "$k $(cksum < "$WORK/m1-$k.blk" | cut -d' ' -f1)" >> "$tab"
done
if [ -n "${UPDATE:-}" ]; then cp "$tab" "$GOLD"; echo "wrote $GOLD"; fi
[ -n "$EVID" ] && cp "$tab" "$EVID/m1-12edo-keys.txt"
if [ -f "$GOLD" ] && cmp -s "$tab" "$GOLD"; then check "M1 12 EDO: the voice tap of keys 0..119 is byte identical to the firmware before the tuning (120 keys)" ok
else check "M1 12 EDO golden" "$(diff "$tab" "$GOLD" 2>&1 | head -4 | tr '\n' ' ')"; fi
bad=$(grep -c . "$WORK/m1-60.err" 2>/dev/null); check "M1 the key 60 run plays (tap has blocks)" "$([ "$(grep -c . "$WORK/m1-60.blk")" -gt 50 ] && echo ok || echo no blocks)"

# M2: keys 120..127, root 127 in 12 EDO: key K plays at 2^((K - 127) / 12) of the recorded pitch (old table: clamped to 119)
play "m2-ref" "tune 12 1200000 127; setstep 0 0 127 0 0 0 0 0;"; ref=$(hz_tap m2-ref)
for k in 120 121 122 123 124 125 126; do
    play "m2-$k" "tune 12 1200000 127; setstep 0 0 $k 0 0 0 0 0;"; t=$(hz_tap "m2-$k"); e=$(awk -v r="$ref" -v k="$k" 'BEGIN {printf "%.2f", r * 2 ^ ((k - 127) / 12)}')
    if near "$t" "$e"; then check "M2 root 127, key $k plays $e Hz" ok; else check "M2 key $k" "want $e Hz, tap $t Hz"; fi
done
# the default keeps the old behaviour (no tuning = a project as before): keys above 119 play key 119
play "m2-def119" "setstep 0 0 119 0 0 0 0 0;"; d119=$(hz_tap m2-def119)
play "m2-def127" "setstep 0 0 127 0 0 0 0 0;"; d127=$(hz_tap m2-def127)
check "M2 no tuning: key 127 still plays key 119 (the unchanged old clamp, $d127 Hz vs $d119 Hz)" "$([ "$d119" = "$d127" ] && echo ok || echo differ)"
# the default table reaches key 127 and is the formula
rates=$("$BIN" "$WORK/m2sd" "wait 200; tune reset; $(for k in 119 120 121 125 127; do printf 'tune rate %s; ' $k; done)" 2>/dev/null | grep '^tune rate')
okr=1; while read -r _ _ k v; do e=$(awk -v k="$k" 'BEGIN {printf "%.9g", 2 ^ ((k - 60) / 12) * 0.9996}'); near "$v" "$e" 0.00002 || { okr=0; echo "rate $k $v want $e"; }; done <<< "$rates"
check "M2 12 EDO table keys 119..127 are 2^((k-60)/12) x the clock correction (no read past the table)" "$([ $okr = 1 ] && echo ok || echo bad)"

# M3: tunings, ratio of the key to the 1/1 key (60)
tuned() {   # NAME LABEL "tune args" PERIODCENTS N KEYS...
    local name="$1" label="$2" args="$3" pc="$4" n="$5"; shift 5
    play "$name-ref" "tune $args; setstep 0 0 60 0 0 0 0 0;"; local ref; ref=$(hz_tap "$name-ref")
    for k in "$@"; do
        play "$name-$k" "tune $args; setstep 0 0 $k 0 0 0 0 0;"; local t; t=$(hz_tap "$name-$k")
        local e; e=$(awk -v r="$ref" -v k="$k" -v pc="$pc" -v n="$n" 'BEGIN {printf "%.2f", r * 2 ^ ((k - 60) * pc / n / 1200)}')
        [ -n "$EVID" ] && printf '%-10s key %3s want %9s Hz tap %9s Hz\n' "$name" "$k" "$e" "$t" >> "$EVID/m3-table.txt"
        if near "$t" "$e"; then check "M3 $label key $k plays $e Hz (ratio to key 60 x2^(deg*period/N))" ok; else check "M3 $label key $k" "want $e Hz, tap $t Hz (ref $ref)"; fi
    done
}
[ -n "$EVID" ] && : > "$EVID/m3-table.txt"
tuned e19 "19 EDO" "19 1200000 60 2616256" 1200 19 41 59 61 62 79 98 119
tuned e31 "31 EDO" "31 1200000 60 2616256" 1200 31 38 59 61 62 91 100 110
tuned bp "13 notes over 1901.955 cents" "13 1901955 60 2616256" 1901.955 13 50 59 61 73 86 90 94
# a period is a ratio: 19 EDO key 79 is one octave, 13 over 1901.955 key 73 is 3:1
r1=$(hz_tap e19-79); r0=$(hz_tap e19-ref); check "M3 19 EDO: 19 keys up is an octave (x2)" "$(near "$(awk -v a="$r1" -v b="$r0" 'BEGIN {print a/b}')" 2 0.006 && echo ok || echo "ratio $(awk -v a="$r1" -v b="$r0" 'BEGIN {print a/b}')")"
r1=$(hz_tap bp-73); r0=$(hz_tap bp-ref); check "M3 13ED3: 13 keys up is x3 (a tritave)" "$(near "$(awk -v a="$r1" -v b="$r0" 'BEGIN {print a/b}')" 3 0.006 && echo ok || echo "ratio $(awk -v a="$r1" -v b="$r0" 'BEGIN {print a/b}')")"

# M4: 1/1 Hz and 1/1 key
play m4-ref "setstep 0 0 60 0 0 0 0 0;"; r0=$(hz_tap m4-ref)
play m4-hz "tune 12 1200000 60 2640000; setstep 0 0 60 0 0 0 0 0;"; t=$(hz_tap m4-hz)
check "M4 1/1 Hz 264.0000 raises key 60 by 264/261.6256 (x1.00908)" "$(near "$t" "$(awk -v r="$r0" 'BEGIN {printf "%.3f", r * 264 / 261.6256}')" && echo ok || echo "ref $r0 tap $t")"
play m4-root "tune 12 1200000 48; setstep 0 0 48 0 0 0 0 0;"; t=$(hz_tap m4-root)
check "M4 1/1 key 48: key 48 plays the recorded pitch (what key 60 plays by default)" "$(near "$t" "$r0" && echo ok || echo "ref $r0 tap $t")"
play m4-root2 "tune 12 1200000 48; setstep 0 0 60 0 0 0 0 0;"; t=$(hz_tap m4-root2)
check "M4 1/1 key 48: key 60 is an octave above" "$(near "$t" "$(awk -v r="$r0" 'BEGIN {print r * 2}')" && echo ok || echo "ref $r0 tap $t")"

# M5: names
names() { "$BIN" "$WORK/m5sd" "wait 200; $1" 2>/dev/null | grep '^tune name' | awk '{printf "%s ", $4} END {print ""}'; }
declare -A WANT=(
  ["31 1200000 60 2616256|60 61 91 92 0 127"]="0.2 1.2 0.3 1.3 2.0 5.4"
  ["19 1200000 60 2616256|60 79 80 127"]="0.4 0.5 1.5 10.7"
  ["5 1200000 60 2616256|60 65 125"]="0.12 0.13 0.25"
  ["13 1901955 60 2616256|60 73 59"]="0.5 0.6 12.4"
  ["1 1200000 60 2616256|60 127"]="60 127"
  ["12 1200000 60 2616256|60 61 127"]="C5 C#5 G10"
)
for key in "${!WANT[@]}"; do
    args="${key%%|*}"; ks="${key##*|}"
    got=$(names "tune $args; $(for k in $ks; do printf 'tune name %s; ' $k; done)"); got="${got% }"
    check "M5 names, tune $args: keys $ks" "$([ "$got" = "${WANT[$key]}" ] && echo ok || echo "want '${WANT[$key]}' got '$got'")"
done

# M6: table refill and reset
rate() { "$BIN" "$WORK/m6sd" "wait 200; $1" 2>/dev/null | grep '^tune rate' | tail -1 | awk '{print $4}'; }
a=$(rate "tune rate 61"); b=$(rate "tune 31; tune rate 61"); c=$(rate "tune 31; tune reset; tune rate 61")
check "M6 a tuning refills the table (31 EDO key 61 = 2^(1/31), 12 EDO key 61 = 2^(1/12)) and reset gives the default back" "$(near "$a" 1.05890 0.0006 && near "$b" 1.02220 0.0006 && [ "$a" = "$c" ] && echo ok || echo "default $a, 31 EDO $b, reset $c")"

# M7: project.mtff round trip (the project-wide keys, written only when they differ from the default)
rt() { rm -f "$WORK/poke" "$WORK/poke.out"; UISHOT_POKE="$WORK/poke" "$BIN" "$WORK/m7sd" "$1" >"$WORK/m7.out" 2>>"$WORK/m7.err" || true; }
mtff="$WORK/m7sd/Workspace/project.mtff"
rm -rf "$WORK/m7sd"; rt "wait 200; poke projsave"
check "M7 a project with no tuning saves no tune line (a 12 EDO project has the file it had before)" "$(grep -qs '^tune\.' "$mtff" && echo "has tune lines" || echo ok)"
rt "wait 200; tune 31 1200000 48 2640000; poke projsave"
got=$(grep '^tune\.' "$mtff" 2>/dev/null | tr '\n' ' ')
check "M7 31 notes, root 48, 264 Hz: the four lines are written" "$([ "$got" = "tune.n 31 tune.root 48 tune.hz 2640000 " ] && echo ok || echo "got '$got'")"
rt "wait 200; tune; tune name 60; tune rate 61"
cfgl=$(grep '^tune n [0-9]' "$WORK/m7.out")
check "M7 the next power up reads them back (the Workspace load)" "$([ "$cfgl" = "tune n 31 period 1200000 root 48 hz 2640000" ] && echo ok || echo "got '$cfgl'")"
rt "wait 200; tune 19 1901955; poke projsave; tune reset; poke projsave"
got=$(grep '^tune\.' "$mtff" 2>/dev/null | tr '\n' ' ')
check "M7 Reset then save: the tune lines are gone again" "$([ -z "$got" ] && echo ok || echo "left '$got'")"
rt "wait 200; tune 19 1901955; poke projsave"
rt "wait 200; tune"
cfgl=$(grep '^tune n [0-9]' "$WORK/m7.out")
check "M7 period 1901955 round trips (19 notes)" "$([ "$cfgl" = "tune n 19 period 1901955 root 60 hz 2616256" ] && echo ok || echo "got '$cfgl'")"

# M8: Scale snap is off when N is not 12 (a 12 note scale has no meaning there): Penta Maj, Nearest, a written F (65) plays the snapped E (64) in 12 EDO
# and stays 65 in 31 EDO (its pitch is the one of the same key without a snap)
snapset="scale 24 18; snap 0 1;"
play m8-12snap "$snapset setstep 0 0 65 0 0 0 0 0;"; play m8-12free "setstep 0 0 65 0 0 0 0 0;"; play m8-12e "setstep 0 0 64 0 0 0 0 0;"
a=$(hz_tap m8-12snap); b=$(hz_tap m8-12free); c=$(hz_tap m8-12e)
check "M8 12 EDO: the snap moves F (65) to E (64) ($a Hz, E $c Hz, F $b Hz)" "$(near "$a" "$c" && ! near "$a" "$b" && echo ok || echo differs)"
play m8-31snap "tune 31; $snapset setstep 0 0 65 0 0 0 0 0;"; play m8-31free "tune 31; setstep 0 0 65 0 0 0 0 0;"
a=$(hz_tap m8-31snap); b=$(hz_tap m8-31free)
check "M8 31 EDO: the same Penta Maj Nearest snap does nothing, key 65 plays its own pitch ($a Hz vs $b Hz)" "$([ "$a" = "$b" ] && echo ok || echo "snapped")"

# M9: the fine Period and 1/1 Hz rows of Project Settings > Tuning, driven through the real UI (File F7, the Tuning group, one list per row). A value that is
# not a preset (1901.955 cents is one, 1900.000 cents and 264.0000 Hz are set with the digit rows). Rows: 0 Notes, 1 Period, 2 1/1 key, 3 1/1 Hz, 4..7 Period
# 100c / 1c / .01c / .001c, 8..11 Hz 100 / 1 / .01 / .0001. The compat rule: nothing new is stored, the keys are the tune.* of M7, written only off the default
ed() { # ROW DELTA: open the list of a Tuning row, move DELTA positions, apply (enc moves at most 3 a call)
    local r=$1 d=$2 t="tap Up 20; tap Down $1; tap F8; wait 200;"
    while [ "$d" -ge 3 ]; do t="$t enc 3;"; d=$((d-3)); done
    while [ "$d" -le -3 ]; do t="$t enc -3;"; d=$((d+3)); done
    [ "$d" -ne 0 ] && t="$t enc $d;"
    echo "$t tap F8; wait 200;"
}
GO="wait 500; tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300;"
ui() { rm -rf "$WORK/m9sd"; mkdir -p "$WORK/m9sd/Plugins/Master" "$WORK/m9sd/Plugins/Synth"; rm -f "$WORK/poke" "$WORK/poke.out"
       UISHOT_POKE="$WORK/poke" "$BIN" "$WORK/m9sd" "$GO $1" >"$WORK/m9.out" 2>>"$WORK/m9.err" || true; }
m9mtff="$WORK/m9sd/Workspace/project.mtff"
# 13 notes, period 19 / 1 / 95 / 5 = 1901.955, Hz 2 / 64 / 0 / 0 = 264.0000, rates of key 73 and 60 over the default key 60
ui "$(ed 0 1) $(ed 4 7) $(ed 5 1) $(ed 6 95) $(ed 7 5) $(ed 9 3) $(ed 10 -62) $(ed 11 -56) tune; tune rate 60; tune rate 73; poke projsave"
cfgl=$(grep '^tune n [0-9]' "$WORK/m9.out" | tail -1)
check "M9 the digit rows set 13 notes, period 1901.955, Hz 264.0000 (what a preset cannot do for the Hz digits)" "$([ "$cfgl" = "tune n 13 period 1901955 root 60 hz 2640000" ] && echo ok || echo "got '$cfgl'")"
r60=$(grep '^tune rate 60 ' "$WORK/m9.out" | awk '{print $4}'); r73=$(grep '^tune rate 73 ' "$WORK/m9.out" | awk '{print $4}')
check "M9 the tables follow: key 60 plays x1.0091 (264 / 261.6256), key 73 is 3:1 over key 60" "$(near "$r60" 1.00907 0.0006 && awk -v a="$r73" -v b="$r60" 'BEGIN {d = a / b - 3; exit !(d > -0.003 && d < 0.003)}' && echo ok || echo "key 60 $r60, key 73 $r73")"
got=$(grep '^tune\.' "$m9mtff" 2>/dev/null | tr '\n' ' ')
check "M9 save writes the four fields (n, period 1901955, hz 2640000), root stays default and is not written" "$([ "$got" = "tune.n 13 tune.period 1901955 tune.hz 2640000 " ] && echo ok || echo "got '$got'")"
cp "$m9mtff" "$WORK/m9keep.mtff"
rm -f "$WORK/poke" "$WORK/poke.out"; UISHOT_POKE="$WORK/poke" "$BIN" "$WORK/m9sd" "wait 200; tune" >"$WORK/m9.out" 2>>"$WORK/m9.err" || true
cfgl=$(grep '^tune n [0-9]' "$WORK/m9.out" | tail -1)
check "M9 the next power up reads the fine values back" "$([ "$cfgl" = "tune n 13 period 1901955 root 60 hz 2640000" ] && echo ok || echo "got '$cfgl'")"
# a period that is not a preset (1900.000) and a Hz that is not a preset (440.5000), nothing else off the default: only those two lines exist
ui "$(ed 4 7) $(ed 5 -1) $(ed 6 -95) $(ed 7 -0) $(ed 8 2) $(ed 9 -21) $(ed 10 -12) $(ed 11 -56) tune; poke projsave"
cfgl=$(grep '^tune n [0-9]' "$WORK/m9.out" | tail -1); got=$(grep '^tune\.' "$m9mtff" 2>/dev/null | tr '\n' ' ')
check "M9 period 1900.000 and Hz 440.5000 (12 notes): period and hz lines only ($cfgl)" "$([ "$cfgl" = "tune n 12 period 1900000 root 60 hz 4405000" ] && [ "$got" = "tune.period 1900000 tune.hz 4405000 " ] && echo ok || echo "cfg '$cfgl' lines '$got'")"
# the preset rows win when they move and the project goes back to the default: no line is left in the file
ui "$(ed 4 7) $(ed 5 1) $(ed 6 95) $(ed 7 5) $(ed 9 3) $(ed 10 -62) $(ed 11 -56) poke projsave; $(ed 1 -7) $(ed 3 -8) tune; poke projsave"
cfgl=$(grep '^tune n [0-9]' "$WORK/m9.out" | tail -1); got=$(grep '^tune\.' "$m9mtff" 2>/dev/null | tr '\n' ' ')
check "M9 the Period and 1/1 Hz presets set back: the default (2/1 1200, 261.6256) and no tune line is left in project.mtff" "$([ "$cfgl" = "tune n 12 period 1200000 root 60 hz 2616256" ] && [ -z "$got" ] && echo ok || echo "cfg '$cfgl' lines '$got'")"
# range: period digits 48 / 99 clamp to 4800.000, Hz 40 / 99 to 4000.0000 (clamped by tuning::set, the digits follow)
ui "$(ed 4 36) $(ed 5 99) tune; $(ed 8 38) $(ed 9 99) tune"
got=$(grep '^tune n [0-9]' "$WORK/m9.out" | tr '\n' '|')
check "M9 the range limits hold: 4800 cents and 4000 Hz at most" "$([ "$got" = "tune n 12 period 4800000 root 60 hz 2616256|tune n 12 period 4800000 root 60 hz 40000000|" ] && echo ok || echo "got '$got'")"

# no sanitizer report in any run
errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
check "no sanitizer report in any run" "$([ "$errs" = 0 ] && echo ok || echo "$errs reports")"
echo
if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; exit 0; fi
echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1
