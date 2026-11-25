#!/bin/bash
# septfx_sim.sh [WORKDIR]
#
# The nine Sample Editor effects the 1.4.0 release image keeps on the card (/Effects/se_*.ptfx, -DFW_SE_PTFX, core/effector/ptfx_manager.cpp) in the
# host build of the real UI (ui_shot, nothing here touches a device). Two binaries run the same scripts:
#   stock  make -C synth-open/tools/ui_shot                  the effects built in, as the stock image has them (the reference)
#   card   make -C synth-open/tools/ui_shot BRIDGE=1 ...     -DFW_SE_PTFX, the effects are card files: the emulator bridge build (bridge_env.sh, emu parity
#                                                            T6) runs the Thumb code of each .ptfx under Unicorn behind the real loader (header, size
#                                                            checks, missing rows, parameter panel, mtEffectModular and the editor engine are the real code)
# So SE1 compares the card files' own bytes with the built in effects of the stock build (before T10 a native twin of the C source stood in).
#
#   SE1  every moved effect (rows 3 to 11), first parameter turned, Apply (9 s: the host holds an apply the unit time of its row, kUnitApplyMs in gen_overlay.py, Equalizer 8000), leave the editor: the instrument's sample (length, peak, sum, FNV of every
#        sample) of the card build equals the stock build, and it is not the untouched sample. Amplifier is also checked against its arithmetic
#   SE2  a card with all nine files: the effect list rows of the card build equal the stock build's rows (no Missing row), every row applies
#   SE3  a card without /Effects files: the nine rows read "Missing: <name>", the other rows are the same, Apply is off on a Missing row
#   SE4  each of the nine Missing rows: Select effect, turn the encoder, Apply, leave: nothing changes in the sample, no crash (exit 0)
#   SE5  the same card: Normalizer (built in, row 0) still applies
#   SE6  one file removed (se_delay.ptfx): only Delay reads Missing, the other eight rows load and apply
#   SE7  damaged files: bad magic, cut inside the header and empty read Missing. A whole header with the payload cut off is listed by name (the list reads
#        headers only) and fails when selected: Apply off, sample untouched. No crash, the intact files still load
#   SE8  the stock build on a card without files: no Missing row (the stock image keeps the effects built in)
#
# Needs the nine files (PTFX_DIR, the pinned set that run_tests.sh fetches, else build/tests/modules) and what bridge_env.sh finds (the pinned
# Unicorn wheel, the release axf). Without the files the script says SKIP and exits 0. BIN_STOCK / BIN_CARD override the binaries.
# SAN=address,undefined: sanitizer builds.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/septfx_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"

NAMES="amplifier overdrive delay bitcrusher chorus flanger equalizer limiter compressor"
PTFX="${PTFX_DIR:-}"
if [ -z "$PTFX" ] && [ -f "$ROOT/build/tests/modules/se_amplifier.ptfx" ]; then PTFX="$ROOT/build/tests/modules"; fi
if [ -z "$PTFX" ]; then
    echo "septfx_sim.sh: SKIP (no folder with the nine se_*.ptfx: PTFX_DIR)"
    exit 0
fi
for n in $NAMES; do [ -f "$PTFX/se_$n.ptfx" ] || { echo "septfx_sim.sh: SKIP (no $PTFX/se_$n.ptfx)"; exit 0; }; done

JOBS="$(sh "$HERE/../jobs.sh" n)"
BIN_STOCK="${BIN_STOCK:-${UI_SHOT_BIN:-}}"             # UI_SHOT_BIN: the prebuilt stock flavour, UI_SHOT_BIN_BRIDGE: the BRIDGE=1 one, no make
BIN_CARD="${BIN_CARD:-${UI_SHOT_BIN_BRIDGE:-}}"
if [ -z "${BIN_STOCK:-}" ]; then
    BIN_STOCK="$ROOT/build/ui-shot${SAN:+-san}/ui_shot"
    make -s -C "$HERE" -j"$JOBS" ${SAN:+SAN=$SAN} >"$WORK/make_stock.log" 2>&1 || { tail -30 "$WORK/make_stock.log"; exit 2; }
fi
if [ -z "${BIN_CARD:-}" ]; then
    . "$HERE/bridge_env.sh"
    bridge_env "$ROOT" || exit 2
    BIN_CARD="$ROOT/build/ui-shot-bridge${SAN:+-san}/ui_shot"
    make -s -C "$HERE" -j"$JOBS" "${BRIDGE_ARGS[@]}" OUT="${BIN_CARD%/ui_shot}" ${SAN:+SAN=$SAN} >"$WORK/make_card.log" 2>&1 || { tail -30 "$WORK/make_card.log"; exit 2; }
fi
[ -x "$BIN_STOCK" ] && [ -x "$BIN_CARD" ] || { echo "septfx_sim.sh: no ui_shot at $BIN_STOCK or $BIN_CARD" >&2; exit 99; }

fails=0
ok()   { printf 'ok    %s\n' "$1"; }
fail() { fails=$((fails + 1)); printf 'FAIL  %s\n' "$1"; }

# a primed card (one power up creates the folders), with the .ptfx files named in $2 (default all nine)
card() {
    local bin="$1" sd="$2" only="${3-$NAMES}" n
    rm -rf "$sd"; mkdir -p "$sd"
    "$bin" "$sd" "wait 100" >/dev/null 2>&1 || return 1
    mkdir -p "$sd/Effects"
    for n in $only; do cp "$PTFX/se_$n.ptfx" "$sd/Effects/"; done
}

# run a script, print what its poke lines wrote. BIN SD SCRIPT. The exit status is the binary's (a crash is 139 or 134)
run() {
    local bin="$1" sd="$2" script="$3" rc
    rm -f "$sd.poke.out"
    UISHOT_POKE="$sd.poke" timeout 240 "$bin" "$sd" "$script" >"$sd.log" 2>&1
    rc=$?
    cat "$sd.poke.out" 2>/dev/null
    return $rc
}

# the encoder moves the Effect list by at most 5 rows per turn
rowmv() { local r="$1" s out=""; while [ "$r" -gt 0 ]; do s=$(( r > 5 ? 5 : r )); out="${out}enc $s; wait 100; "; r=$(( r - s )); done; printf '%s' "$out"; }
OPEN='wait 300; sampletone 0; instr 0; tap SampleEdit; wait 800; tap F8;'
# select row $1, Select effect, turn the first parameter by $2, Apply, leave the editor, print the sample
apply_row() { printf '%s %s wait 300; poke seeffects; tap F6; wait 500; enc %s; wait 300; tap F8; wait 9000; tap Pattern; wait 500; poke samplestat 0' "$OPEN" "$(rowmv "$1")" "$2"; }
stat_of()   { grep '^samplestat' | head -1; }
field()     { sed -n "s/.* $1 \([^ ]*\).*/\1/p" <<<"$2"; }   # field NAME "samplestat 0 len L peak P ..."
ORIG="samplestat 0 len 176400 peak 12000 sum 0 fnv a1547f2d80ecfe65"   # the 4 s sine of `sampletone`, untouched

# effect row, first parameter turn (the limiter and the compressor threshold go down: a -6 / -12 dB threshold does not touch a sine at 12000)
ROWS="3:amplifier:5 4:overdrive:5 5:delay:5 6:bitcrusher:5 7:chorus:5 8:flanger:5 9:equalizer:5 10:limiter:-20 11:compressor:-20"
MOVED="Amplifier Overdrive Delay Bitcrusher Chorus Flanger Equalizer Limiter Compressor"

card "$BIN_STOCK" "$WORK/sd_stock" || { echo "no card"; exit 2; }
card "$BIN_CARD" "$WORK/sd_card" || { echo "no card"; exit 2; }

# SE1
for spec in $ROWS; do
    IFS=: read -r row name turn <<<"$spec"
    a="$(run "$BIN_STOCK" "$WORK/sd_stock" "$(apply_row "$row" "$turn")" | stat_of)"
    b="$(run "$BIN_CARD" "$WORK/sd_card" "$(apply_row "$row" "$turn")" | stat_of)"
    if [ -z "$a" ] || [ -z "$b" ]; then fail "SE1 $name: no result (stock '$a' card '$b')"
    elif [ "$a" != "$b" ]; then fail "SE1 $name: card build differs from the stock build: stock [$a] card [$b]"
    elif [ "$b" = "$ORIG" ]; then fail "SE1 $name: Apply changed nothing [$b]"
    else ok "SE1 $name: Apply on the card build equals the stock build ($(field len "$b") samples, peak $(field peak "$b"))"; fi
done
b="$(run "$BIN_CARD" "$WORK/sd_card" "$(apply_row 3 20)" | stat_of)"
[ "$(field peak "$b")" = "14400" ] && [ "$(field len "$b")" = "176400" ] && ok "SE1 Amplifier x1.20 of a 12000 peak is 14400 (the card file's own arithmetic)" || fail "SE1 Amplifier x1.20: [$b]"
b="$(run "$BIN_CARD" "$WORK/sd_card" "$(apply_row 5 5)" | stat_of)"
[ "$(field len "$b")" -gt 176400 ] 2>/dev/null && ok "SE1 Delay appends its tail (length $(field len "$b") > 176400)" || fail "SE1 Delay length: [$b]"

# SE2
rows_of() { grep '^seeffects' | head -1; }
# (the stock build lists every .ptfx of /Effects as one more row, so its reference card has none: the built in rows are all it has)
card "$BIN_STOCK" "$WORK/sd_stockref" "" || exit 2
a="$(run "$BIN_STOCK" "$WORK/sd_stockref" "$OPEN wait 300; poke seeffects" | rows_of)"
b="$(run "$BIN_CARD" "$WORK/sd_card" "$OPEN wait 300; poke seeffects" | rows_of)"
if [ -n "$a" ] && [ "$a" = "$b" ] && ! grep -q Missing <<<"$b"; then ok "SE2 card with all nine files: the list is the stock list, no Missing row"; else fail "SE2 rows: stock [$a] card [$b]"; fi
grep -q "apply 1" <<<"$b" && ok "SE2 Apply is on for the selected row" || fail "SE2 apply flag: [$b]"

# SE3
card "$BIN_CARD" "$WORK/sd_none" "" || exit 2
miss=""; for n in $MOVED; do miss="${miss}${miss:+|}Missing: $n"; done
want="[Normalizer|Crop|Reverse|$miss|WT Smoother|Timestretch Beat|Timestretch Note|Fade In|Fade Out]"
b="$(run "$BIN_CARD" "$WORK/sd_none" "$OPEN $(rowmv 3) wait 300; poke seeffects" | rows_of)"
if [[ "$b" == *"$want" ]]; then ok "SE3 no files: nine Missing: <name> rows in place, the other rows unchanged"; else fail "SE3 rows [$b]"; fi
grep -q "sel 3 apply 0" <<<"$b" && ok "SE3 Apply is off on a Missing row" || fail "SE3 apply flag [$b]"

# SE4
for spec in $ROWS; do
    IFS=: read -r row name turn <<<"$spec"
    out="$(run "$BIN_CARD" "$WORK/sd_none" "$(apply_row "$row" "$turn")")"; rc=$?
    s="$(stat_of <<<"$out")"
    if [ $rc -ne 0 ]; then fail "SE4 $name missing: ui_shot exit $rc (crash?)"
    elif [ "$s" != "$ORIG" ]; then fail "SE4 $name missing: the sample changed [$s]"
    else ok "SE4 $name missing: Select effect, encoder and Apply do nothing, sample untouched, no crash"; fi
done

# SE5
b="$(run "$BIN_CARD" "$WORK/sd_none" "$OPEN wait 300; tap F6; wait 500; enc 5; wait 300; tap F8; wait 9000; tap Pattern; wait 500; poke samplestat 0" | stat_of)"
[ "$(field peak "$b")" = "32767" ] && ok "SE5 Normalizer (built in) still applies on the card without files" || fail "SE5 Normalizer [$b]"

# SE6
card "$BIN_CARD" "$WORK/sd_nodelay" "amplifier overdrive bitcrusher chorus flanger equalizer limiter compressor" || exit 2
b="$(run "$BIN_CARD" "$WORK/sd_nodelay" "$OPEN wait 300; poke seeffects" | rows_of)"
[[ "$b" == *"|Overdrive|Missing: Delay|Bitcrusher|"* ]] && [[ "$b" != *"Missing: Amplifier"* ]] && ok "SE6 se_delay.ptfx removed: only Delay reads Missing" || fail "SE6 rows [$b]"
b="$(run "$BIN_CARD" "$WORK/sd_nodelay" "$(apply_row 6 5)" | stat_of)"
a="$(run "$BIN_STOCK" "$WORK/sd_stock" "$(apply_row 6 5)" | stat_of)"
[ -n "$b" ] && [ "$a" = "$b" ] && ok "SE6 Bitcrusher still applies as on the stock build" || fail "SE6 Bitcrusher [$b] stock [$a]"

# SE7
card "$BIN_CARD" "$WORK/sd_bad" "$NAMES" || exit 2
printf 'JUNKJUNK' | dd of="$WORK/sd_bad/Effects/se_amplifier.ptfx" bs=1 count=8 conv=notrunc 2>/dev/null   # bad magic
head -c 100 "$PTFX/se_chorus.ptfx" > "$WORK/sd_bad/Effects/se_chorus.ptfx"                            # cut short, inside the header
: > "$WORK/sd_bad/Effects/se_overdrive.ptfx"                                                           # empty
head -c 600 "$PTFX/se_limiter.ptfx" > "$WORK/sd_bad/Effects/se_limiter.ptfx"                           # header whole, code cut off
out="$(run "$BIN_CARD" "$WORK/sd_bad" "$OPEN wait 300; poke seeffects")"; rc=$?
b="$(rows_of <<<"$out")"
if [ $rc -eq 0 ] && [[ "$b" == *"|Missing: Amplifier|Missing: Overdrive|Delay|"* ]] && [[ "$b" == *"|Missing: Chorus|"* ]]; then ok "SE7 bad magic, header cut and empty files read Missing, the intact ones are listed (a file with a whole header and a cut payload is listed, the load fails)"; else fail "SE7 rows (exit $rc) [$b]"; fi
for spec in "3:Amplifier:5" "7:Chorus:5" "10:Limiter:-20"; do
    IFS=: read -r row name turn <<<"$spec"
    out="$(run "$BIN_CARD" "$WORK/sd_bad" "$(apply_row "$row" "$turn")")"; rc=$?
    s="$(stat_of <<<"$out")"
    [ $rc -eq 0 ] && [ "$s" = "$ORIG" ] && ok "SE7 damaged $name: Apply does nothing, no crash" || fail "SE7 damaged $name (exit $rc) [$s]"
done
# a damaged file next to intact ones must not stop them
b="$(run "$BIN_CARD" "$WORK/sd_bad" "$(apply_row 5 5)" | stat_of)"
a="$(run "$BIN_STOCK" "$WORK/sd_stock" "$(apply_row 5 5)" | stat_of)"
[ -n "$b" ] && [ "$a" = "$b" ] && ok "SE7 Delay next to the damaged files still equals the stock build" || fail "SE7 Delay [$b] stock [$a]"

# SE8
card "$BIN_STOCK" "$WORK/sd_stocknone" "" || exit 2
b="$(run "$BIN_STOCK" "$WORK/sd_stocknone" "$OPEN wait 300; poke seeffects" | rows_of)"
[ -n "$b" ] && [[ "$b" != *Missing* ]] && ok "SE8 stock build, no files: no Missing row" || fail "SE8 rows [$b]"

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
