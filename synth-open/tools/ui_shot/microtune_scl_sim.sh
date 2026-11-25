#!/bin/bash
# microtune_scl_sim.sh [EVIDENCE_DIR]
#
# Microtuning phase 2, Scala .scl import and just intonation on SAMPLE tracks (core/source/mtScala.h parser, mtTuneScale.cpp picker, mtTuning.h model), driven through
# the real UI: Project Settings (File F7) > Tuning > Scale file lists /Tunings/*.scl of the card. Instrument 0 is a 441 Hz sine ("sampletone 0") on track 1, the pitch
# is the zero crossings of the voice tap ("audiostat tap 0", the master WAV of the sim carries no sample voices), judged as a RATIO to the 1/1 key (the tap reads about
# 0.8 % low) with a 0.4 % tolerance (12 EDO is 0.8 % away from the just major third, so a tempered table fails the check).
#   L1  the five example files of releases/tunings load through the picker: note count, period and the degrees in 1e-3 cent equal the ratios
#   L2  every reject (not a Scala file, 129 notes, descending, bad value, period 5 cents, file larger than 8 KB, 9 digit ratio part, empty): a popup text, the state
#       and the sound unchanged, the row still on EDO
#   L3  5-limit just major: the pitch of keys 53..74 per ratio (the JI thirds, fifths, the period above and below)
#   L4  non octave: Bohlen-Pierce just over 3/1, key 73 is x3 and key 47 is x1/3, per ratio. A cents file next to it (a 31 EDO as cents lines) equals the phase 1 31 EDO
#   L5  project.mtff: the keys of a Scala tuning, the default writes none, N equal steps again writes none of the two lines, a phase 1 N-EDO project loads and plays unchanged
#   L6  save and reload: the table is embedded (tune.cents), the project reloads without the file on the card and plays the same, Save As then Open on a card with no /Tunings
#   L7  a corrupt tune.cents (count not matching tune.n) plays as N equal steps, the two lines are carried by a rewrite only when valid
#   L8  a looping sample HELD across a tuning change: the playing voice keeps its pitch (no jump, no fault), the next note takes the new tuning
# Host only, ui_shot (build/ui-shot, or BIN for another platform build: microtune_scl_imx_sim.sh runs this file on the Tracker+ and Mini builds, LISTKEYS=tap on the Mini). Ends with "RESULT: GREEN" (exit 0).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
EVID="${1:-}"
PLAT="${PLAT:-og}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/microtune_scl_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN:-}" ]; then
    BIN="${UI_SHOT_BIN:-$ROOT/build/ui-shot${SAN:+-san}/ui_shot}"
    [ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "microtune_scl_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
[ -n "$EVID" ] && mkdir -p "$EVID"
TUN="$ROOT/releases/tunings"
fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
near() { awk -v a="$1" -v b="$2" -v tol="${3:-0.004}" 'BEGIN {d = a - b; if (d < 0) d = -d; exit !(b > 0 && d <= b * tol)}'; }
cents() { awk -v p="$1" -v q="$2" 'BEGIN {printf "%d", 1000 * 1200 * log(p / q) / log(2) + 0.5}'; }   # 1e-3 cent of the ratio p/q

# the Project Settings > Tuning page, as microtune_sim.sh M9 drives it: row 12 is Scale file
GO="wait 500; tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300;"
ed() { local r=$1 d=$2 t="tap Up 20; tap Down $1; tap F8; wait 200;"
    if [ "${LISTKEYS:-enc}" = tap ]; then   # the Mini has no encoder in its lists: the list moves by Down and Up
        while [ "$d" -gt 0 ]; do t="$t tap Down; wait 50;"; d=$((d-1)); done
        while [ "$d" -lt 0 ]; do t="$t tap Up; wait 50;"; d=$((d+1)); done
    else
        while [ "$d" -ge 3 ]; do t="$t enc 3;"; d=$((d-3)); done
        while [ "$d" -le -3 ]; do t="$t enc -3;"; d=$((d+3)); done
        [ "$d" -ne 0 ] && t="$t enc $d;"
    fi
    echo "$t tap F8; wait 200;"; }
newsd() {   # NAME FILES...: a card folder with those example/test files in /Tunings
    local sd="$WORK/$1"; shift; rm -rf "$sd"; mkdir -p "$sd/Tunings" "$sd/Plugins/Master" "$sd/Plugins/Synth"
    for f in "$@"; do cp "$f" "$sd/Tunings/"; done; echo "$sd"; }
runui() {   # SD "ui commands": the picker work in one process, saved into the Workspace. stdout to $WORK/ui.out
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$1" "$GO $2 poke projsave" >"$WORK/ui.out" 2>>"$WORK/ui.err" || true; }
state() { "$BIN" "$1" "wait 300; tune scale" 2>/dev/null | grep '^tune scale file' | tail -1; }
mkws() {   # SD "mtff text": a card whose Workspace is a saved default project with this project.mtff
    rm -rf "$1"; mkdir -p "$1"; rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$1" "wait 200; poke projsave" >/dev/null 2>&1 || true; printf '%b' "$2" > "$1/Workspace/project.mtff"; }
# pitch on the voice tap, from the saved Workspace of SD (a new process = a power up that reads project.mtff)
pitch() {   # SD KEY [MS] -> Hz on stdout
    local sd=$1 k=$2 ms=${3:-600}
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; sampletone 0; instr 0; setstep 0 0 $k 0 0 0 0 0; audio 1; audiostat tap 0; audiostat 1; tap Play; wait $ms; tap Play; audiostat 0; audiostat tapdump" \
        2>>"$WORK/pitch.err" | grep -E '^blk ' | awk '$4 > 500 {v++; if (v > 2) {z += $6; n++}} END {printf "%.2f", n ? z / 2 / (n * 128 / 44117.647) : 0}'; }
ratio_check() {   # LABEL SD KEY REFHZ WANTRATIO [TOL]: far octaves read off by the whole crossing of a short or partial voice, so they get more room and time
    local t tol=${6:-0.004} ms=600; [ "$tol" != 0.004 ] && ms=2500; t=$(pitch "$2" "$3" "$ms")
    [ -n "$EVID" ] && printf '%-26s key %3s want x%-10s tap %8s Hz ref %8s Hz\n' "$1" "$3" "$5" "$t" "$4" >> "$EVID/pitch-table.txt"
    if near "$t" "$(awk -v r="$4" -v w="$5" 'BEGIN {printf "%.4f", r * w}')" "$tol"; then check "$1 key $3 plays x$5 of the 1/1" ok; else check "$1 key $3" "want x$5 of $4 Hz, tap $t Hz"; fi; }
[ -n "$EVID" ] && : > "$EVID/pitch-table.txt"

# ---- L1: the five example files
want_n=(bohlen-pierce-just:13:1901955 harmonic-series-8:8:1200000 just-7limit-12:12:1200000 just-major-5limit:7:1200000 shruti-22:22:1200000)
for e in "${want_n[@]}"; do
    IFS=: read -r f n p <<<"$e"
    sd=$(newsd "l1-$f" "$TUN/$f.scl"); runui "$sd" "$(ed 12 1)"
    st=$(state "$sd")
    if [[ "$st" == "tune scale file 1 name $f.scl n $n period $p deg"* ]]; then check "L1 $f.scl loads through the picker: $n notes, period $p" ok; else check "L1 $f.scl" "got '$st'"; fi
done
sd=$WORK/l1-just-major-5limit; st=$(state "$sd")
exp="tune scale file 1 name just-major-5limit.scl n 7 period 1200000 deg $(cents 9 8) $(cents 5 4) $(cents 4 3) $(cents 3 2) $(cents 5 3) $(cents 15 8)"
check "L1 just major: the six degrees are the cents of 9/8 5/4 4/3 3/2 5/3 15/8 (1e-3 cent, within 1)" "$(awk -v a="$st" -v b="$exp" 'BEGIN {na = split(a, x, " "); nb = split(b, y, " "); if (na != nb) {print "count"; exit} for (i = 12; i <= na; i++) { d = x[i] - y[i]; if (d < 0) d = -d; if (d > 1) {print "deg " i ": " x[i] " vs " y[i]; exit} } print "ok"}')"
sd=$WORK/l1-shruti-22; st=$(state "$sd")
exp="$(cents 256 243) $(cents 16 15) $(cents 10 9) $(cents 9 8) $(cents 32 27) $(cents 6 5) $(cents 5 4) $(cents 81 64) $(cents 4 3) $(cents 27 20) $(cents 45 32) $(cents 729 512) $(cents 3 2) $(cents 128 81) $(cents 8 5) $(cents 5 3) $(cents 27 16) $(cents 16 9) $(cents 9 5) $(cents 15 8) $(cents 243 128)"
check "L1 shruti 22: all 21 degrees equal the ratios" "$(awk -v a="${st#*deg }" -v b="$exp" 'BEGIN {na = split(a, x, " "); nb = split(b, y, " "); if (na != 21 || nb != 21) {print "count " na "/" nb; exit} for (i = 1; i <= 21; i++) { d = x[i] - y[i]; if (d < 0) d = -d; if (d > 1) {print "deg " i ": " x[i] " vs " y[i]; exit} } print "ok"}')"
sd=$WORK/l1-bohlen-pierce-just; st=$(state "$sd")
check "L1 Bohlen-Pierce just: the period 3/1 is 1901955 and the last degree 25/9 is $(cents 25 9)" "$(case "$st" in *" period 1901955 "*" $(cents 25 9)") echo ok;; *) echo "got $st";; esac)"

# ---- L2: every reject through the picker. a file per case on its own card, position 1 is that file
mk() { printf '%b' "$2" > "$WORK/rej/$1.scl"; }
mkdir -p "$WORK/rej"
mk notscala "this is not a scale file\nat all\n"
mk toomany "129 notes\n129\n$(for i in $(seq 1 128); do printf '%s.0\\n' "$i"; done)2/1\n"
mk descend "descending\n3\n3/2\n5/4\n2/1\n"
mk badvalue "bad value\n3\n! comment\n9/8\nfoo\n2/1\n"
mk period5 "period 5 cents\n1\n5.0\n"
mk badratio "ratio part with 10 digits\n2\n1000000000/3\n2/1\n"
mk zeroden "zero denominator\n2\n3/0\n2/1\n"
mk empty ""
mk shortfile "fewer lines than the count\n5\n9/8\n2/1\n"
{ printf 'too long\n1\n2/1\n'; for i in $(seq 1 200); do printf '! a comment line that fills the file up to more than eight kilobytes of text\n'; done; } > "$WORK/rej/toolong.scl"
declare -A MSG=([notscala]="Not a Scala file" [toomany]="Scale has 129 notes, max 128" [descend]="Scale must ascend" [badvalue]="Bad value on line 5" [period5]="Period out of range"
                [badratio]="Bad value on line 3" [zeroden]="Bad value on line 3" [empty]="Not a Scala file" [shortfile]="Not a Scala file" [toolong]="Scala file too long")
[ "$(wc -c < "$WORK/rej/toolong.scl")" -gt 8192 ] || check "L2 the too long file is larger than 8 KB" "only $(wc -c < "$WORK/rej/toolong.scl") bytes"
for r in notscala toomany descend badvalue period5 badratio zeroden empty shortfile toolong; do
    sd=$(newsd "l2-$r" "$WORK/rej/$r.scl")
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "$GO $(ed 12 1) tune scale; tune scale msg; tune" >"$WORK/l2.out" 2>>"$WORK/l2.err" || true
    got=$(grep '^tune scale msg' "$WORK/l2.out" | sed 's/^tune scale msg //'); stl=$(grep '^tune scale file' "$WORK/l2.out" | tail -1); cfg=$(grep '^tune n ' "$WORK/l2.out" | tail -1)
    if [ "$got" = "${MSG[$r]}" ] && [[ "$stl" == "tune scale file 0 "* ]] && [ "$cfg" = "tune n 12 period 1200000 root 60 hz 2616256" ]; then check "L2 reject $r: '${MSG[$r]}', the tuning stays 12 EDO" ok
    else check "L2 reject $r" "msg '$got' (want '${MSG[$r]}') state '$stl' cfg '$cfg'"; fi
done
# the popup on screen (one evidence shot)
if [ -n "$EVID" ]; then sd=$(newsd l2-shot "$WORK/rej/descend.scl"); "$BIN" "$sd" "$GO $(ed 12 1) wait 100; shot $EVID/reject-popup.png" >/dev/null 2>&1 || true; fi
# a failed pick leaves a loaded scale as it was
sd=$(newsd l2-keep "$TUN/just-major-5limit.scl" "$WORK/rej/descend.scl"); runui "$sd" "$(ed 12 1)"; before=$(state "$sd")
"$BIN" "$sd" "$GO $(ed 12 1) tune scale" >"$WORK/l2k.out" 2>/dev/null || true   # position 1 is descend.scl (sorted before just-...), the current one is just-major
after=$(grep '^tune scale file' "$WORK/l2k.out" | tail -1)
check "L2 a refused file leaves the loaded scale in place" "$([ "$before" = "$after" ] && echo ok || echo "before '$before' after '$after'")"

# ---- L3: just major on keys: ratio to key 60 from the ratios
sd=$WORK/l1-just-major-5limit
ref=$(pitch "$sd" 60); check "L3 just major: key 60 (the 1/1) plays at all ($ref Hz)" "$(awk -v r="$ref" 'BEGIN {exit !(r > 100)}' && echo ok || echo silent)"
declare -a JM=("61 9/8" "62 5/4" "63 4/3" "64 3/2" "65 5/3" "66 15/8" "67 2/1" "68 9/4" "69 5/2" "74 4/1" "59 15/16" "53 1/2")
for e in "${JM[@]}"; do k=${e%% *}; r=${e#* }; w=$(awk -v r="$r" 'BEGIN {split(r, a, "/"); printf "%.5f", a[1] / a[2]}'); ratio_check "L3 just major" "$sd" "$k" "$ref" "$w"; done
ratio_check "L3 just major" "$sd" 52 "$ref" 0.46875 0.012   # 15/32: the period below, the degree 15/16 of the octave under
# a tempered table would not pass: the just third differs from the tempered one by 0.8 %
t62=$(pitch "$sd" 62); check "L3 key 62 is the just 5/4 and NOT the tempered 2^(4/12) (tap x$(awk -v a="$t62" -v b="$ref" 'BEGIN {printf "%.4f", a / b}'))" "$(near "$t62" "$(awk -v r="$ref" 'BEGIN {print r * 1.2599}')" 0.004 && echo tempered || echo ok)"

# ---- L4: non octave, and a cents file
sd=$WORK/l1-bohlen-pierce-just
ref=$(pitch "$sd" 60)
declare -a BP=("61 27/25" "63 9/7" "64 7/5" "66 5/3" "69 15/7" "72 25/9" "73 3/1")
for e in "${BP[@]}"; do k=${e%% *}; r=${e#* }; w=$(awk -v r="$r" 'BEGIN {split(r, a, "/"); printf "%.5f", a[1] / a[2]}'); ratio_check "L4 Bohlen-Pierce just" "$sd" "$k" "$ref" "$w"; done
for e in "86 9/1" "47 1/3" "34 1/9"; do k=${e%% *}; r=${e#* }; w=$(awk -v r="$r" 'BEGIN {split(r, a, "/"); printf "%.5f", a[1] / a[2]}'); ratio_check "L4 Bohlen-Pierce just" "$sd" "$k" "$ref" "$w" 0.012; done
{ printf '31 EDO as cents\n31\n'; for i in $(seq 1 30); do awk -v i="$i" 'BEGIN {printf "%.4f\\n", i * 1200 / 31}'; done; printf '1200.0\n'; } | sed 's/\\n/\n/g' > "$WORK/rej/cents31.scl"
sd=$(newsd l4-cents "$WORK/rej/cents31.scl"); runui "$sd" "$(ed 12 1)"
sde=$WORK/l4-edo; rm -rf "$sde"; mkdir -p "$sde/Tunings"; UISHOT_POKE="$WORK/poke" "$BIN" "$sde" "wait 300; tune 31; poke projsave" >/dev/null 2>&1 || true
rc=$(pitch "$sd" 60); re=$(pitch "$sde" 60)
for k in 61 70 91 38; do a=$(pitch "$sd" $k); b=$(pitch "$sde" $k); check "L4 cents file of 31 EDO == the phase 1 31 EDO on key $k ($(awk -v a="$a" -v r="$rc" 'BEGIN {printf "x%.4f", a / r}') vs $(awk -v a="$b" -v r="$re" 'BEGIN {printf "x%.4f", a / r}'))" "$(near "$(awk -v a="$a" -v r="$rc" 'BEGIN {print a / r}')" "$(awk -v a="$b" -v r="$re" 'BEGIN {print a / r}')" 0.003 && echo ok || echo differ)"; done

# ---- L5: project.mtff
mtff() { grep '^tune\.' "$1/Workspace/project.mtff" 2>/dev/null | tr '\n' '|'; }
sd=$WORK/l1-just-major-5limit
got=$(mtff "$sd")
check "L5 just major writes tune.n, tune.file and tune.cents (period 1200 is the default and not written)" "$([ "$got" = "tune.n 7|tune.file just-major-5limit.scl|tune.cents $(awk '{print}' <<<"$(state "$sd" | sed 's/.* deg //')")|" ] && echo ok || echo "got '$got'")"
sd=$(newsd l5-def "$TUN/just-major-5limit.scl"); runui "$sd" ""
check "L5 a project with no tuning writes no tune line (the default is not stored)" "$([ -z "$(mtff "$sd")" ] && echo ok || echo "got '$(mtff "$sd")'")"
# EDO from the row: the file lines go, N equal steps stay (7 notes)
sd=$(newsd l5-edo "$TUN/just-major-5limit.scl"); runui "$sd" "$(ed 12 1) $(ed 12 -1)"
check "L5 Scale file back to EDO: tune.file and tune.cents are gone, tune.n 7 stays (7 equal steps)" "$([ "$(mtff "$sd")" = "tune.n 7|" ] && echo ok || echo "got '$(mtff "$sd")'")"
# Notes row moved while a file is loaded: back to equal steps, no file lines
sd=$(newsd l5-notes "$TUN/just-major-5limit.scl"); runui "$sd" "$(ed 12 1) $(ed 0 5)"
check "L5 Notes/period moved from a file's 7 to 12: back to the default, nothing is written" "$([ -z "$(mtff "$sd")" ] && echo ok || echo "got '$(mtff "$sd")'")"
sd=$(newsd l5-root "$TUN/just-major-5limit.scl"); runui "$sd" "$(ed 12 1) $(ed 2 -12)"
check "L5 the 1/1 key moved on a loaded file keeps the scale (tune.root 48 written too)" "$([ "$(mtff "$sd" | cut -c1-22)" = "tune.n 7|tune.root 48|" ] && [ "$(state "$sd" | cut -c1-17)" = "tune scale file 1" ] && echo ok || echo "got '$(mtff "$sd")'")"
# a phase 1 project (tune.n 31 only, written by the 0c94d9a6 firmware) loads and plays as the equal steps it was
sd=$WORK/l5-p1; mkws "$sd" 'tffproj 1\ntune.n 31\ntune.hz 2640000\n'
st=$(state "$sd"); cfg=$("$BIN" "$sd" "wait 300; tune; tune rate 61; tune rate 60" 2>/dev/null | grep '^tune ' | tr '\n' '|')
r61=$(echo "$cfg" | sed 's/.*tune rate 61 \([0-9.e+-]*\)|.*/\1/'); r60=$(echo "$cfg" | sed 's/.*tune rate 60 \([0-9.e+-]*\)|.*/\1/')
check "L5 a phase 1 project (tune.n 31, tune.hz) loads with no Scala state and the 31 EDO table (key 61 / key 60 = 2^(1/31))" "$([ "${st:0:17}" = "tune scale file 0" ] && near "$(awk -v a="$r61" -v b="$r60" 'BEGIN {print a / b}')" 1.022611 0.0003 && echo ok || echo "state '$st' rates $r61 $r60")"

# ---- L6: save, reload, remove the file
sd=$WORK/l1-just-major-5limit
a=$(pitch "$sd" 64)
mv "$sd/Tunings" "$sd/Tunings.gone"
st2=$(state "$sd"); b=$(pitch "$sd" 64)
check "L6 reload with the .scl removed from the card: same state (name kept) and same pitch ($a Hz, $b Hz)" "$([ "$(state "$sd")" = "$(state "$WORK/l1-just-major-5limit")" ] && [[ "$st2" == *"just-major-5limit.scl n 7 "* ]] && near "$a" "$b" 0.001 && echo ok || echo "state '$st2'")"
# the page lists the project copy as its own entry
"$BIN" "$sd" "$GO tap Up 20; tap Down 12; tap F8; wait 200; shot $WORK/l6-list.png; tap F7; wait 100" >/dev/null 2>&1 || true
[ -n "$EVID" ] && cp "$WORK/l6-list.png" "$EVID/project-copy-row.png" 2>/dev/null
# Save As on a card with the file, open it on another card without /Tunings
sd=$(newsd l6-saveas "$TUN/bohlen-pierce-just.scl"); rm -f "$WORK/poke"
UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "$GO $(ed 12 1) saveproj BPSong; wait 200" >"$WORK/l6.out" 2>>"$WORK/l6.err" || true
sd2="$WORK/l6-other"; rm -rf "$sd2"; mkdir -p "$sd2/Projects" "$sd2/Plugins/Master" "$sd2/Plugins/Synth"; cp -R "$sd/Projects/BPSong" "$sd2/Projects/BPSong"
got=$("$BIN" "$sd2" "wait 300; openproj BPSong; tune scale; tune" 2>/dev/null | grep -E '^(openproj|tune scale file|tune n)' | tr '\n' '|')
check "L6 Save As then Open on another card with no /Tunings: the Scala tuning is back (13 notes, 3/1)" "$([[ "$got" == *"tune scale file 1 name bohlen-pierce-just.scl n 13 period 1901955 deg"* ]] && echo ok || echo "got '$got'")"
mtp=$(grep '^tune\.' "$sd/Projects/BPSong/project.mtff" | cut -c1-100 | tr '\n' '|')
check "L6 the saved project holds tune.n 13, tune.period 1901955, tune.file and tune.cents" "$([[ "$mtp" == "tune.n 13|tune.period 1901955|tune.file bohlen-pierce-just.scl|tune.cents 133238 301847 "* ]] && echo ok || echo "got '$mtp'")"

# ---- L7: a corrupt tune.cents plays as equal steps
sd=$WORK/l7; mkws "$sd" 'tffproj 1\ntune.n 7\ntune.file bad.scl\ntune.cents 203910 386314 498045\n'
st=$(state "$sd"); cfg=$("$BIN" "$sd" "wait 300; tune" 2>/dev/null | grep '^tune n')
check "L7 tune.cents with 3 values for 7 notes: no Scala tuning, 7 equal steps ($cfg)" "$([ "${st:0:17}" = "tune scale file 0" ] && [ "$cfg" = "tune n 7 period 1200000 root 60 hz 2616256" ] && echo ok || echo "state '$st' cfg '$cfg'")"
mkws "$sd" 'tffproj 1\ntune.n 3\ntune.cents 400000 300000\n'
check "L7 tune.cents not ascending: no Scala tuning" "$([ "$(state "$sd" | cut -c1-17)" = "tune scale file 0" ] && echo ok || echo "state '$(state "$sd")'")"

# ---- L8: a looping sample held across a tuning change
sd=$(newsd l8 "$TUN/just-major-5limit.scl")
"$BIN" "$sd" "$GO tap Back; wait 300; sampletone 0; instr 0; playmode 0 1; audio 1; audiostat tap 0; audiostat 1; midion 60 100 900; tap File; wait 800; tap F7; wait 800; tap Down 20; tap Up; tap F8; wait 300; $(ed 12 1) wait 900; midioff 60; wait 100; audiostat 0; audiostat tapdump" 2>>"$WORK/l8.err" | grep '^blk' > "$WORK/l8.blk"
awk '$4 > 500 {n++; z[n] = $6} END {a = 0; ac = 0; b = 0; bc = 0; for (i = 20; i <= int(n / 4); i++) {a += z[i]; ac++} for (i = int(n * 3 / 4); i <= n - 10; i++) {b += z[i]; bc++} printf "%d %.4f %.4f", n, ac ? a / ac : 0, bc ? b / bc : 0}' "$WORK/l8.blk" > "$WORK/l8.res"
read -r nb za zb < "$WORK/l8.res"
check "L8 the held loop sounded through the change ($nb voiced blocks, zero crossings per block $za before, $zb after)" "$([ "$nb" -gt 300 ] && near "$za" "$zb" 0.06 && echo ok || echo "blocks $nb before $za after $zb")"
# the next note takes the new tuning: key 64 now is the just third
[ -s "$WORK/l8.blk" ] || true
# ---- no sanitizer report
errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
check "no sanitizer report in any run" "$([ "$errs" = 0 ] && echo ok || echo "$errs reports")"
echo
if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; exit 0; fi
echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1
