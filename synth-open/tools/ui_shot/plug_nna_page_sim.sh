#!/bin/bash
# plug_nna_page_sim.sh: the New note row of the Synth page on Tracker+ and Mini (plugin engine parts only), through the real UI, sequencer and
# closed libPolySynth of the plus or mini sim. PRODUCT: plus (default) or mini. PLG_DIR: the .plg files (alco.plg). BIN: a ui_shot to run instead
# of building one (a mutant build). OGBIN: the original Tracker ui_shot (build/ui-shot/ui_shot) for the project file rows, built when missing.
# DEVTOOLS_DIR: the console checkout the plus and mini sims build with.
#   N1  ALCO (plugin) part: Right x6 reaches the New note place, four Ups step Release, Continue, Cut, Fade and stay on Fade, four Downs back to Release
#   N2  ACD (a stock engine): the row is not drawn (no ink in its area), Right x7 stops on the Delay column (the screen equals Right x5), Up leaves the part's New note alone
#   N3  the row follows the engine: ALCO then ACD on one part shows the page of a part that was always ACD (row gone), ACD then ALCO brings it back
#   N7  a plugin part shows the block when the page is entered again (the project open case), not only after the pick
#   N4  the stored value is the original Tracker's: the pair the page writes (raw bytes) == the OG sim's, a project saved here opens as that mode in
#       the OG sim and a project saved by the OG sim opens as that mode here (saved project.mt files)
#   N5  every value changes the rendered overlap like the OG does: Cut set on the page == "poke nna 0 2" (bit equal), Fade == poke 3, Continue == poke 1,
#       and the three differ from Release and from each other
#   N6  the NNA step FX still overrides: a part set to Continue on the page, a Cut FX on step 1 == the note alone (as Cut), not the Continue render
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PRODUCT="${PRODUCT:-plus}"
PLG_DIR="${PLG_DIR:?PLG_DIR=<dir with alco.plg>}"
if [ -z "${BIN:-}" ] && [ -z "${DEVTOOLS_DIR:-}" ]; then
  for d in "/Volumes/StoreCI/prj/poly-devtools/.wt/rel-200-int" "$HOME/prj/poly-devtools/.wt/rel-200-int" "$HOME/prj/poly-devtools/.wt/main" "$HOME/prj/poly-devtools"; do
    grep -q UISHOT_PLUS "$d/firmware/include/UsbTest.h" 2>/dev/null && { DEVTOOLS_DIR="$d"; break; }
  done
  [ -n "${DEVTOOLS_DIR:-}" ] || { echo "plug_nna_page_sim: SKIP (no devtools checkout with the UISHOT_PLUS guards, set DEVTOOLS_DIR)"; exit 0; }
fi
W="${OUT:-$ROOT/build/plug_nna_page_$PRODUCT}"
rm -rf "$W"; mkdir -p "$W/card/Plugins/Synth"
cp "$PLG_DIR/alco.plg" "$W/card/Plugins/Synth/" || exit 2
if [ -z "${BIN:-}" ]; then
  . "$HERE/bridge_env.sh"
  bridge_env "$ROOT" > "$W/bridge_env.log" 2>&1 || { echo "bridge_env failed ($W/bridge_env.log)"; exit 2; }
  P=(); B="$ROOT/build/ui-shot-plus-console-bridge"
  [ "$PRODUCT" = mini ] && { P=(PRODUCT=MINI); B="$ROOT/build/ui-shot-mini-console-bridge"; }
  make -C "$HERE" -j"${JOBS:-8}" PLATFORM=IMX "${P[@]}" CONSOLE=1 "${BRIDGE_ARGS[@]}" DEVTOOLS_DIR="${DEVTOOLS_DIR:-}" OUT="$B" > "$W/make.log" 2>&1 \
    || { echo "build failed ($W/make.log)"; exit 2; }
  BIN="$B/ui_shot"
fi
if [ -z "${OGBIN:-}" ]; then
  OGBIN="$ROOT/build/ui-shot/ui_shot"
  [ -x "$OGBIN" ] || make -s -C "$HERE" -j"${JOBS:-8}" > "$W/make_og.log" 2>&1 || { echo "OG build failed ($W/make_og.log)"; exit 2; }
fi
PY="${PY:-$HOME/prj/poly/build/pyenv/bin/python3}"
pass=0; fail=0
row() { if [ "$2" = 0 ]; then echo "PASS $1"; pass=$((pass + 1)); else echo "FAIL $1"; fail=$((fail + 1)); fi; }
same() { [ -s "$W/$1" ] && cmp -s "$W/$1" "$W/$2"; }
win() { "$PY" -I - "$@" <<'PYEOF'
import sys, wave, numpy as np
def rd(n):
    w = wave.open(n); return np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(float).reshape(-1, 2)
op = sys.argv[1]
if op == 'onset':
    a = rd(sys.argv[2]); nz = np.nonzero(np.abs(a).max(axis=1))[0]; print((int(nz[0]) // 128) * 128 if len(nz) else -1)
else:               # from A B FRAME: equal from FRAME on
    a, b, f = rd(sys.argv[2]), rd(sys.argv[3]), int(sys.argv[4]); sys.exit(0 if len(a) == len(b) and np.array_equal(a[f:], b[f:]) else 1)
PYEOF
}
# run NAME ENGINE "SCRIPT": the engine on part 1 (instr 64), the Synth page open, SCRIPT, output in $W/NAME.log and $W/poke_NAME.out
run() {
  rm -rf "$W/card_$1"; cp -R "$W/card" "$W/card_$1"
  local s="wait 500; instr 64; tap Params; wait 1500; pickmodel $2; wait 300; $3"
  (cd "$W" && UISHOT_POKE="$W/poke_$1" ASAN_OPTIONS=detect_leaks=0 timeout 240 "$BIN" "$W/card_$1" "$s" > "$W/$1.log" 2>&1)
  grep -q "^pickmodel $2 ok" "$W/$1.log" || echo "  $1: pickmodel $2 did not pass ($W/$1.log)"
}
pokes() { grep "^nna 0 " "$W/poke_$1.out" | awk '{printf "%s%s", (NR > 1 ? " " : ""), $4}'; }

# ---- N1: the row on a plugin part
run n1 ALCO "tap Right 6; wait 300; shot $W/n1_place.png; poke nna 0; tap Up; wait 200; poke nna 0; tap Up; wait 200; poke nna 0; tap Up; wait 200; poke nna 0; tap Up; wait 200; poke nna 0; tap Down; tap Down; tap Down; tap Down; wait 200; poke nna 0" &
# ---- N2 / N3: a stock engine, and the row following the engine
run n2a ACD "tap Right 7; wait 300; shot $W/n2_r7.png; tap Up; wait 200; poke nna 0" &
run n2b ACD "tap Right 5; wait 300; shot $W/n2_r5.png" &
run n3a ALCO "pickmodel ACD; wait 400; tap Right 6; wait 300; shot $W/n3_acd.png; poke nna 0" &
run n3b ACD "tap Right 6; wait 300; shot $W/n3_ref.png" &
run n7 ALCO "tap Pattern; wait 400; tap Params; wait 1500; tap Right 6; wait 300; shot $W/n7.png" &
wait
run n3c ACD "pickmodel ALCO; wait 400; tap Right 6; wait 300; shot $W/n3_alco.png" &
run n3d ALCO "tap Right 6; wait 300; shot $W/n3_alco_ref.png" &
wait
[ "$(pokes n1)" = "0 1 2 3 3 0" ] && [ -s "$W/n1_place.png" ]; row "N1 $PRODUCT alco: Right x6 reaches New note, Up walks Release Continue Cut Fade and stops on Fade, Downs back (got '$(pokes n1)')" $?
# the New note block is drawn at x 625..795, y 243..273: its text is the brightest thing there (max channel value, the background is dark)
ink() { "$PY" -I - "$W/$1" <<'PYEOF'
import sys, zlib, struct
d = open(sys.argv[1], 'rb').read(); pos = 8; idat = b''
while pos < len(d):
    n, t = struct.unpack('>I4s', d[pos:pos + 8]); body = d[pos + 8:pos + 8 + n]; pos += 12 + n
    if t == b'IHDR': w, h, bd, ct = struct.unpack('>IIBB', body[:10]); assert bd == 8 and ct == 2
    elif t == b'IDAT': idat += body
raw = zlib.decompress(idat); bpp = 3; stride = w * bpp; prev = bytearray(stride); rows = []; i = 0
for y in range(h):
    f = raw[i]; line = bytearray(raw[i + 1:i + 1 + stride]); i += 1 + stride
    for x in range(stride):
        a = line[x - bpp] if x >= bpp else 0; b = prev[x]; c = prev[x - bpp] if x >= bpp else 0
        if f == 1: line[x] = (line[x] + a) & 255
        elif f == 2: line[x] = (line[x] + b) & 255
        elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
        elif f == 4:
            p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
    rows.append(line); prev = line
print(max(max(rows[y][625 * 3:795 * 3]) for y in range(245, 272)))
PYEOF
}
# ink > 60: the block is drawn, < 60: it is not (checked first, the row message must not reset $?)
I_N7=$(ink n7.png); I_ALCO=$(ink n1_place.png); I_ACD=$(ink n3_ref.png); I_A2D=$(ink n3_acd.png); I_D2A=$(ink n3_alco.png); I_ALCO2=$(ink n3_alco_ref.png)
N1V=$(pokes n1); N2V=$(pokes n2a)
[ "$N1V" = "0 1 2 3 3 0" ] && [ "${I_ALCO:-0}" -gt 60 ]; row "N1 $PRODUCT alco: the block is drawn (ink $I_ALCO), Right x6 reaches it, four Ups step Release Continue Cut Fade and stay on Fade, Downs back (got '$N1V')" $?
same n2_r7.png n2_r5.png && [ "$N2V" = "0" ] && [ "${I_ACD:-255}" -lt 60 ]; row "N2 $PRODUCT acd: the block is not drawn (ink $I_ACD), Right x7 stays on the Delay column (== Right x5), Up leaves nna at Release (got '$N2V')" $?
same n3_acd.png n3_ref.png && same n3_alco.png n3_alco_ref.png && [ "${I_A2D:-255}" -lt 60 ] && [ "${I_D2A:-0}" -gt 60 ] && [ "${I_ALCO2:-0}" -gt 60 ]
row "N3 $PRODUCT the block follows the engine: ALCO then ACD loses it (ink $I_A2D), ACD then ALCO gets it (ink $I_D2A), both equal a part that was picked once" $?

[ "${I_N7:-0}" -gt 60 ]; row "N7 $PRODUCT a plugin part shows the block when the page is opened again (ink $I_N7), not only after the pick" $?

# ---- N4: the saved file is the OG's
rm -rf "$W/p_plus" "$W/p_og"; mkdir -p "$W/p_plus" "$W/p_og"; cp -R "$W/card/Plugins" "$W/p_plus/"; cp -R "$W/card/Plugins" "$W/p_og/"
(cd "$W" && UISHOT_POKE="$W/poke_s1" timeout 120 "$BIN" "$W/p_plus" "wait 500; instr 64; tap Params; wait 1500; pickmodel ALCO; wait 300; tap Right 6; tap Up; tap Up; wait 200; poke nna 0; saveproj nnaP" > "$W/s1.log" 2>&1)
(cd "$W" && UISHOT_POKE="$W/poke_s2" timeout 120 "$OGBIN" "$W/p_og" "wait 500; instr 64; poke nna 0 2; poke nna 0; saveproj nnaP" > "$W/s2.log" 2>&1)
RAW_PLUS=$(grep '^nna 0 ' "$W/poke_s1.out" | tail -1 | cut -d' ' -f3-); RAW_OG=$(grep '^nna 0 ' "$W/poke_s2.out" | tail -1 | cut -d' ' -f3-)
# the OG opens the project saved here, this sim opens the one the OG saved (each starts on another mode first)
rm -rf "$W/p_o1" "$W/p_o2"; mkdir -p "$W/p_o1/Projects" "$W/p_o2/Projects"; cp -R "$W/p_plus/Projects/nnaP" "$W/p_o1/Projects/"; cp -R "$W/p_og/Projects/nnaP" "$W/p_o2/Projects/"
cp -R "$W/card/Plugins" "$W/p_o2/"
(cd "$W" && UISHOT_POKE="$W/poke_o1" timeout 120 "$OGBIN" "$W/p_o1" "wait 500; instr 64; poke nna 0 3; openproj nnaP; wait 500; poke nna 0" > "$W/o1.log" 2>&1)
(cd "$W" && UISHOT_POKE="$W/poke_o2" timeout 120 "$BIN" "$W/p_o2" "wait 500; instr 64; poke nna 0 3; openproj nnaP; wait 500; poke nna 0" > "$W/o2.log" 2>&1)
O1=$(grep '^nna 0 ' "$W/poke_o1.out" | tail -1 | cut -d' ' -f3-); O2=$(grep '^nna 0 ' "$W/poke_o2.out" | tail -1 | cut -d' ' -f3-)
[ -n "$RAW_PLUS" ] && [ "$RAW_PLUS" = "$RAW_OG" ]; row "N4a $PRODUCT the page writes the OG's pair (raw '$RAW_PLUS' vs OG '$RAW_OG')" $?
[ "${O1%% *}" = "mode" ] && [ "$(echo "$O1" | cut -d' ' -f2)" = 2 ] && [ "$(echo "$O2" | cut -d' ' -f2)" = 2 ]; row "N4b $PRODUCT project saved here opens as Cut in the OG sim ('$O1'), the OG's file opens as Cut here ('$O2')" $?
cmp -s <(cd "$W/p_plus/Projects/nnaP" && ls) <(cd "$W/p_og/Projects/nnaP" && ls) ; row "N4c $PRODUCT saved project folders hold the same files" $?

# ---- N5 / N6: rendered overlap. ALCO 4 voices, Amp Sustain 1, Release 2 s, note 60 at step 1 and 64 at step 5 (120 BPM)
V4="poke voicecount 0 4; poke synthparam 0 Amp_Sustain 1; poke synthparam 0 Amp_Release 2"
TWO="setstep 0 0 60 64 0 0 0 0; setstep 0 4 64 64 0 0 0 0"
run2() {   # run2 NAME "PAGE KEYS" "PRE" "STEPS": the keys run on the Synth page, then the pattern, the steps, 1.2 s of play
  run "$1" ALCO "$2; tap Pattern; wait 300; $3; wait 300; $4; audio 1; wav $W/$1.wav; tap Play; wait 1200; tap Play; wait 400; wav off"
}
UP() { local k=""; for ((i = 0; i < $1; i++)); do k="$k; tap Up"; done; echo "tap Right 6$k; wait 200"; }
run2 r_alone "wait 100" "$V4" "setstep 0 4 64 64 0 0 0 0" &
run2 r_rel "wait 100" "$V4" "$TWO" &
run2 r_cut_ui "$(UP 2)" "$V4" "$TWO" &
run2 r_fade_ui "$(UP 3)" "$V4" "$TWO" &
wait
run2 r_cont_ui "$(UP 1)" "$V4" "$TWO" &
run2 r_cut_poke "wait 100" "$V4; poke nna 0 2" "$TWO" &
run2 r_fade_poke "wait 100" "$V4; poke nna 0 3" "$TWO" &
run2 r_cont_poke "wait 100" "$V4; poke nna 0 1" "$TWO" &
wait
run2 r_fx "$(UP 1)" "$V4" "setstep 0 0 60 64 54 2 0 0; setstep 0 4 64 64 0 0 0 0" &
wait
same r_cut_ui.wav r_cut_poke.wav && ! same r_cut_ui.wav r_rel.wav; row "N5a $PRODUCT Cut set on the page == poke nna 2, bit equal, not Release" $?
same r_fade_ui.wav r_fade_poke.wav && ! same r_fade_ui.wav r_cut_ui.wav; row "N5b $PRODUCT Fade set on the page == poke nna 3, bit equal, not Cut" $?
same r_cont_ui.wav r_cont_poke.wav && ! same r_cont_ui.wav r_rel.wav && ! same r_cont_ui.wav r_cut_ui.wav; row "N5c $PRODUCT Continue set on the page == poke nna 1, bit equal, not Release, not Cut" $?
S4=$(win onset "$W/r_alone.wav")
win from "$W/r_fx.wav" "$W/r_alone.wav" $((S4 + 256)) && ! same r_fx.wav r_cont_ui.wav; row "N6 $PRODUCT part on Continue (page), Cut FX on step 1: == step 5 note alone within two blocks (onset $S4), not the Continue render" $?
echo "RESULT: pass $pass fail $fail"
[ "$fail" = 0 ]
