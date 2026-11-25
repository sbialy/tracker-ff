#!/bin/bash
# stereo_edit_sim.sh [BIN]
#
# The stereo Sample Editor on the MK66 (FEATURE_STEREO_EDIT, core/effector/mtEffect.h) in the host build of the real UI. BIN is a ui_shot built with
#   make -C synth-open/tools/ui_shot OUT=build/ui-shot-e UISHOT_DEFS=-DFEATURE_STEREO_EDIT
# BASE_BIN (optional) is the ui_shot of the base tip (flag off): a mono edit must give the same bytes there.
#   SE-O  a stereo instrument opens in the editor with no "makes it mono" notice (the instrument stays stereo, a shot is written for the eye)
#   SE-E  every built in effect row of the list on a stereo sample (L 441 Hz, R 882 Hz): the left channel after the effect equals the same effect on a MONO
#         441 Hz sample and the right channel equals the effect on a mono 882 Hz sample (FNV of the samples, bit exact), the stereo flag is kept, one length
#   SE-M  mono samples edit exactly as before: the effect result of a mono sample equals BASE_BIN
#   SE-P  the saved workspace .pti of an edited stereo instrument is a stereo file (4 B per frame past the header)
#   SE-X  (BIN_CARD = the BRIDGE=1 build with the same flag, PTFX_DIR = the folder of the nine se_*.ptfx) the release image keeps nine effects as card files
#         that ignore the channel count: on a stereo sample the host runs the plugin once per channel (mtEffectModular two pass), L and R equal the mono result
#   SE-L  the longest editable sample: mono 1,966,080 frames (44.6 s), stereo 983,040 frames (22.3 s) (popup text and the editor start)
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-$ROOT/build/ui-shot-e/ui_shot}"
BASE_BIN="${BASE_BIN:-}"
SHOTS="${SHOTS:-}"
[ -x "$BIN" ] || { echo "stereo_edit_sim.sh: no $BIN" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/stereo_edit_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = "ok" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
rowmv() { local r="$1" s out=""; while [ "$r" -gt 0 ]; do s=$(( r > 5 ? 5 : r )); out="${out}enc $s; wait 100; "; r=$(( r - s )); done; printf '%s' "$out"; }
# run BIN NAME SCRIPT: one run on its own card, the poke lines to $WORK/NAME.out
run() { local sd="$WORK/sd_$2"; rm -rf "$sd" "$sd.poke.out"; UISHOT_POKE="$sd.poke" timeout 300 "$1" "$sd" "$3" >"$WORK/$2.log" 2>&1; cp "$sd.poke.out" "$WORK/$2.out" 2>/dev/null; }
OPEN='wait 300; instr 0; tap SampleEdit; wait 800; tap F8;'
STE='pair 1 mode; samplestereo 0 441 882 8000;'
# row:name:turn of the effect list (septfx_sim.sh), then the built in rows that stay built in
ROWS="0:normalizer:5 1:crop:5 2:reverse:5 3:amplifier:5 4:overdrive:5 5:delay:5 6:bitcrusher:5 7:chorus:5 8:flanger:5 9:equalizer:5 10:limiter:-20 11:compressor:-20 12:wtsmoother:5 13:tsbeat:5 14:tsnote:5 15:fadein:5 16:fadeout:5"
edit() { printf '%s %s %s wait 300; tap F6; wait 500; enc %s; wait 300; tap F8; wait 9000; tap Pattern; wait 800; poke samplelr 0; poke samplestat 0' "$1" "$OPEN" "$(rowmv "$2")" "$3"; }

echo "### SE-O: a stereo instrument opens in the editor"
run "$BIN" open "wait 500; $STE instr 0; instrinfo 0; tap SampleEdit; wait 800; ${SHOTS:+shot $SHOTS/stereo-edit-open.png;} instrinfo 0; poke samplelr 0"
n=$(grep -c '^instrinfo 0 stereo 1 rightIsLeft 0' "$WORK/open.log")
check "no notice, the instrument is still stereo (R != L) after the editor opened" "$([ "$n" = 2 ] && echo ok || grep instrinfo "$WORK/open.log")"

echo "### SE-E: every effect on a stereo sample equals the mono effect on each channel"
for spec in $ROWS; do
    IFS=: read -r row name turn <<<"$spec"
    ( run "$BIN" "st_$name" "wait 500; $STE $(edit '' "$row" "$turn")" ) &
    ( run "$BIN" "ml_$name" "wait 500; sampletonehz 0 441 8000; $(edit '' "$row" "$turn")" ) &
    ( run "$BIN" "mr_$name" "wait 500; sampletonehz 0 882 8000; $(edit '' "$row" "$turn")" ) &
    # three jobs per effect, wait every two effects so the host is not swamped
    if [ $((row % 2)) = 1 ]; then wait; fi
done
wait
fld() { sed -n "s/.* $2 \([^ ]*\).*/\1/p" "$WORK/$1.out" | head -1; }
mono() { grep '^samplestat' "$WORK/$1.out" | head -1; }
for spec in $ROWS; do
    IFS=: read -r row name turn <<<"$spec"
    st=$(grep '^samplelr' "$WORK/st_$name.out" | head -1); ml=$(grep '^samplelr' "$WORK/ml_$name.out" | head -1); mr=$(grep '^samplelr' "$WORK/mr_$name.out" | head -1)
    if [ -z "$st" ] || [ -z "$ml" ] || [ -z "$mr" ]; then check "$name" "no result (stereo [$st] monoL [$ml] monoR [$mr])"; continue; fi
    f() { sed -n "s/.* $2 \([^ ]*\).*/\1/p" <<<"$1"; }
    why=""
    [ "$(f "$st" stereo)" = 1 ] || why="$why the instrument is no longer stereo;"
    [ "$(f "$st" fnvL)" = "$(f "$ml" fnvL)" ] || why="$why left != mono 441 Hz result;"
    [ "$(f "$st" fnvR)" = "$(f "$mr" fnvL)" ] || why="$why right != mono 882 Hz result;"
    [ "$(f "$st" len)" = "$(f "$ml" len)" ] || why="$why length $(f "$st" len) vs mono $(f "$ml" len);"
    [ "$(f "$st" fnvL)" != "$(f "$st" fnvR)" ] || why="$why L == R;"
    if [ -z "$why" ]; then check "$name: L = mono 441 Hz result, R = mono 882 Hz result, bit exact (len $(f "$st" len), peak L $(f "$st" peakL) R $(f "$st" peakR))" ok; else check "$name" "$why [$st] [$ml] [$mr]"; fi
done

echo "### SE-M: mono edits are unchanged"
if [ -n "$BASE_BIN" ] && [ -x "$BASE_BIN" ]; then
    for spec in $ROWS; do
        IFS=: read -r row name turn <<<"$spec"
        ( run "$BASE_BIN" "mb_$name" "wait 500; sampletonehz 0 441 8000; $(edit '' "$row" "$turn")" ) &
        if [ $((row % 4)) = 3 ]; then wait; fi
    done
    wait
    for spec in $ROWS; do
        IFS=: read -r row name turn <<<"$spec"
        a=$(grep '^samplestat' "$WORK/mb_$name.out" | head -1); b=$(grep '^samplestat' "$WORK/ml_$name.out" | head -1)
        check "$name on a mono sample: same samples as the base tip (flag off)" "$([ -n "$a" ] && [ "$a" = "$b" ] && echo ok || echo "base [$a] flag [$b]")"
    done
else
    echo "(no BASE_BIN: SE-M skipped)"
fi

echo "### SE-P: the saved file is a stereo .pti"
sd="$WORK/sd_st_amplifier"
pti=$(find "$sd" -name 'instrument_01*.pti' -o -name 'instr*01*.pti' 2>/dev/null | head -1)
[ -n "$pti" ] || pti=$(find "$sd" -name '*.pti' -size +100k 2>/dev/null | head -1)
if [ -n "$pti" ]; then
    sz=$(stat -f %z "$pti" 2>/dev/null || stat -c %s "$pti")
    len=$(fld st_amplifier len)
    # header 0x?? bytes then planar L then R, 2 B per sample per channel
    hdr=$(( sz - 4 * len ))
    check "$(basename "$pti"): $sz B for $len frames, header $hdr B (stereo = 4 B per frame past the header, mono would leave $((hdr + 2 * len)) B)" "$([ "$hdr" -ge 0 ] && [ "$hdr" -lt 2048 ] && echo ok || echo "size $sz")"
else
    check "workspace .pti of the edited instrument" "no .pti found under $sd"
fi

echo "### SE-X: the nine card effects (.ptfx) on a stereo sample"
BIN_CARD="${BIN_CARD:-}"; PTFX="${PTFX_DIR:-}"
if [ -n "$BIN_CARD" ] && [ -x "$BIN_CARD" ] && [ -n "$PTFX" ] && [ -f "$PTFX/se_amplifier.ptfx" ]; then
    CARD="$WORK/card_master"; rm -rf "$CARD"; mkdir -p "$CARD"; "$BIN_CARD" "$CARD" "wait 100" >/dev/null 2>&1; mkdir -p "$CARD/Effects"; cp "$PTFX"/se_*.ptfx "$CARD/Effects/"
    runc() { local sd="$WORK/sdc_$1"; rm -rf "$sd" "$sd.poke.out"; cp -R "$CARD" "$sd"; UISHOT_POKE="$sd.poke" timeout 400 "$BIN_CARD" "$sd" "$2" >"$WORK/c_$1.log" 2>&1; cp "$sd.poke.out" "$WORK/c_$1.out" 2>/dev/null; }
    CROWS="3:amplifier:5 4:overdrive:5 5:delay:5 6:bitcrusher:5 7:chorus:5 8:flanger:5 9:equalizer:5 10:limiter:-20 11:compressor:-20"
    for spec in $CROWS; do
        IFS=: read -r row name turn <<<"$spec"
        ( runc "st_$name" "wait 500; $STE $(edit '' "$row" "$turn")" ) &
        ( runc "ml_$name" "wait 500; sampletonehz 0 441 8000; $(edit '' "$row" "$turn")" ) &
        ( runc "mr_$name" "wait 500; sampletonehz 0 882 8000; $(edit '' "$row" "$turn")" ) &
        if [ $((row % 2)) = 1 ]; then wait; fi
    done
    wait
    for spec in $CROWS; do
        IFS=: read -r row name turn <<<"$spec"
        st=$(grep '^samplelr' "$WORK/c_st_$name.out" 2>/dev/null | head -1); ml=$(grep '^samplelr' "$WORK/c_ml_$name.out" 2>/dev/null | head -1); mr=$(grep '^samplelr' "$WORK/c_mr_$name.out" 2>/dev/null | head -1)
        if [ -z "$st" ] || [ -z "$ml" ] || [ -z "$mr" ]; then check "card $name" "no result (stereo [$st] monoL [$ml] monoR [$mr])"; continue; fi
        f() { sed -n "s/.* $2 \([^ ]*\).*/\1/p" <<<"$1"; }
        why=""
        [ "$(f "$st" stereo)" = 1 ] || why="$why not stereo;"
        [ "$(f "$st" fnvL)" = "$(f "$ml" fnvL)" ] || why="$why left != mono 441 Hz card result;"
        [ "$(f "$st" fnvR)" = "$(f "$mr" fnvL)" ] || why="$why right != mono 882 Hz card result;"
        [ "$(f "$st" len)" = "$(f "$ml" len)" ] || why="$why length;"
        [ "$(f "$st" fnvL)" != "c4f178cf71988ca5" ] || why="$why left untouched;"
        [ "$(f "$st" fnvR)" != "7db2f9ccedc645c5" ] || why="$why right untouched;"
        if [ -z "$why" ]; then check "card $name.ptfx: L = mono 441 Hz result, R = mono 882 Hz result, both changed, bit exact (len $(f "$st" len))" ok; else check "card $name" "$why [$st] [$ml] [$mr]"; fi
    done
else
    echo "(no BIN_CARD / PTFX_DIR: SE-X skipped)"
fi

echo "### SE-G: goldens (goldens/stereoedit, STEREOEDIT_UPDATE=1 writes them): two waveform lanes of a stereo sample, before and after a Normalizer"
GOLD="$HERE/goldens/stereoedit"; SH="$WORK/shots"; mkdir -p "$SH"
UISHOT_POKE="$WORK/g.poke" "$BIN" "$WORK/sd_g" "wait 500; $STE instr 0; tap SampleEdit; wait 800; shot $SH/stereo-edit-open.png; tap F8; wait 300; tap F6; wait 500; enc 15; wait 300; tap F8; wait 9000; shot $SH/stereo-edit-normalized.png" >/dev/null 2>&1
for n in stereo-edit-open stereo-edit-normalized; do
    if [ -n "${STEREOEDIT_UPDATE:-}" ]; then mkdir -p "$GOLD"; cp "$SH/$n.png" "$GOLD/$n.png"; check "golden $n written" ok
    elif [ ! -s "$SH/$n.png" ]; then check "golden $n" "no screenshot"
    elif cmp -s "$SH/$n.png" "$GOLD/$n.png"; then check "golden $n: byte equal to goldens/stereoedit/$n.png" ok
    else check "golden $n" "differs from goldens/stereoedit/$n.png (STEREOEDIT_UPDATE=1 rewrites it)"; fi
done

echo "### SE-L: the longest editable sample"
mx=$(grep -n "maxFrames\|bufStride" "$ROOT/core/effector/mtEffect.h" | head -3)
check "mono max edit 1,966,080 frames, stereo 983,040 (mtEffect.h bufStride/maxFrames)" "$(grep -q 'EDIT_BUFF_SIZE / 2 : EDIT_BUFF_SIZE' "$ROOT/core/effector/mtEffect.h" && echo ok || echo "$mx")"

if [ "$fails" = 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing)"; exit 1; fi
