#!/bin/bash
# mini_smoke.sh: smoke test of the Tracker Mini simulator (ui_shot PLATFORM=IMX PRODUCT=MINI, mirror of plus_smoke.sh). Host only, NOT firmware, no device.
#   1 builds the Mini sim: make PLATFORM=IMX PRODUCT=MINI CONSOLE=1 BRIDGE=1 into build/ui-shot-mini-console-bridge (bridge_env.sh finds the pinned Unicorn
#     and the release axf, DEVTOOLS_DIR names a console checkout with the UISHOT_PLUS guards, devtools rel-2.0.0-int or main)
#   2 the binary is the IMX core of the Mini (-DTRACKER_MINI in every compile line) (the stereo voice of AudioVoice.h links) with Unicorn linked
#   3 boot to the start screen, one screen shot, the Mini keys: hold Menu shows the 11 module popup and releasing it on another box switches the page,
#     Fn1..Fn4 reach four different pages (Fn1 = Pattern), Shift+Fn1 reaches the shifted binding, the centre key starts the playback, a page walk
#     through all 11 Menu boxes (Pattern, Instrument Parameters, Sample Playback, Master, Song, Sample Loader, File, Config, Perform, Sample Editor,
#     Sample Recorder) shows 11 different screens. The Mini has no pads and no encoder: none of them is pressed
#   4 a sample note (samplekick on instrument 1, pattern step, Play) renders a WAV that is not silent
#   5 a synth part note (instrument 64 = part 0) renders a WAV that is not silent: the STOCK synth stack (Synth.cpp, no FEATURE_SYNTH_OPEN)
#     over the host polysynth:: proxy (host_plusproxy.cpp) and the build's own closed engine harness
#   6 one note of each stock Tracker+ engine through the closed libPolySynth under Unicorn (../plus_synth), not silent, no NaN
#   7 phase 3b: one UI synth note per stock engine (ACD FAT VAP WTFM PERC DIRT, FAT PERC DIRT picked in the stock model list) on the closed
#     lib (UISHOT_PLUS_SYNTH = step 6's harness, host_plusproxy.cpp), the part's guest stream bit identical to run.py --events
#   8 phase 5: the mini pin parity (devtools emu-release.sh parity, MODEL=mini), then a mirror smoke (server.ts --model mini answers _frame)
#     and a mirror replay of demo-pattern.flow --model mini --offline --wav: the pin's harness plays the synth parts (no manual env), not silent
#   9 round 3: a param edit in the patch editor reaches the guest (the par line, the render differs from step 7's ACD and equals run.py
#     --events), a .patch save (Synth::savePatch on the host card) and load (Synth::loadPatch on a fresh card) round trip gives the guest the
#     same params and the same stream, and a PERC part panned right (Kick Pan 1) is stereo: guest R only, the mix's WAV R not silent, L silent
#     9d (round 4): a guest PERC part's aux sends return through reverb and delay (part sends 100: Kick Reverb 1 or Kick Delay 1 leave a
#     tail after the note, Kick sends 0 leave none)
#  10 round 5: per stock engine a param edit on the stock patch editor (first param, encoder) then a note: the edit is a par line of the dump
#     and the stream equals run.py --events. The stock value text is the lib's (synthfmt: ACD Sub Mode, DIRT Shape), DIRT is listed by the
#     stock SynthEditor.cpp (no engine_list.h overlay), and the build has no FEATURE_SYNTH_OPEN, no open stack, no host_plussynth.cpp
# The Mini build is the plus build plus -DTRACKER_MINI: only the core FF features (P4, FEATURE_FF_CORE: Project Settings, FX3/FX4, Snap, Follow, Seed, Groove, Parts, Soft Knee, flows in plus_flows.sh) and the
# master insert host (FEATURE_PLUG_HOST since P6, as on Tracker+, no engine plugins): no FW_TRIM, FW_SE_PTFX, FW_REVERB_SRAM, and since round 5 no
# FEATURE_SYNTH_OPEN: the synth parts are the stock stack over the closed lib
#  11 the project store: a Workspace save writes project.mtff and a restart reads it back (imx_store_sim.sh)
# Exit 0 when every check passes. Output in build/mini-smoke (PNG, WAV, logs).
#  11 P2 build hygiene (imx_check.sh mini): DEFS parity with make -n mini, the core objects -Wall -Wextra -Werror in a fresh sim build, the real compiler's syntax pass
#  12 P10 H7: the extension module loader (ext_imx.cpp) with the devtools IMX module under Unicorn, rows X1-X5 (ext_imx_rows.sh)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
WORK="$ROOT/build/mini-smoke"
# the tracker-ff ref the mini pin is checked against (MINI_PIN_REV=<ref> for a feature branch)
MINI_PIN_REV="${MINI_PIN_REV:-release-2.0.0-int}"
JOBS="$(sh "$HERE/../jobs.sh" n)"
if [ -z "${DEVTOOLS_DIR:-}" ]; then
    for d in "$HOME/prj/poly-devtools/.wt/rel141-int" "$HOME/prj/poly-devtools/.wt/main" "$HOME/prj/poly-devtools/.wt/plus-sim" "$HOME/prj/poly-devtools"; do
        grep -q UISHOT_PLUS "$d/firmware/include/UsbTest.h" 2>/dev/null && { DEVTOOLS_DIR="$d"; break; }
    done
fi
[ -n "${DEVTOOLS_DIR:-}" ] && grep -q UISHOT_PLUS "$DEVTOOLS_DIR/firmware/include/UsbTest.h" \
    || { echo "mini_smoke: DEVTOOLS_DIR must name a console checkout with the UISHOT_PLUS guards (devtools rel-2.0.0-int or main)" >&2; exit 2; }
. "$HERE/bridge_env.sh"
bridge_env "$ROOT" || exit 2
BUILD="$ROOT/build/ui-shot-mini-console-bridge"
BIN="$BUILD/ui_shot"
rm -rf "$WORK"; mkdir -p "$WORK"
fails=0
ok()   { printf 'ok    %s\n' "$1"; }
bad()  { printf 'FAIL  %s\n' "$1"; fails=$((fails+1)); }

# 1 build
if make -C "$HERE" -j"$JOBS" PLATFORM=IMX PRODUCT=MINI CONSOLE=1 "${BRIDGE_ARGS[@]}" DEVTOOLS_DIR="$DEVTOOLS_DIR" >"$WORK/make.log" 2>&1; then ok "build $BIN"
else tail -30 "$WORK/make.log"; bad "build (log $WORK/make.log)"; echo "mini_smoke: FAIL"; exit 1; fi

# 2 the IMX core and the bridge are in the binary
syms=$(nm "$BIN" | c++filt)
grep -q 'AudioVoice::getPanningAmplifier' <<<"$syms" && ok "IMX core linked (AudioVoice::getPanningAmplifier, the Tracker+ stereo voice)" || bad "IMX core: no AudioVoice::getPanningAmplifier"
[ "$(nm "$BIN" | grep -cE ' _uc_(emu_start|open|close)$')" = 3 ] && ok "Unicorn linked (BRIDGE=1)" || bad "Unicorn not linked"
# round 5: the stock synth stack, no open stack (no FEATURE_SYNTH_OPEN in the compile lines, no open engine registry, the proxy's createSynth)
nso=$(make -C "$HERE" -n -B PLATFORM=IMX PRODUCT=MINI CONSOLE=1 "${BRIDGE_ARGS[@]}" DEVTOOLS_DIR="$DEVTOOLS_DIR" 2>/dev/null | grep -c -- '-DFEATURE_SYNTH_OPEN' || true)
if [ "$nso" = 0 ] && grep -q 'polysynth::Synth::createSynth' <<<"$syms" && ! grep -q 'polysynth::open::registerEngines' <<<"$syms" \
    && ! grep -q 'uishot_plus_block' <<<"$syms"; then ok "stock synth stack: no FEATURE_SYNTH_OPEN in the DEFS, no open stack, host polysynth:: proxy linked"
else bad "stock synth stack: $nso compile lines with FEATURE_SYNTH_OPEN, or the open stack / host_plussynth.cpp is linked"; fi

run() {   # run NAME SCRIPT: a fresh card per run
    rm -rf "$WORK/$1"; mkdir -p "$WORK/$1"
    (cd "$WORK" && ASAN_OPTIONS=detect_leaks=0 timeout 240 "$BIN" "$WORK/$1" "$2" >"$WORK/$1.log" 2>&1)
}
peak() {   # peak WAV: the largest absolute sample (16 bit PCM), stdlib only
    python3 -I -c 'import sys,wave,array
w=wave.open(sys.argv[1]); a=array.array("h"); a.frombytes(w.readframes(w.getnframes())); print(max((abs(x) for x in a), default=0), w.getnframes())' "$1"
}

# 3 boot, screen shot, the Mini keys (Menu is hold to show, release to switch: fPatternEdit.cpp functMenu, interfaceMenuPopup.cpp)
if run boot "wait 1500; shot $WORK/boot.png"; then
    grep -q 'boot: start state 1: start RUN' "$WORK/boot.log" && ok "boot to the start screen" || bad "boot: no start RUN line (log $WORK/boot.log)"
    [ -s "$WORK/boot.png" ] && [ "$(wc -c <"$WORK/boot.png")" -gt 4000 ] && ok "screen shot boot.png ($(wc -c <"$WORK/boot.png" | tr -d ' ') B)" || bad "screen shot boot.png missing or empty"
else bad "boot run exited $? (log $WORK/boot.log)"; fi
# the page walk: every Menu box from Pattern (box 0): Right x (i % 4), Down x (i / 4), release. Back to Pattern with Fn1 (the default binding 0)
walk="wait 1500; shot $WORK/w_pattern.png; "
names=(pattern params splay master song sload file config perform sedit srec)
for i in $(seq 0 10); do
    mv=""; for ((k = 0; k < i % 4; k++)); do mv="$mv tap Right;"; done   # not seq 1 N: BSD seq 1 0 counts down
    for ((k = 0; k < i / 4; k++)); do mv="$mv tap Down;"; done
    walk="$walk hold Menu; wait 300;$mv wait 100; shot $WORK/p_${names[$i]}_popup.png; unhold Menu; wait 700; shot $WORK/p_${names[$i]}.png; tap Fn1; wait 500;"
done
if run walk "$walk hold Menu; wait 300; shot $WORK/m_popup.png; unhold Menu; wait 400; tap Fn1; wait 300; tap Fn2; wait 500; shot $WORK/fn2.png; tap Fn3; wait 500; shot $WORK/fn3.png; tap Fn4; wait 500; shot $WORK/fn4.png; tap Fn1; wait 500; shot $WORK/fn1.png; hold Shift; tap Fn1; unhold Shift; wait 500; shot $WORK/sfn1.png; tap Fn1; wait 400; tap Mid; wait 600; shot $WORK/mid.png; tap Mid; wait 300"; then
    ! cmp -s "$WORK/boot.png" "$WORK/m_popup.png" && [ "$(wc -c <"$WORK/m_popup.png")" -gt 4000 ] && ok "Menu held shows the module popup" || bad "Menu held: the screen equals the start screen"
    cmp -s "$WORK/boot.png" "$WORK/fn1.png" && ok "Fn1 = Pattern (the start screen again)" || bad "Fn1 is not the Pattern page"
    nd=$(md5 -q "$WORK/fn2.png" "$WORK/fn3.png" "$WORK/fn4.png" "$WORK/fn1.png" 2>/dev/null | sort -u | wc -l | tr -d ' ')
    [ "$nd" = 4 ] && ok "Fn1..Fn4 reach 4 different pages" || bad "Fn1..Fn4 reach $nd different pages, not 4"
    ! cmp -s "$WORK/fn1.png" "$WORK/sfn1.png" && ok "Shift+Fn1 reaches the shifted binding (another screen than Fn1)" || bad "Shift+Fn1 equals Fn1"
    ! cmp -s "$WORK/fn1.png" "$WORK/mid.png" && ok "centre key (Mid) acts on the Pattern page (the screen changes)" || bad "Mid changes nothing"
    okw=0; for n in "${names[@]:1}"; do
        [ -s "$WORK/p_$n.png" ] && ! cmp -s "$WORK/w_pattern.png" "$WORK/p_$n.png" && okw=$((okw+1)); done
    [ "$okw" = 10 ] && ok "page walk through the 11 Menu boxes: 10 pages other than Pattern reached (each Menu box switches the page)" || bad "page walk: $okw of 10 pages differ from Pattern (walk screens in $WORK/p_*.png)"
    nd=$(for n in "${names[@]}"; do md5 -q "$WORK/p_$n.png"; done 2>/dev/null | sort -u | wc -l | tr -d ' ')
    [ "$nd" -ge 10 ] && ok "page walk: $nd different screens over the 11 pages" || bad "page walk: only $nd different screens"
else bad "Mini key run exited (log $WORK/walk.log)"; fi
# 4 sample note
if run sample "wait 500; samplekick 0; instr 0; setstep 0 0 60 0 0 0 0 0; audio 1; wav $WORK/sample.wav; tap Play; wait 600; tap Play; wait 100; wav off"; then
    read -r p n < <(peak "$WORK/sample.wav")
    [ "$p" -gt 1000 ] && ok "sample note WAV not silent (peak $p, $n frames)" || bad "sample note WAV silent (peak $p, $n frames)"
else bad "sample run exited (log $WORK/sample.log)"; fi

# 5 synth part note (stock synth stack on the closed lib, see the header)
if run synth "wait 500; instr 64; setstep 0 0 60 64 0 0 0 0; audio 1; wav $WORK/synth.wav; tap Play; wait 800; tap Play; wait 300; wav off; tap Params; wait 400; shot $WORK/synth_params.png"; then
    read -r p n < <(peak "$WORK/synth.wav")
    [ "$p" -gt 500 ] && ok "synth part note WAV not silent (peak $p, $n frames, stock synth stack on the closed lib)" || bad "synth part note WAV silent (peak $p, $n frames)"
else bad "synth run exited (log $WORK/synth.log)"; fi

# 6 the STOCK Tracker+ engines: the closed libPolySynth.a + libPolyDSP.a (unmodified, Cortex-M7 code) under Unicorn (plus_synth/build.sh, run.py),
#   one note each of ACD FAT VAP WTFM PERC DIRT, not silent, no NaN. Not wired into the UI yet: the sim's synth parts still play the open ports (step 5)
PY="${PY:-$HOME/prj/poly/build/pyenv/bin/python}"
PS="$HERE/../plus_synth"
if bash "$PS/build.sh" "$WORK/plus-synth" >"$WORK/plus-synth.log" 2>&1; then
    ok "closed engine harness built (libPolySynth.a, libPolyDSP.a unmodified)"
    for e in acd fat vap wtfm perc dirt; do
        if line=$("$PY" -I "$PS/run.py" "$WORK/plus-synth/harness.elf" "$WORK/plus-synth/harness.nm" "$e" "$WORK/stock_$e.wav" 2>&1 | tail -1); then
            p=$(sed -n 's/.* peak \([0-9.]*\) .*/\1/p' <<<"$line"); nan=$(sed -n 's/.* nan \([0-9]*\) .*/\1/p' <<<"$line")
            awk -v p="$p" -v n="$nan" 'BEGIN { exit !(p > 0.01 && n == 0) }' && ok "stock $e note via libPolySynth under Unicorn (peak $p)" || bad "stock $e: $line"
        else bad "stock $e: $line"; fi
    done
else tail -20 "$WORK/plus-synth.log"; bad "closed engine harness build (log $WORK/plus-synth.log)"; fi

# 7 phase 3b: a UI synth part on the CLOSED lib (host_plussynth.cpp, UISHOT_PLUS_SYNTH = the harness of step 6). One note per stock engine from
#   the pattern (part defaults: 0 WTFM, 1 ACD, 2 VAP, FAT PERC and DIRT picked on the Params page of part 1). The guest's block stream of the part
#   (UISHOT_PLUS_DUMP) must equal run.py --events (the same notes at the same blocks, the standalone harness) bit for bit, and not be silent.
#   DIRT (kShapingSynth) is in the stock model list (the lib registers it, SynthEditor.cpp draws its icon)
if [ -s "$WORK/plus-synth/harness.elf" ]; then
    for spec in "wtfm 64 0 -" "acd 65 1 -" "vap 66 2 -" "fat 65 1 FAT" "perc 65 1 PERC" "dirt 65 1 DIRT"; do
        read -r e ins part pick <<<"$spec"
        mkdir -p "$WORK/guest_$e"
        pk=""; [ "$pick" != - ] && pk="tap Params; wait 600; pickmodel $pick; tap Pattern; wait 300; "
        if UISHOT_PLUS_SYNTH="$WORK/plus-synth/harness.elf" UISHOT_PLUS_DUMP="$WORK/guest_$e" \
            run "ui_$e" "wait 500; instr $ins; ${pk}setstep 0 0 60 $ins 0 0 0 0; audio 1; wav $WORK/ui_$e.wav; tap Play; wait 800; tap Play; wait 300; wav off"; then
            d="$WORK/guest_$e/part$part"
            if [ -s "$d.f32" ] && grep -q '^ev .* 4 0 60 ' "$d.events"; then
                n=$(( $(wc -c <"$d.f32") / 1024 ))
                "$PY" -I "$PS/run.py" "$WORK/plus-synth/harness.elf" "$WORK/plus-synth/harness.nm" "$e" "$WORK/guest_$e/replay.f32" --events "$d.events" --blocks "$n" >"$WORK/guest_$e/replay.log" 2>&1
                pk=$("$PY" -I -c 'import sys,array; a=array.array("f"); a.frombytes(open(sys.argv[1],"rb").read()); print("%.4f" % max(abs(x) for x in a))' "$d.f32")
                t=$(head -1 "$d.events")
                if cmp -s "$d.f32" "$WORK/guest_$e/replay.f32" && awk -v p="$pk" 'BEGIN { exit !(p > 0.01) }'; then
                    ok "UI part $part $e on the closed lib ($t, $n blocks, peak $pk) bit identical to run.py --events"
                else bad "UI part $part $e: guest stream differs from run.py --events or silent (peak $pk, $d)"; fi
            else bad "UI part $part $e: no guest dump or no note-on event ($d, log $WORK/ui_$e.log)"; fi
        else bad "UI $e run exited (log $WORK/ui_$e.log)"; fi
    done
else bad "UI synth parts on the closed lib: no harness (step 6)"; fi

# 8 phase 5: the mini pin (build/emu/current-mini, MODEL=mini emu-release.sh build --rev release-2.0.0-int) is this branch's tip and passes parity
#   (tip sha, compiled sources vs the IMX image minus parity-exceptions-plus.txt, defines, both overlay allowlists, an empty card), then the
#   mirror on it answers: server.ts --model mini serves the screen (_frame of the sim behind /api/screen.png). MINI_SMOKE_NO_PIN=1 skips both
if [ "${MINI_SMOKE_NO_PIN:-0}" != 1 ]; then
    EMU_ROOT="${EMU_ROOT:-$HOME/prj/poly/build/emu}"
    if MODEL=mini EMU_ROOT="$EMU_ROOT" "$DEVTOOLS_DIR/tools/mirror/emu-release.sh" parity --rev "$MINI_PIN_REV" >"$WORK/parity.log" 2>&1 \
        && grep -q '^PARITY OK (mini)' "$WORK/parity.log"; then ok "mini pin parity: $(grep -c '^PASS' "$WORK/parity.log") checks PASS, PARITY OK (mini) ($(readlink "$EMU_ROOT/current-mini"))"
    else tail -15 "$WORK/parity.log"; bad "mini pin parity (log $WORK/parity.log; re-pin: MODEL=mini emu-release.sh build --rev $MINI_PIN_REV)"; fi
    port=$((7300 + RANDOM % 500))
    (cd "$DEVTOOLS_DIR/tools/mirror" && exec bun server.ts --target sim --model mini --port "$port") >"$WORK/mirror.log" 2>&1 &
    mpid=$!
    got=0
    for _ in $(seq 1 60); do
        if curl -sf -o "$WORK/mirror.png" "http://127.0.0.1:$port/api/screen.png?fresh=1" 2>/dev/null && [ "$(wc -c <"$WORK/mirror.png")" -gt 4000 ]; then got=1; break; fi
        sleep 1
    done
    st=$(curl -sf "http://127.0.0.1:$port/api/state" 2>/dev/null | head -c 400 || true)
    kill "$mpid" 2>/dev/null; wait "$mpid" 2>/dev/null || true
    [ "$got" = 1 ] && ok "mirror --model mini answers _frame (screen $(wc -c <"$WORK/mirror.png" | tr -d ' ') B, port $port)" || bad "mirror --model mini: no screen (log $WORK/mirror.log, state $st)"
    # the flow's synth parts on the pin's closed lib harness: plusSynthEnv (devtools src/release.ts) sets UISHOT_PLUS_SYNTH, nobody else does
    if (cd "$DEVTOOLS_DIR/tools/mirror" && env -u UISHOT_PLUS_SYNTH bun replay.ts scenarios/demo-pattern.flow --target sim --model mini --offline \
            --wav "$WORK/mirror_synth.wav") >"$WORK/mirror_replay.log" 2>&1 && [ -s "$WORK/mirror_synth.wav" ]; then
        read -r p n < <(peak "$WORK/mirror_synth.wav")
        g=$(grep -c 'plusproxy: closed libPolySynth guest' "$WORK/mirror_replay.log" || true)
        [ "$p" -gt 500 ] && [ "$g" -gt 0 ] && ok "mirror replay --model mini plays the synth parts on the closed lib ($g guests, WAV peak $p, $n frames)" \
            || bad "mirror replay --model mini: guests $g, WAV peak $p (log $WORK/mirror_replay.log)"
    else tail -15 "$WORK/mirror_replay.log"; bad "mirror replay --model mini (log $WORK/mirror_replay.log)"; fi
fi

# 9 round 3: parameters, .patch files and stereo on the closed lib (the stock synth stack over host_plusproxy.cpp since round 5)
H="$WORK/plus-synth"
grun() {   # grun NAME CARD SCRIPT: a run with the parts on the closed lib, dump in guest_NAME, poke output in poke_NAME.out
    rm -rf "$WORK/guest_$1"; mkdir -p "$WORK/guest_$1"
    (cd "$WORK" && UISHOT_POKE="$WORK/poke_$1" UISHOT_PLUS_SYNTH="$H/harness.elf" UISHOT_PLUS_DUMP="$WORK/guest_$1" ASAN_OPTIONS=detect_leaks=0 \
        timeout 240 "$BIN" "$WORK/$2" "$3" >"$WORK/$1.log" 2>&1)
}
replay_eq() {   # replay_eq NAME PART ENGINE: the guest stream equals run.py --events of its dump
    local d="$WORK/guest_$1/part$2"
    [ -s "$d.f32" ] || return 1
    "$PY" -I "$PS/run.py" "$H/harness.elf" "$H/harness.nm" "$3" "$WORK/guest_$1/replay.f32" --events "$d.events" --blocks $(( $(wc -c <"$d.f32") / 1024 )) >"$WORK/guest_$1/replay.log" 2>&1 \
        && cmp -s "$d.f32" "$WORK/guest_$1/replay.f32"
}
ACD_PLAY='setstep 0 0 60 65 0 0 0 0; audio 1; tap Play; wait 800; tap Play; wait 300'
SAW06=$("$PY" -I -c 'import struct; print(struct.unpack("<i", struct.pack("<f", 0.6))[0])')
if [ -s "$H/harness.elf" ]; then
    rm -rf "$WORK/card_edit"; mkdir -p "$WORK/card_edit"
    grun edit card_edit "wait 500; instr 65; tap Params; wait 1500; tap F8; wait 2000; tap Mid; wait 300; tap Minus10 4; wait 300; poke patchvals 1; poke synthlib save 1 R3RT; tap Pattern; wait 300; $ACD_PLAY"
    if grep -q "^par 0 0 $SAW06\$" "$WORK/guest_edit/part1.events" && replay_eq edit 1 acd && ! cmp -s "$WORK/guest_edit/part1.f32" "$WORK/guest_acd/part1.f32"; then
        ok "param edit (patch editor, Saw Mix 100 % -> 60 %) reaches the guest: $(grep -c '^par' "$WORK/guest_edit/part1.events") params sent, render differs from step 7, bit identical to run.py --events"
    else bad "param edit: no Saw Mix 0.6 on the guest, or the stream differs from run.py --events / equals the unedited one ($WORK/guest_edit, poke $(tail -1 "$WORK/poke_edit.out" 2>/dev/null))"; fi
    f="$WORK/card_edit/Patches/ACD/R3RT.patch"
    rm -rf "$WORK/card_load"; mkdir -p "$WORK/card_load/Patches/ACD"
    if [ -s "$f" ] && cp "$f" "$WORK/card_load/Patches/ACD/"; then
        grun load card_load "wait 500; instr 65; wait 300; poke synthlib load 1 R3RT; wait 300; poke patchvals 1; $ACD_PLAY"
        if grep -q 'synthlib load part 1 R3RT rc=1' "$WORK/poke_load.out" && replay_eq load 1 acd \
            && cmp -s <(grep '^par' "$WORK/guest_edit/part1.events") <(grep '^par' "$WORK/guest_load/part1.events") \
            && cmp -s "$WORK/guest_edit/part1.f32" "$WORK/guest_load/part1.f32"; then
            ok ".patch round trip: savePatch wrote $(wc -c <"$f" | tr -d ' ') B, loadPatch on a fresh card gives the guest the same params and the same stream"
        else bad ".patch round trip: load did not give the edited params or stream ($WORK/guest_load, poke $(cat "$WORK/poke_load.out" 2>/dev/null | head -2))"; fi
    else bad ".patch save: no $f"; fi
    rm -rf "$WORK/card_pan"; mkdir -p "$WORK/card_pan"
    grun pan card_pan "wait 500; instr 65; tap Params; wait 600; pickmodel PERC; tap Pattern; wait 300; poke synthparam 1 Kick_Panning 1; wait 300; setstep 0 0 60 65 0 0 0 0; audio 1; wav $WORK/pan.wav; tap Play; wait 800; tap Play; wait 300; wav off"
    st=$("$PY" -I -c 'import sys,array,wave
a=array.array("f"); a.frombytes(open(sys.argv[1],"rb").read()); gl=max(map(abs,a[0::2])); gr=max(map(abs,a[1::2]))
w=wave.open(sys.argv[2]); b=array.array("h"); b.frombytes(w.readframes(w.getnframes())); wl=max(map(abs,b[0::2])); wr=max(map(abs,b[1::2]))
print("%d guestL %.4f guestR %.4f wavL %d wavR %d" % (gl < 1e-6 and gr > 0.01 and wl == 0 and wr > 1000, gl, gr, wl, wr))' "$WORK/guest_pan/part1.f32" "$WORK/pan.wav" 2>&1 || true)
    if [ "$(head -1 "$WORK/guest_pan/part1.events" 2>/dev/null)" = 'init 6 1' ] && [ "${st%% *}" = 1 ] && replay_eq pan 1 perc; then ok "stereo: PERC Kick Pan 1 on the guest (stock output path, ${st#* }), bit identical to run.py --events"
    else bad "stereo: PERC panned right ($st, log $WORK/pan.log)"; fi
    # 9d round 4: the reverb and delay return of a guest PERC part's aux sends. As on a Tracker+ the lib's send buffers go through the part's
    # own send level (setSynthReverbSend/DelaySend, 0 by default): the part sends are set to 100, the Kick sends of the patch pick the FX.
    # The mix after the note (0.9 s on, Play stopped at 0.8 s) is silent with the Kick sends at 0 and carries the reverb or delay tail otherwise
    sendrun() {   # sendrun NAME POKES
        rm -rf "$WORK/card_$1"; mkdir -p "$WORK/card_$1"
        grun "$1" "card_$1" "wait 500; instr 65; tap Params; wait 600; pickmodel PERC; tap Pattern; wait 300; $2; wait 300; setstep 0 0 60 65 0 0 0 0; audio 1; wav $WORK/$1.wav; tap Play; wait 800; tap Play; wait 1500; wav off"
    }
    tail_of() { "$PY" -I -c 'import sys,array,wave
w=wave.open(sys.argv[1]); b=array.array("h"); b.frombytes(w.readframes(w.getnframes())); t=int(0.9*w.getframerate())
print(max(map(abs,b[2*t:])) if len(b) > 2*t else -1)' "$1" 2>/dev/null || echo -1; }
    sendrun snd0 "poke synthsend 1 reverb 100; poke synthsend 1 delay 100"
    sendrun rev1 "poke synthsend 1 reverb 100; poke synthparam 1 Kick_Reverb 1"
    sendrun dly1 "poke synthsend 1 delay 100; poke synthparam 1 Kick_Delay 1"
    t0=$(tail_of "$WORK/snd0.wav"); tr=$(tail_of "$WORK/rev1.wav"); td=$(tail_of "$WORK/dly1.wav")
    if [ "$t0" = 0 ] && [ "$tr" -gt 100 ] && [ "$td" -gt 100 ] && grep -q '^par 0 52 1065353216$' "$WORK/guest_rev1/part1.events" \
        && grep -q 'synthsend part 1 reverb = 100' "$WORK/poke_rev1.out" && replay_eq rev1 1 perc && replay_eq dly1 1 perc; then
        ok "aux send return: guest PERC Kick Reverb 1 -> reverb tail $tr, Kick Delay 1 -> delay tail $td, Kick sends 0 -> tail $t0 (part sends 100), guest bit identical to run.py --events"
    else bad "aux send return: tails none $t0 reverb $tr delay $td ($WORK/rev1.log, $WORK/dly1.log)"; fi
else bad "round 3 checks: no harness (step 6)"; fi

# 10 round 5: the stock param pages, value text and model list of the stock synth stack
if [ -s "$H/harness.elf" ]; then
    # the Mini has no encoder: its -10 / +10 keys feed the same pot input (interfaceInputs.cpp functMiniMinus10), 4 taps = the plus smoke's enc -/+40
for spec in "acd - Minus10" "fat FAT Minus10" "vap VAP Minus10" "wtfm WTFM Plus10" "perc PERC Down+Minus10" "dirt DIRT Minus10"; do   # WTFM's first param starts at its minimum
        read -r e pick turn <<<"$spec"
        rm -rf "$WORK/card_ed_$e"; mkdir -p "$WORK/card_ed_$e"
        pk=""; [ "$pick" != - ] && pk="pickmodel $pick; wait 300; "
        pre=""; [ "${turn%%+*}" = Down ] && { pre="tap Down; wait 200; "; turn=${turn#*+}; }   # PERC: the Model row (enum list) takes no +/- key on the Mini, Pitch below it does
        grun "ed_$e" "card_ed_$e" "wait 500; instr 65; tap Params; wait 1500; ${pk}tap F8; wait 2000; tap Mid; wait 300; ${pre}tap $turn 4; wait 300; tap Pattern; wait 300; $ACD_PLAY"
        np=$(grep -c '^par ' "$WORK/guest_ed_$e/part1.events" 2>/dev/null || true)
        if [ "${np:-0}" -gt 0 ] && grep -q '^ev .* 4 0 60 ' "$WORK/guest_ed_$e/part1.events" && replay_eq "ed_$e" 1 "$e"; then
            ok "stock patch editor edit on $e: $np par line(s) before the note, bit identical to run.py --events ($(head -1 "$WORK/guest_ed_$e/part1.events"))"
        else bad "stock patch editor edit on $e: $np par lines or the stream differs ($WORK/guest_ed_$e, log $WORK/ed_$e.log)"; fi
    done
    rm -rf "$WORK/card_fmt"; mkdir -p "$WORK/card_fmt"
    grun fmt card_fmt "wait 500; instr 65; poke synthfmt 1 Sub_Mode 0; poke synthfmt 1 Saw_Mix 0.6; tap Params; wait 600; pickmodel DIRT; wait 300; poke synthfmt 1 Shape 0.57"
    if grep -q '^synthfmt part 1 Sub Mode 0 = -1 Oct Square$' "$WORK/poke_fmt.out" && grep -q '^synthfmt part 1 Saw Mix 0.6 = 60 %$' "$WORK/poke_fmt.out" \
        && grep -q '^synthfmt part 1 Shape 0.57 = 57 %$' "$WORK/poke_fmt.out" && grep -q '^pickmodel DIRT ok' "$WORK/fmt.log" \
        && [ ! -e "$BUILD/gen/inc/engine_list.h" ]; then
        ok "stock value text from the lib (ACD Sub Mode '-1 Oct Square', DIRT Shape '57 %'), DIRT in the stock model list (no engine_list.h overlay)"
    else bad "stock value text or DIRT listing ($WORK/poke_fmt.out, $WORK/fmt.log)"; fi
else bad "round 5 checks: no harness (step 6)"; fi

# 15 P12 step 2 (ref/ff-imx-port-plan.md): the Patch step FX and its bank over the stock patch API (FEATURE_SYNTH_PATCHFX, core/synth/Synth.cpp).
#   Part 1 (ACD): slot 3 is filled at 80 % of every range (poke slotfill: Shift + Up / Down's Synth::selectPatchSlot, a save, back to slot 0),
#   then a step plays C-4 with the Patch FX 3 (id 46). The values the part plays become slot 3's row (rawvals: applied 3, rows 0 and 3), the
#   FX's par lines reach the guest before the note and the stream is bit identical to run.py --events of the dump. A second run with
#   Patch FX 5 (a slot with no file) leaves the part on slot 0's values and sends fewer par lines (only the slot changes of the setup)
if [ -s "$H/harness.elf" ]; then
    rm -rf "$WORK/card_pfx"; mkdir -p "$WORK/card_pfx"
    grun pfx card_pfx "wait 500; instr 65; tap Params; wait 1500; poke slotfill 1 3 80; poke rawvals 1; tap Pattern; wait 300; setstep 0 0 60 65 46 3 0 0; audio 1; tap Play; wait 800; tap Play; wait 300; setstep 0 0 60 65 46 3 0 0; tap Play; wait 800; tap Play; wait 300; poke rawvals 1"
    rm -rf "$WORK/card_pfx0"; mkdir -p "$WORK/card_pfx0"
    grun pfx0 card_pfx0 "wait 500; instr 65; tap Params; wait 1500; poke slotfill 1 3 80; tap Pattern; wait 300; setstep 0 0 60 65 46 5 0 0; audio 1; tap Play; wait 800; tap Play; wait 300; poke rawvals 1"
    r0=$(grep '^rawvals part 1' "$WORK/poke_pfx.out" 2>/dev/null | head -1); r1=$(grep '^rawvals part 1' "$WORK/poke_pfx.out" 2>/dev/null | tail -1)
    r5=$(grep '^rawvals part 1' "$WORK/poke_pfx0.out" 2>/dev/null | tail -1)
    np=$(grep -c '^par ' "$WORK/guest_pfx/part1.events" 2>/dev/null || true); np0=$(grep -c '^par ' "$WORK/guest_pfx0/part1.events" 2>/dev/null || true)
    if grep -q '^slotfill part 1 slot 3 t 80 select 2 saved 1 back 1$' "$WORK/poke_pfx.out" && [[ "$r0" == *" slot 0 applied -1 filled 9 "* ]] \
        && [[ "$r1" == *" slot 0 applied 3 filled 9 "* ]] && [ "${r0##* }" != "${r1##* }" ] && [ "${np:-0}" -gt "${np0:-0}" ] \
        && [[ "$r5" == *" applied -1 "* ]] && [ "${r5##* }" = "${r0##* }" ] && [ -s "$WORK/card_pfx/Workspace/1-3.patch" ] && replay_eq pfx 1 acd && replay_eq pfx0 1 acd; then
        ok "Patch FX: slot 3 (80 %) played by FX 3 ($np par lines on the guest, bit identical to run.py --events), FX 5 (no file) ignored ($np0 par lines), rawvals '${r1:0:60}...'"
    else bad "Patch FX: slotfill $(grep '^slotfill' "$WORK/poke_pfx.out" 2>/dev/null) r0 '${r0:0:60}' r1 '${r1:0:60}' r5 '${r5:0:60}' par $np/$np0 ($WORK/pfx.log, $WORK/guest_pfx)"; fi
else bad "step 15 Patch FX: no harness (step 6)"; fi

# 16 P12 step 3 (ref/ff-imx-port-plan.md): the Morph step FX (id 49) over the same bank. Part 1 (ACD), slot 3 filled as in step 15. A step
#   with Patch 3 + Morph 50 blends slot 0 and slot 3 (Patch next to Morph only names the target): rawvals applied 16 (the blend), every value
#   is slot 0's, slot 3's or their midpoint (enum params switch at 50) and some value is strictly between. Patch 3 + Morph 100 plays slot 3's
#   row exactly (the values of step 15's FX 3 run). Both guest streams are bit identical to run.py --events of their dumps
if [ -s "$H/harness.elf" ] && [ -s "$WORK/poke_pfx.out" ]; then
    rm -rf "$WORK/card_pmo"; mkdir -p "$WORK/card_pmo"
    grun pmo card_pmo "wait 500; instr 65; tap Params; wait 1500; poke slotfill 1 3 80; tap Pattern; wait 300; setstep 0 0 60 65 46 3 0 0; audio 1; tap Play; wait 800; tap Play; wait 300; setstep 0 0 60 65 46 3 49 50; tap Play; wait 800; tap Play; wait 300; poke rawvals 1"
    rm -rf "$WORK/card_pmo1"; mkdir -p "$WORK/card_pmo1"
    grun pmo1 card_pmo1 "wait 500; instr 65; tap Params; wait 1500; poke slotfill 1 3 80; tap Pattern; wait 300; setstep 0 0 60 65 46 3 49 50; audio 1; tap Play; wait 800; tap Play; wait 300; setstep 0 0 60 65 46 3 49 100; tap Play; wait 800; tap Play; wait 300; poke rawvals 1"
    r0=$(grep '^rawvals part 1' "$WORK/poke_pfx.out" 2>/dev/null | head -1); r1=$(grep '^rawvals part 1' "$WORK/poke_pfx.out" 2>/dev/null | tail -1)
    rm50=$(grep '^rawvals part 1' "$WORK/poke_pmo.out" 2>/dev/null | tail -1); rm100=$(grep '^rawvals part 1' "$WORK/poke_pmo1.out" 2>/dev/null | tail -1)
    blend=$("$PY" -I -c '
import sys
a, b, m = (list(map(float, s.split(","))) for s in sys.argv[1:4])
assert len(a) == len(b) == len(m) and a
mid = 0
for x, y, v in zip(a, b, m):
    if v == x or v == y:
        continue
    h = x + (y - x) * 0.5
    if abs(v - h) > 1e-4 * max(1.0, abs(x), abs(y)):  # rawvals prints %.6g
        print("off", x, y, v); sys.exit(1)
    mid += 1
print("between", mid)
' "${r0##* }" "${r1##* }" "${rm50##* }" 2>/dev/null)
    if [[ "$rm50" == *" applied 16 "* ]] && [[ "$blend" == between* ]] && [ "${blend#between }" -gt 0 ] \
        && [[ "$rm100" == *" applied 3 "* ]] && [ "${rm100##* }" = "${r1##* }" ] && replay_eq pmo 1 acd && replay_eq pmo1 1 acd; then
        ok "Morph FX: Patch 3 + Morph 50 is the blend of slots 0 and 3 (${blend#between } values between), Morph 100 = slot 3's row, both guest streams bit identical to run.py --events"
    else bad "Morph FX: blend '$blend' m50 '${rm50:0:60}' m100 '${rm100:0:60}' ($WORK/pmo.log, $WORK/guest_pmo)"; fi
else bad "step 16 Morph FX: no harness or no step 15 run"; fi

# 17 P12 round 4 (ref/ff-imx-port-plan.md): the Morph target and the slot names (<P>.names, the MK66 format of synth-open/open_slotnames.h) and
#   the MK66 rounding rule. Run pnm, part 1 (ACD): slot 3 filled at 80 %, opened (slotsel), saved to the library as P12SLOT3 (its name now),
#   slot 0 again, then Shift + Right twice on the Synth screen: Morph target 3, the heading "Slot 0  Morph 3:". Save As P12NAMES (fmsave).
#   The Workspace/1.names is then removed by hand and run pnm2 boots the same card: target back to the default 1 until fmopen P12NAMES brings
#   the project's 1.names back (copyWorkspacePatches): target 3, slot 3 named P12SLOT3. A Morph 50 step without a Patch FX next to it then
#   blends to slot 3: the values equal step 16's Patch 3 + Morph 50 and the guest stream is bit identical to run.py --events.
#   Run pkr, part 1 on VAP (pickmodel): slot 3 at 30 %, Patch 3 + Morph 50 (run pkr1: Morph 100, slot 3's row). morphkinds marks Tune 1 and
#   Tune 2 'r' (VAP kSemi on the MK66), every 'r' value is the blend rounded to the nearest integer, every 's' value slot 3's (50 takes B),
#   both streams are bit identical to run.py --events.
# Rows 15-17 play twice (round 5): the second play starts with the part on the first play's slot or blend, so a Patch FX or Morph that
#   sends a value the part already has (the stock finishBatchUpdate re-sent every param, P12 round 5) shows up as a stream run.py --events
#   does not replay. pfx: Patch 3 twice. pmo: Patch 3, then Patch 3 + Morph 50. pmo1: Patch 3 + Morph 50, then Morph 100. pnm2: Patch 3, then
#   Morph 50 alone. pkr: Patch 3 + Morph 50 twice. pkr1: Patch 3, then Patch 3 + Morph 100
if [ -s "$H/harness.elf" ] && [ -s "$WORK/poke_pmo.out" ]; then
    rm -rf "$WORK/card_pnm"; mkdir -p "$WORK/card_pnm"
    grun pnm card_pnm "wait 500; instr 65; tap Params; wait 1500; poke slotfill 1 3 80; poke slotsel 1 3; poke synthlib save 1 P12SLOT3; poke slotsel 1 0; wait 300; hold Shift; tap Right; wait 300; tap Right; wait 300; unhold Shift; wait 500; poke heading; poke slotnames 1; poke fmsave - P12NAMES; wait 4000; poke slotnames 1"
    ws_names="$(cat "$WORK/card_pnm/Workspace/1.names" 2>/dev/null | tr '\n' '/')"; pj_names="$(find "$WORK/card_pnm" -path '*P12NAMES*' -name 1.names | head -1)"
    rm -f "$WORK/card_pnm/Workspace/1.names"
    grun pnm2 card_pnm "wait 500; instr 65; tap Params; wait 1500; poke slotnames 1; poke fmopen - P12NAMES; wait 4000; tap Pattern; wait 300; instr 65; tap Params; wait 1500; poke slotnames 1; poke heading; tap Pattern; wait 300; setstep 0 0 60 65 46 3 0 0; audio 1; tap Play; wait 800; tap Play; wait 300; setstep 0 0 60 65 49 50 0 0; tap Play; wait 800; tap Play; wait 300; poke rawvals 1"
    s1=$(grep '^slotnames part 1' "$WORK/poke_pnm.out" 2>/dev/null | head -1); h1=$(grep '^heading' "$WORK/poke_pnm.out" 2>/dev/null | head -1)
    s2a=$(grep '^slotnames part 1' "$WORK/poke_pnm2.out" 2>/dev/null | head -1); s2b=$(grep '^slotnames part 1' "$WORK/poke_pnm2.out" 2>/dev/null | tail -1)
    h2=$(grep '^heading' "$WORK/poke_pnm2.out" 2>/dev/null | tail -1)
    rn=$(grep '^rawvals part 1' "$WORK/poke_pnm2.out" 2>/dev/null | tail -1); rm50=$(grep '^rawvals part 1' "$WORK/poke_pmo.out" 2>/dev/null | tail -1)
    if [[ "$s1" == "slotnames part 1 target 3 open 0 names 3=P12SLOT3" ]] && [[ "$h1" == "heading label Slot 0  Morph 3:"* ]] && [ "$ws_names" = "tffnames 1/3=P12SLOT3/m=3/" ] \
        && [ -s "$pj_names" ] && [[ "$s2a" == "slotnames part 1 target 1 open 0 names -" ]] && [[ "$s2b" == "slotnames part 1 target 3 open 0 names 3=P12SLOT3" ]] \
        && [[ "$h2" == "heading label Slot 0  Morph 3:"* ]] && [[ "$rn" == *" applied 16 "* ]] && [ "${rn##* }" = "${rm50##* }" ] && replay_eq pnm2 1 acd; then
        ok "Morph target and slot names: Shift + Right x2 -> target 3 ('$h1'), Workspace 1.names '$ws_names', project copy restored by fmopen ('$s2b'), Morph 50 alone blends to slot 3 = step 16's values, bit identical to run.py --events"
    else bad "Morph target / names: s1 '$s1' h1 '$h1' ws '$ws_names' proj '$pj_names' s2 '$s2a' / '$s2b' h2 '$h2' rn '${rn:0:60}' ($WORK/pnm.log, $WORK/pnm2.log)"; fi
    rm -rf "$WORK/card_pkr"; mkdir -p "$WORK/card_pkr"
    grun pkr card_pkr "wait 500; instr 65; tap Params; wait 600; pickmodel VAP; wait 300; poke slotfill 1 3 30; poke rawvals 1; poke morphkinds 1; tap Pattern; wait 300; setstep 0 0 60 65 46 3 49 50; audio 1; tap Play; wait 800; tap Play; wait 300; setstep 0 0 60 65 46 3 49 50; tap Play; wait 800; tap Play; wait 300; poke rawvals 1"
    rm -rf "$WORK/card_pkr1"; mkdir -p "$WORK/card_pkr1"
    grun pkr1 card_pkr1 "wait 500; instr 65; tap Params; wait 600; pickmodel VAP; wait 300; poke slotfill 1 3 30; tap Pattern; wait 300; setstep 0 0 60 65 46 3 0 0; audio 1; tap Play; wait 800; tap Play; wait 300; setstep 0 0 60 65 46 3 49 100; tap Play; wait 800; tap Play; wait 300; poke rawvals 1"
    k=$(grep '^morphkinds part 1' "$WORK/poke_pkr.out" 2>/dev/null | head -1)
    ra=$(grep '^rawvals part 1' "$WORK/poke_pkr.out" 2>/dev/null | sed -n 1p); rb=$(grep '^rawvals part 1' "$WORK/poke_pkr1.out" 2>/dev/null | tail -1)
    rc=$(grep '^rawvals part 1' "$WORK/poke_pkr.out" 2>/dev/null | sed -n 2p)
    kinds=$(echo "$k" | sed -n 's/.* kinds \([lrs-]*\) names .*/\1/p')
    rnd=$("$PY" -I -c '
import sys, math
k = sys.argv[4]
a, b, m = (list(map(float, s.split(","))) for s in sys.argv[1:4])
assert len(a) == len(b) == len(m) == len(k) and a
r = mid = 0
for kind, x, y, v in zip(k, a, b, m):
    if x == y:
        assert v == x, (kind, x, y, v)
        continue
    if kind == "s":
        assert v == y, (kind, x, y, v)          # 50 takes B, as the MK66
    elif kind == "r":
        want = x + (y - x) * 0.5
        assert v == float(round(want)) and v == math.floor(v), (kind, x, y, v)  # nearest, ties to even (python round = nearbyint)
        r += 1
    else:
        assert abs(v - (x + (y - x) * 0.5)) <= 1e-4 * max(1.0, abs(x), abs(y)), (kind, x, y, v)
        mid += 1
print("ok", r, mid)
' "${ra##* }" "${rb##* }" "${rc##* }" "$kinds" 2>&1 | tail -1)
    nr=$(echo "$kinds" | tr -cd r | wc -c | tr -d ' ')
    if [[ "$k" == *"names "* ]] && [ "$nr" = 2 ] && [[ "$rnd" == ok* ]] && [[ "$rc" == *" applied 16 "* ]] && [[ "$rb" == *" applied 3 "* ]] && replay_eq pkr 1 vap && replay_eq pkr1 1 vap; then
        ok "Morph rounding: VAP kinds $kinds (Tune 1 and Tune 2 rounded, ${rnd#ok } rounded / blended values checked), bit identical to run.py --events"
    else bad "Morph rounding: kinds '$kinds' ($nr r) check '$rnd' rc '${rc:0:60}' ($WORK/pkr.log, $WORK/poke_pkr.out)"; fi
else bad "step 17 Morph target / names: no harness or no step 16 run"; fi

# 18 P12 round 5: Patch Vary (the MK66 patch editor F4 and Shift + F4, Synth::varyPatch / undoVary over the stock patch, poke vary / varyundo
#   with a fixed seed). Run pva, part 1 (ACD): Vary 60 % (enums flip from 50 %), play, undo (the values are the start's bit for bit), play,
#   Vary 10 %, play, undo, a second undo finds nothing. Three plays with the changes between them, the guest stream bit identical to run.py
#   --events. Run pvb, part 1 on VAP: Vary 10 %, play, undo (bit for bit), play, bit identical. Run pvc (P12 round 6), part 1 on VAP: Vary
#   100 % turns Sync 1 on (param 30, the stock lib converts the Ratio set before it), play, undo, play: bit identical because the dump
#   writes every set in its call order (host_plusproxy.cpp dumpSetBefore / dumpSetAfter, before that the stored values in index order did not)
if [ -s "$H/harness.elf" ]; then
    rm -rf "$WORK/card_pva"; mkdir -p "$WORK/card_pva"
    grun pva card_pva "wait 500; instr 65; tap Params; wait 1500; poke patchvals 1; poke vary 1 60 12345; poke patchvals 1; tap Pattern; wait 300; setstep 0 0 60 65 0 0 0 0; audio 1; tap Play; wait 800; tap Play; wait 300; poke varyundo 1; poke patchvals 1; tap Play; wait 800; tap Play; wait 300; poke vary 1 10 777; poke patchvals 1; tap Play; wait 800; tap Play; wait 300; poke varyundo 1; poke varyundo 1"
    rm -rf "$WORK/card_pvb"; mkdir -p "$WORK/card_pvb"
    grun pvb card_pvb "wait 500; instr 65; tap Params; wait 600; pickmodel VAP; wait 300; poke patchvals 1; poke vary 1 10 777; poke patchvals 1; tap Pattern; wait 300; setstep 0 0 60 65 0 0 0 0; audio 1; tap Play; wait 800; tap Play; wait 300; poke varyundo 1; poke patchvals 1; tap Play; wait 800; tap Play; wait 300"
    rm -rf "$WORK/card_pvc"; mkdir -p "$WORK/card_pvc"
    grun pvc card_pvc "wait 500; instr 65; tap Params; wait 600; pickmodel VAP; wait 300; poke patchvals 1; poke vary 1 100 12345; poke patchvals 1; tap Pattern; wait 300; setstep 0 0 60 65 0 0 0 0; audio 1; tap Play; wait 800; tap Play; wait 300; poke varyundo 1; poke patchvals 1; tap Play; wait 800; tap Play; wait 300"
    sync=$(grep '^patchvals part 1' "$WORK/poke_pvc.out" 2>/dev/null | sed 's/.* v //' | cut -d, -f31 | tr '\n' ' ')
    va=$(grep '^patchvals part 1' "$WORK/poke_pva.out" 2>/dev/null | sed 's/.* v //' | tr '\n' '|')
    vb=$(grep '^patchvals part 1' "$WORK/poke_pvb.out" 2>/dev/null | sed 's/.* v //' | tr '\n' '|')
    ca=$(grep '^vary part 1' "$WORK/poke_pva.out" 2>/dev/null | awk '{print $7}' | tr '\n' ' '); ua=$(grep '^varyundo part 1' "$WORK/poke_pva.out" 2>/dev/null | awk '{print $5}' | tr -d '\n')
    ub=$(grep '^varyundo part 1' "$WORK/poke_pvb.out" 2>/dev/null | awk '{print $5}' | tr -d '\n')
    nb=$(awk '$1=="par"{b[$2]=1} END{print length(b)}' "$WORK/guest_pva/part1.events" 2>/dev/null)
    IFS='|' read -r a0 a1 a2 a3 _ <<<"$va"; IFS='|' read -r b0 b1 b2 _ <<<"$vb"
    if [ -n "$a0" ] && [ "$a1" != "$a0" ] && [ "$a2" = "$a0" ] && [ "$a3" != "$a0" ] && [ "$ua" = 110 ] && [ "${nb:-0}" -ge 3 ] \
        && [ -n "$b0" ] && [ "$b1" != "$b0" ] && [ "$b2" = "$b0" ] && [ "$ub" = 1 ] && replay_eq pva 1 acd && replay_eq pvb 1 vap \
        && [ "$sync" = "0 1 0 " ] && replay_eq pvc 1 vap; then
        ok "Patch Vary: ACD 60 % and 10 % (changed $ca) and undo back to the start bit for bit over three plays ($nb blocks with par lines), VAP 10 % and undo, VAP 100 % with Sync 1 on and undo, all three guest streams bit identical to run.py --events"
    else bad "Patch Vary: changed '$ca' undo '$ua'/'$ub' par blocks '$nb' (a0 = a2: $([ "$a2" = "$a0" ] && echo y || echo n), b0 = b2: $([ "$b2" = "$b0" ] && echo y || echo n), pvc Sync 1 '$sync') ($WORK/pva.log, $WORK/poke_pva.out, $WORK/guest_pva)"; fi
else bad "step 18 Patch Vary: no harness (step 6)"; fi

# 19 P12 round 6: Strum timing on a synth part (the shared sequencer code, mtSequencer.cpp sendMidiChordOn / strumQueue / strumService, already
#   compiled on IMX, only the noteTag calls are MK66). Part 1 (ACD), step 0 C4 with MIDI Chord 1 (FX 14, notes 60 62 67) and Strum in the other slot (FX 47).
#   Run pst0 Strum 0: the three note-ons in block 0 at velocity 100. Run pstu Strum 10 (20 ms a note): 60 at block 0, 62 at block 7, 67 at
#   block 14 (the first 2,902 us block at or past 20 and 40 ms, as on the MK66), the ramp of plug/strum_ramp.h 100 96 92. Run pstd Strum
#   74 (-10, top down): 67 62 60 at blocks 0 7 14, 88 84 80. The ACD Para ramp inside the engine stays out (D4). All three bit identical
#   to run.py --events
if [ -s "$H/harness.elf" ]; then
    STRUM_S='wait 500; instr 65; wait 300; setstep 0 0 60 65 14 1 47'; STRUM_P='audio 1; tap Play; wait 800; tap Play; wait 300'
    for r in "pst0 0" "pstu 10" "pstd 74"; do
        rm -rf "$WORK/card_${r% *}"; mkdir -p "$WORK/card_${r% *}"
        grun "${r% *}" "card_${r% *}" "$STRUM_S ${r#* }; $STRUM_P"
    done
    son() { awk '$1=="ev" && $3==4 {printf "%s:%s:%s ", $2, $5, $6}' "$WORK/guest_$1/part1.events" 2>/dev/null; }
    s0=$(son pst0); su=$(son pstu); sd=$(son pstd)
    if [ "$s0" = "0:60:100 0:62:100 0:67:100 " ] && [ "$su" = "0:60:100 7:62:96 14:67:92 " ] && [ "$sd" = "0:67:88 7:62:84 14:60:80 " ] \
        && replay_eq pst0 1 acd && replay_eq pstu 1 acd && replay_eq pstd 1 acd; then
        ok "Strum timing: chord 60 62 67 on ACD, Strum 0 one block, Strum +10 blocks 0 7 14 up, Strum -10 blocks 0 7 14 down, ramp velocities, all three guest streams bit identical to run.py --events"
    else bad "Strum timing: block:note:velocity Strum 0 '$s0' +10 '$su' -10 '$sd' ($WORK/guest_pst0, $WORK/guest_pstu, $WORK/guest_pstd)"; fi
else bad "step 19 Strum timing: no harness (step 6)"; fi

# 20 P12 round 7: the Synth play page on the stock parts (fSynthPlay.cpp + synthPlayRouter.cpp over the stock polysynth:: API, the MIDI in hook
#   of mtMidi.cpp on its IMX path). Synth 2 (instr 65, ACD) is the entry instrument, Menu box 12 (Synth Play, Up then Left from the Pattern box) opens the page: the MIDI in mode is the
#   page's (5), part 1 gets the voices (8) and the other two none. USB MIDI in C4 100 and E4 90 (Linear curve) reach part 1 as note-ons, the bend
#   goes to 0 at entry, the wheel at 4096 is sent as 4096 with Bend 2 (the stock lib's own range, kLibBendSemis), 8191 with Bend 4 (clipped at the lib's range) and 0 with
#   Bend 0, the note-offs, and Pattern leaves the page: CC 123 and the bend back at 0, the MIDI in mode back at 0 and the voices back. The guest
#   stream bit identical to run.py --events
if [ -s "$H/harness.elf" ]; then
    rm -rf "$WORK/card_spl"; mkdir -p "$WORK/card_spl"
    grun spl card_spl "wait 500; instr 65; wait 300; hold Menu; tap Up; tap Left; unhold Menu; wait 800; poke synthplay; audio 1; midion 60 100 300; midion 64 90 300; midibend 1 4096; wait 200; poke synthset 5 4; midibend 1 4096; wait 200; poke synthset 5 0; midibend 1 4096; wait 200; poke synthset 5 2; midioff 60 0 300; midioff 64 0 300; tap Pattern; wait 300; poke synthplay"
    so=$(grep '^splay ' "$WORK/poke_spl.out" 2>/dev/null | head -1 | sed 's/ vel .*//'); sc=$(grep '^splay ' "$WORK/poke_spl.out" 2>/dev/null | tail -1 | sed 's/ vel .*//')
    son=$(awk '$1=="ev" && $3==4 && $4==0 {printf "%s:%s ", $5, $6}' "$WORK/guest_spl/part1.events" 2>/dev/null)
    soff=$(awk '$1=="ev" && $3==5 && $4==0 {printf "%s ", $5}' "$WORK/guest_spl/part1.events" 2>/dev/null)
    sbend=$(awk '$1=="ev" && $3==8 && $4==0 {v=$5+256*$6; if (v>=32768) v-=65536; printf "%d ", v}' "$WORK/guest_spl/part1.events" 2>/dev/null)
    if [ "$so" = "splay open 1 mode 5 part 1 partb 0 voices 0 8 0" ] && [ "$sc" = "splay open 0 mode 0 part 1 partb 0 voices 0 0 0" ] \
        && [ "$son" = "60:100 64:90 " ] && [ "$soff" = "60 64 " ] && [ "$sbend" = "0 4096 8191 0 0 " ] && replay_eq spl 1 acd; then
        ok "Synth play page: opened by Menu box 12 (Synth Play, Up then Left from the Pattern box) on part 1 (voices 0 8 0, MIDI in mode 5), USB notes 60:100 64:90, bend $sbend(entry, Bend 2, 4, 0, exit), closed back to mode 0, bit identical to run.py --events"
    else bad "Synth play page: open '$so' closed '$sc' on '$son' off '$soff' bend '$sbend' ($WORK/spl.log, $WORK/poke_spl.out, $WORK/guest_spl)"; fi
else bad "step 20 Synth play page: no harness (step 6)"; fi

# 21 P12 round 7 step 2: the A/B page's Bank column on the stock parts (Synth::applyPatchBankSlot, the Patch FX bank of step 15). Part 1 (ACD),
#   slot 3 filled at 60 % (poke slotfill), Menu box 12 opens the page on part 1, Right selects Bank A, USB MIDI C4 is held and 90 ms in Up steps
#   Bank A over the empty slots 1 and 2 to slot 3: the part plays slot 3's values (rawvals applied 3) while the note sounds on (no note event
#   between the note-on and the swap, the cell held), the largest sample step around the swap is no larger than the old or the new patch makes
#   (ab_click_check.py: no click, the note still sounds after it). Down goes back to slot 0, Pattern leaves the page with bank A on the
#   project's slot. The guest stream is bit identical to run.py --events
if [ -s "$H/harness.elf" ]; then
    rm -rf "$WORK/card_sab"; mkdir -p "$WORK/card_sab"
    grun sab card_sab "wait 500; instr 65; tap Params; wait 1500; poke slotfill 1 3 60; tap Pattern; wait 300; hold Menu; tap Up; tap Left; unhold Menu; wait 800; poke abstate; poke rawvals 1; tap Right; wait 300; audio 1; midion 60 100; wait 60; tap Up; wait 400; poke abstate; poke rawvals 1; poke synthplay; midioff 60; wait 300; tap Down; wait 200; poke abstate; tap Pattern; wait 300; poke abstate"
    ab=$(grep '^abstate partb' "$WORK/poke_sab.out" 2>/dev/null | sed 's/abstate partb 0 morph 50 //' | tr '\n' '|')
    r0=$(grep '^rawvals part 1' "$WORK/poke_sab.out" 2>/dev/null | head -1); r1=$(grep '^rawvals part 1' "$WORK/poke_sab.out" 2>/dev/null | tail -1)
    sh=$(grep '^splay ' "$WORK/poke_sab.out" 2>/dev/null | tail -1)
    son=$(awk '$1=="ev" && $3==4 {printf "%s:%s ", $5, $6}' "$WORK/guest_sab/part1.events" 2>/dev/null)
    soff=$(awk '$1=="ev" && $3==5 {printf "%s ", $5}' "$WORK/guest_sab/part1.events" 2>/dev/null)
    clk=$("$PY" -I "$HERE/ab_click_check.py" "$WORK/guest_sab/part1.f32" "$WORK/guest_sab/part1.events" 2>&1); crc=$?
    if grep -q '^slotfill part 1 slot 3 t 60 select 2 saved 1 back 1$' "$WORK/poke_sab.out" && [ "$ab" = "banka 0 bankb 0|banka 3 bankb 0|banka 0 bankb 0|banka 0 bankb 0|" ] \
        && [[ "$r0" == *" applied -1 "* ]] && [[ "$r1" == *" applied 3 "* ]] && [ "${r0##* }" != "${r1##* }" ] && [[ "$sh" == *" cells 60:1" ]] \
        && [ "$son" = "60:100 " ] && [ "$soff" = "60 " ] && [ $crc -eq 0 ] && replay_eq sab 1 acd; then
        ok "A/B Bank A: slot 3 swapped in with C4 held (applied 3, cell 60:1, one note-on and one note-off), ${clk#abclick }, bit identical to run.py --events"
    else bad "A/B Bank A: banks '$ab' r0 '${r0:0:50}' r1 '${r1:0:50}' held '$sh' on '$son' off '$soff' click '$clk' ($WORK/sab.log, $WORK/poke_sab.out, $WORK/guest_sab)"; fi
else bad "step 21 A/B Bank: no harness (step 6)"; fi

# 11 P2 (ref/ff-imx-port-plan.md): the IMX build hygiene gate (imx_check.sh mini): the -D set of `make -n mini` equals this sim's, the core objects compile
# -Wall -Wextra -Werror in a fresh sim build, the real compiler's own syntax pass (no object, no link, no make mini). Own build dir under build/imx-check
if DEVTOOLS_DIR="$DEVTOOLS_DIR" IMX_CHECK_OUT="$WORK/imx-check" bash "$HERE/imx_check.sh" mini >"$WORK/imx_check.log" 2>&1; then ok "imx_check mini: DEFS parity with make mini, -Werror sim build, gcc syntax pass ($WORK/imx_check.log)"
else tail -15 "$WORK/imx_check.log"; bad "imx_check mini (log $WORK/imx_check.log)"; fi

# 12 P10 H7 (ref/ff-imx-port-plan.md R2.1g): the extension module loader (synth-open/ext_imx.cpp) with the devtools IMX module under Unicorn
# (host_extimx.cpp): /.develop gate, load + cache steps + status.txt, console verbs through the command hook, refusal and crash rows (ext_imx_rows.sh).
# The module source is poly-devtools firmware/ext_imx (EXT_IMX_DEVTOOLS names a checkout with it when DEVTOOLS_DIR has none)
EXTDT=""
for d in "${EXT_IMX_DEVTOOLS:-}" "$DEVTOOLS_DIR" "$HOME/prj/poly-devtools/.wt/imx-p10"; do
    [ -n "$d" ] && [ -x "$d/firmware/ext_imx/build_imx_module.sh" ] && { EXTDT=$d; break; }
done
if [ -z "$EXTDT" ]; then bad "ext module: no poly-devtools checkout with firmware/ext_imx (EXT_IMX_DEVTOOLS)"
elif ! bash "$EXTDT/firmware/ext_imx/build_imx_module.sh" "$ROOT" "$WORK/ext-module" >"$WORK/ext_module_build.log" 2>&1; then
    tail -10 "$WORK/ext_module_build.log"; bad "ext module build (log $WORK/ext_module_build.log)"
else
    while IFS= read -r line; do
        case "$line" in ok*) ok "${line#ok    }";; FAIL*) bad "${line#FAIL  }";; esac
    done < <(bash "$HERE/ext_imx_rows.sh" "$BIN" "$WORK/ext-rows" "$WORK/ext-module/module.plg" mini 2>&1)
fi

# 14 P7 H5 (ref/ff-imx-port-plan.md): plugin synth engines play on the stock synth stack. A card with pluck.plg (PLK) and alco.plg (ALCO) from
#   PLUG_CARD (default the OG emu card): part 0 picks PLK and part 1 picks ALCO through the Model list (pickmodel, so Synth::attachCardEngine
#   loads each .plg into a code slot of the guest and PlugSynthVoice plays it), one note each renders non-silent. SKIP without the card
PLUG_CARD="${PLUG_CARD:-$HOME/prj/poly/build/emu/current/card/Plugins/Synth}"
if [ ! -s "$PLUG_CARD/pluck.plg" ] || [ ! -s "$PLUG_CARD/alco.plg" ]; then ok "plugin engines: SKIP (no pluck.plg and alco.plg in PLUG_CARD=$PLUG_CARD)"
elif [ -s "$H/harness.elf" ]; then
    rm -rf "$WORK/card_plug"; mkdir -p "$WORK/card_plug/Plugins/Synth"; cp "$PLUG_CARD/pluck.plg" "$PLUG_CARD/alco.plg" "$WORK/card_plug/Plugins/Synth/"
    grun plug card_plug "wait 500; instr 64; tap Params; wait 1500; pickmodel PLK; wait 300; tap Pattern; wait 300; instr 65; tap Params; wait 1500; pickmodel ALCO; wait 300; tap Pattern; wait 300; poke modelrow 0; poke modelrow 1; setstep 0 0 60 64 0 0 0 0; audio 1; wav $WORK/plug_plk.wav; tap Play; wait 800; tap Play; wait 300; wav off; setstep 0 0 60 65 0 0 0 0; wav $WORK/plug_alco.wav; tap Play; wait 800; tap Play; wait 300; wav off"
    read -r p1 n1 < <(peak "$WORK/plug_plk.wav" 2>/dev/null || echo "0 0")
    read -r p2 n2 < <(peak "$WORK/plug_alco.wav" 2>/dev/null || echo "0 0")
    if grep -q '^pickmodel PLK ok' "$WORK/plug.log" && grep -q '^pickmodel ALCO ok' "$WORK/plug.log" && [ "${p1:-0}" -gt 0 ] && [ "${p2:-0}" -gt 0 ] \
        && ! grep -q 'h2_plug_attach' "$WORK/plug.log"; then
        ok "plugin engines: part 0 PLK peak $p1, part 1 ALCO peak $p2 ($n1 / $n2 frames), both picked from the stock Model list ($(grep -h '^modelrow' "$WORK/poke_plug.out" 2>/dev/null | tr '\n' ' '))"
    else bad "plugin engines: $(grep -h '^pickmodel' "$WORK/plug.log" | tr '\n' ' ') peaks ${p1:-?} ${p2:-?} ($WORK/plug.log)"; fi
else bad "step 14 plugin engines: no harness (step 6)"; fi
# store (release-1.4.1-int step 11): the project store without the plugin host or the open synth stack (core/makefile PROJECT_STORE on PLATFORM=IMX): a Workspace save
#   writes project.mtff, a restart reads it back, Save As and Open carry it (imx_store_sim.sh)
if bash "$HERE/imx_store_sim.sh" "$BIN" mini >"$WORK/imx_store.log" 2>&1; then ok "project store: $(tail -1 "$WORK/imx_store.log") (Workspace save, restart, Save As, Open)"
else grep -E '^FAIL' -A2 "$WORK/imx_store.log"; bad "project store (log $WORK/imx_store.log)"; fi

# 15 poly-sim-imx-midi: the MIDI of this sim is the REAL IMX driver (usb_midi.c, UsbMidiImpl, UartMidiImpl) over the byte level stub (host_imxmidi.cpp): the
#   driver symbols are linked (not the HUsbMidi stand-in), and imx_midi_mpe_sim.sh runs the MPE in rows (per voice bend, pressure, master bend, note off,
#   DIN) on a plain mini sim with raw bytes injected below the driver. Own build dir build/ui-shot-mini (the open Synth play page lives in this flavour)
nmx=$(nm "$BIN" | c++filt)
if grep -q 'UsbMidiImpl::read' <<<"$nmx" && grep -q 'UartMidiImpl::read' <<<"$nmx" && grep -q 'midiUsbReceive' <<<"$nmx" && grep -q 'PlusUsbMidi' <<<"$nmx"; then ok "real IMX MIDI driver linked (UsbMidiImpl::read, UartMidiImpl::read, midiUsbReceive, the host seam PlusUsbMidi over it)"
else bad "real IMX MIDI driver not linked"; fi
if IMX_MIDI_FLAVOURS=mini bash "$HERE/imx_midi_sim.sh" in >"$WORK/imx_midi_in.log" 2>&1; then ok "MPE in through the real IMX MIDI driver on the mini sim: $(grep -c '^ok' "$WORK/imx_midi_in.log") rows ($WORK/imx_midi_in.log)"
else grep -E '^FAIL' -A2 "$WORK/imx_midi_in.log" | head -12; bad "MPE in on the mini sim (log $WORK/imx_midi_in.log)"; fi

if [ "$fails" = 0 ]; then echo "mini_smoke: PASS ($WORK)"; exit 0; fi
echo "mini_smoke: FAIL ($fails)"; exit 1
