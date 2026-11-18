#!/usr/bin/env bash
# The host test suite of a tracker-ff tree (Mac or Linux, nothing here touches a device or builds a firmware image):
#   - run_smoke.sh in 5 variants (the five built-in engines, TRIM=1, PLUGENG=1: a run time plugin engine stand-in)
#   - plug/build_plugs.sh: the two worked examples of docs/plugin_sdk.md, built and run in the emulator
#   - the plugin host unit tests (plug_*_test.cpp, sat_softknee_test.cpp, seq_tick_test.cpp, kern_*_test.cpp), the index and logo tests against the pinned modules
# Ends with one verdict line: "RESULT: GREEN" (exit 0) or "RESULT: RED (...)" (exit 1). Logs in OUTDIR/logs.
#
#   synth-open/tools/run_tests.sh [WORKTREE [OUTDIR]]
#     WORKTREE  the tree to test, default the one this script is in
#     OUTDIR    logs, the example plugins and the fetched modules, default WORKTREE/build/tests (git-ignored)
#   Environment:
#     TOOLCHAIN       Arm GNU prefix (.../bin/arm-none-eabi-) for build_plugs.sh. Default: WORKTREE/agt/12.3.rel1/bin/arm-none-eabi-
#                     when it is there, else arm-none-eabi-gcc on PATH
#     PY              a python3 with unicorn, capstone and numpy (the emulators). Default: the pinned ~/prj/poly/build/pyenv/bin/python,
#                     made once by `sh synth-open/tools/pyenv.sh` (a missing one stops the run at once). An explicit PY wins
#     PLUGDIR         the shipped plugins (.plg, .plgi) for the index and logo tests. Default: the pinned modules of
#                     engines/external.lock.json, fetched into OUTDIR/modules from the local module builds (external.py fetch --from-local)
#     NO_BUILD_PLUGS  set: skip build_plugs.sh (run_host_tests.sh)
#     CHIP_PLUGIN_SRC engine_chip.cpp of tracker-engines-os (CHIP is chip.plg): adds run_smoke.sh variants that link that engine and the
#                     Polyend voice natively into the smoke harness (not ui_shot) and list CHIP as a plugin engine at stored 6 (the engine
#                     tests of CHIP, all-engine loops with it). Default: the file at the pinned tag of the engines-os set (external.py
#                     source). Without a checkout of that repository the CHIP variants are skipped and the run says so
#     UNICORN_DIR BRIDGE_AXF  the emulator bridge build of ui_shot (ui_shot/bridge_env.sh): the sims that run plugin bytes (septfx_sim,
#                     fx34_compat_sim) build it. Default: the pinned Unicorn wheel unpacked into build/unicorn and this tree's release axf
#                     (make original-release, run here with TOOLCHAIN when the axf is missing)
#     ENG CARD        the bridge_check leg: a poly-engines checkout (default ~/prj/poly-engines) and the pinned emu card (default
#                     ~/prj/poly/build/emu/current/card). Without either the leg prints SKIP and the reason and passes
#     EXT_MODULE      "module.plg IMAGE_CRC RUN_ADDRESS": ext_loader_test also loads that extension module (pack_ext.py output)
#     SMOKE_ARCH      run_smoke.sh architecture, default arm64 here (native on Apple silicon). x86_64 runs under Rosetta
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
wt=$(cd "${1:-$HERE/../..}" && pwd) || { echo "no worktree ${1:-}"; echo "RESULT: RED (no worktree)"; exit 1; }
out=${2:-$wt/build/tests}
mkdir -p "$out" && out=$(cd "$out" && pwd)
cd "$wt" || exit 1
[ -f synth-open/tools/run_smoke.sh ] || { echo "$wt is not a tracker-ff tree"; echo "RESULT: RED (not a tree)"; exit 1; }

if [ -z "${TOOLCHAIN:-}" ]; then
  if [ -x "$wt/agt/12.3.rel1/bin/arm-none-eabi-gcc" ]; then TOOLCHAIN=$wt/agt/12.3.rel1/bin/arm-none-eabi-
  elif command -v arm-none-eabi-gcc > /dev/null; then TOOLCHAIN=$(dirname "$(command -v arm-none-eabi-gcc)")/arm-none-eabi-
  fi
fi
export TOOLCHAIN=${TOOLCHAIN:-}
if [ -z "${CHIP_PLUGIN_SRC:-}" ]; then
  if python3 -I engines/external.py source engines-os "$out/src" engine_chip.cpp chip_voice.cpp chip_share.h > "$out/chip_src.log" 2>&1; then CHIP_PLUGIN_SRC=$out/src/engine_chip.cpp
  else echo "CHIP plugin source: not available, the CHIP smoke variants are skipped ($(tail -1 "$out/chip_src.log"))"; fi
fi
export CHIP_PLUGIN_SRC=${CHIP_PLUGIN_SRC:-}
PY=${PY:-$(sh "$HERE/pyenv.sh" python)} || { echo "RESULT: RED (no python)"; exit 1; }
export PY
export SMOKE_ARCH=${SMOKE_ARCH:-arm64}
echo "tree $wt ($(git rev-parse --short HEAD 2>/dev/null || echo '?'))"
echo "out $out"
echo "toolchain ${TOOLCHAIN:-none} ($( [ -n "$TOOLCHAIN" ] && "${TOOLCHAIN}gcc" -dumpversion 2>/dev/null))"
echo "python $PY"

L=$out/logs; rm -rf "$L"; mkdir -p "$L" "$out/plugins"
T=$(mktemp -d "${TMPDIR:-/tmp}/tff_tests.XXXXXX")
trap 'rm -rf "$T"' EXIT
bad=0

echo "### firmware names"
bash synth-open/tools/check_ptf_name_test.sh | tail -1 || bad=$((bad+1))
bash synth-open/tools/release_gate_tail_test.sh | tail -1 || bad=$((bad+1))
bash synth-open/tools/package_zip_test.sh | tail -1 | grep -q PASS$ || { echo "FAIL package_zip_test"; bad=$((bad+1)); }
python3 -I synth-open/tools/tail_pad_test.py | tail -1 || bad=$((bad+1))
bash synth-open/tools/icons_inflate_test.sh | tail -1 || bad=$((bad+1))
bash synth-open/tools/font_inflate_test.sh | tail -1 || bad=$((bad+1))
bash synth-open/tools/randname_test.sh | tail -1 || bad=$((bad+1))
bash synth-open/tools/playorder_test.sh | tail -1 || bad=$((bad+1))
bash synth-open/tools/fxnames_test.sh | tail -1 || bad=$((bad+1))

echo "### smoke"
{
# Only the five built-in engines can be linked in (engines.mk), every other engine is a plugin (CHIP too since 1.3.0).
# PLUGENG=1 adds a run time plugin engine (a 100 engine library, missing numbers, names.txt).
for v in \
 "|ACD FAT VAP WTFM" "TRIM=1|ACD FAT VAP WTFM PERC" "TRIM=1 PLUGENG=1|ACD FAT VAP WTFM PERC" \
 "|ACD FAT VAP WTFM PERC" "PLUGENG=1|ACD FAT VAP WTFM PERC" ; do
  e=${v%%|*}; s=${v#*|}
  echo "### env $e | $s"
  env $e sh synth-open/tools/run_smoke.sh "$s"; echo "### rc=$?"
done
if [ -n "${CHIP_PLUGIN_SRC:-}" ]; then
  for e in "TRIM=1" "TRIM=1 PLUGENG=1"; do
    echo "### env $e CHIP_PLUGIN_SRC | ACD FAT VAP WTFM PERC"
    env $e CHIP_PLUGIN_SRC="$CHIP_PLUGIN_SRC" sh synth-open/tools/run_smoke.sh "ACD FAT VAP WTFM PERC"; echo "### rc=$?"
  done
fi; } > "$L/smoke.log" 2>&1
grep -c '^### rc=0' "$L/smoke.log" | sed 's/^/smoke variants rc=0: /'; grep '^### rc=' "$L/smoke.log" | grep -v 'rc=0' | sed 's/^/smoke NONZERO /'

if [ -z "${NO_BUILD_PLUGS:-}" ]; then
  echo "### build_plugs"
  if [ -z "$TOOLCHAIN" ]; then
    echo "build_plugs: no toolchain (set TOOLCHAIN)"; bprc=1
  elif ! "$PY" -c 'import unicorn' 2> /dev/null; then
    echo "build_plugs: $PY has no unicorn module (set PY)"; bprc=1
  else
    (bash synth-open/plug/build_plugs.sh "$out/plugins") > "$L/build_plugs.log" 2>&1; bprc=$?
  fi
  echo "build_plugs rc=$bprc"
  [ "$bprc" -eq 0 ] || { echo "build_plugs FAILED"; bad=$((bad+1)); }
fi

# the shipped plugins the index and logo tests read: $PLUGDIR, else the pinned modules (engines/external.lock.json) from the local module
# builds (build_plugs.sh builds only the SDK examples since the five repository split)
if [ -z "${PLUGDIR:-}" ]; then
  PLUGDIR=$out/modules; rm -rf "$PLUGDIR"
  python3 -I engines/external.py fetch "$PLUGDIR" --from-local > "$L/fetch_modules.log" 2>&1 ||
    { echo "fetch of the pinned modules FAILED (set PLUGDIR), see $L/fetch_modules.log"; bad=$((bad+1)); }
fi
echo "plugins for the index and logo tests: $PLUGDIR"

echo "### host unit tests"
python3 -I synth-open/tools/waves_curated.py check > "$L/waves_curated.log" 2>&1 || { echo "FAIL waves_curated check (releases/waves vs MANIFEST.txt), see $L/waves_curated.log"; bad=$((bad+1)); }
TESTS="plug_bank_test plug_index_test plug_logo_test plug_ui_nav_test plug_defer_test sat_softknee_test plug_work_test plug_scan_cache_test synth_order_test plug_transport_test plug_files_test wave_name_test"
for t in $TESTS; do
  c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/$t.cpp -o "$T/$t" > "$L/$t.build.log" 2>&1 || echo "BUILD FAIL $t"
done
c++ -std=c++20 -O1 -Wall -I synth-open -I synth-open/tools/plug_stub synth-open/tools/plug_sidecar_test.cpp -o "$T/plug_sidecar_test" \
  > "$L/plug_sidecar_test.build.log" 2>&1 || echo "BUILD FAIL plug_sidecar_test"
# project.mtff: the codec and the file rules on an in-memory card (power loss at every step), unknown lines kept, stale stamp, nested paths
c++ -std=c++20 -O1 -Wall -Wextra -I synth-open -I synth-open/tools/plug_stub synth-open/tools/plug_mtff_test.cpp -o "$T/plug_mtff_test" \
  > "$L/plug_mtff_test.build.log" 2>&1 || echo "BUILD FAIL plug_mtff_test"
# the integer sequencer clock against the stock double accumulator (core/source/internalTickClock.h), 1 simulated minute per run here
c++ -std=c++20 -O2 -Wall -I core/source synth-open/tools/seq_tick_test.cpp -o "$T/seq_tick_test" > "$L/seq_tick_test.build.log" 2>&1 \
  || echo "BUILD FAIL seq_tick_test"
# the sequencer's private random generator of the seeded random takes (core/source/seqRandom.h)
c++ -std=c++20 -O2 -Wall -Wextra -I core/source synth-open/tools/seq_random_test.cpp -o "$T/seq_random_test" > "$L/seq_random_test.build.log" 2>&1 \
  || echo "BUILD FAIL seq_random_test"
# the Tape FX arithmetic (Stop 1-64, Start 65-128) and the id 51 load migration (core/source/tapeFx.h)
c++ -std=c++20 -O2 -Wall -I core/source synth-open/tools/tape_fx_test.cpp -o "$T/tape_fx_test" > "$L/tape_fx_test.build.log" 2>&1 \
  || echo "BUILD FAIL tape_fx_test"
# the CPU kernels of core/libs/Audio (the firmware includes the very headers the tests include): old loop against new loop, bit for bit
c++ -std=c++20 -O2 -Wall -I core/libs/Audio synth-open/tools/kern_sample_read_test.cpp -o "$T/kern_sample_read_test" \
  > "$L/kern_sample_read_test.build.log" 2>&1 || echo "BUILD FAIL kern_sample_read_test"
c++ -std=c++20 -O2 -Wall -I synth-open/tools/kern_host -I core/libs/Audio synth-open/tools/kern_env_steady_test.cpp -o "$T/kern_env_steady_test" \
  > "$L/kern_env_steady_test.build.log" 2>&1 || echo "BUILD FAIL kern_env_steady_test"
c++ -std=c++20 -O2 -Wall -I synth-open/tools/kern_host -I core/libs/Audio synth-open/tools/kern_gain_copy_test.cpp -o "$T/kern_gain_copy_test" \
  > "$L/kern_gain_copy_test.build.log" 2>&1 || echo "BUILD FAIL kern_gain_copy_test"
c++ -std=c++20 -O2 -ffp-contract=fast -Wall -I core/libs/Audio synth-open/tools/kern_synth_part_test.cpp -o "$T/kern_synth_part_test" \
  > "$L/kern_synth_part_test.build.log" 2>&1 || echo "BUILD FAIL kern_synth_part_test"
# the Parts sidecar codec and song order (core/source/parts_format.h)
c++ -std=c++20 -O1 -Wall -I core/source synth-open/tools/parts_format_test.cpp -o "$T/parts_format_test" \
  > "$L/parts_format_test.build.log" 2>&1 || echo "BUILD FAIL parts_format_test"
c++ -std=c++20 -O1 -Wall -I synth-open -I core/include synth-open/tools/ext_loader_test.cpp -o "$T/ext_loader_test" \
  > "$L/ext_loader_test.build.log" 2>&1 || echo "BUILD FAIL ext_loader_test"
c++ -std=c++20 -O1 -Wall -Wextra -I synth-open -I core/include synth-open/tools/ext_recovery_test.cpp -o "$T/ext_recovery_test" \
  > "$L/ext_recovery_test.build.log" 2>&1 || echo "BUILD FAIL ext_recovery_test"
# the chord matcher and the chord names of the chord capture (core/source/mtChord.cpp), plain and with ASan/UBSan
CM=(-std=c++20 -O1 -Wall -I core/source synth-open/tools/chord_match_test.cpp core/source/mtChord.cpp)
c++ "${CM[@]}" -o "$T/chord_match_test" > "$L/chord_match_test.build.log" 2>&1 || echo "BUILD FAIL chord_match_test"
c++ -g -fsanitize=address,undefined -fno-sanitize-recover=all "${CM[@]}" -o "$T/chord_match_test_san" > "$L/chord_match_test_san.build.log" 2>&1 \
  || echo "BUILD FAIL chord_match_test_san"
# the hand roll of a recognised chord: the Strum FX value and its slot (core/source/mtRoll.h, header only), plain and with ASan/UBSan
SR=(-std=c++20 -O1 -Wall -Wextra -I core/source synth-open/tools/strum_roll_test.cpp)
c++ "${SR[@]}" -o "$T/strum_roll_test" > "$L/strum_roll_test.build.log" 2>&1 || echo "BUILD FAIL strum_roll_test"
c++ -g -fsanitize=address,undefined -fno-sanitize-recover=all "${SR[@]}" -o "$T/strum_roll_test_san" > "$L/strum_roll_test_san.build.log" 2>&1 \
  || echo "BUILD FAIL strum_roll_test_san"
# the scale snap function (core/source/scales.cpp, per track snap at playback): every note x root x scale x mode against a brute force
SS=(-std=c++20 -O1 -Wall -DFEATURE_PLUG_HOST -DAUDIO_CORE_RAM= -I synth-open/tools/scale_stub -I core/source synth-open/tools/scale_snap_test.cpp core/source/scales.cpp)
c++ "${SS[@]}" -o "$T/scale_snap_test" > "$L/scale_snap_test.build.log" 2>&1 || echo "BUILD FAIL scale_snap_test"
# the pattern idea generator of the Fill dialog (core/source/mtPatternGen.cpp, needs scales.cpp), plain and with ASan/UBSan
PG=(-std=c++20 -O1 -Wall -Wextra -I synth-open/tools/kern_host -I core/source synth-open/tools/pattern_gen_test.cpp core/source/mtPatternGen.cpp core/source/scales.cpp)
c++ "${PG[@]}" -o "$T/pattern_gen_test" > "$L/pattern_gen_test.build.log" 2>&1 || echo "BUILD FAIL pattern_gen_test"
c++ -g -fsanitize=address,undefined -fno-sanitize-recover=all "${PG[@]}" -o "$T/pattern_gen_test_san" > "$L/pattern_gen_test_san.build.log" 2>&1 \
  || echo "BUILD FAIL pattern_gen_test_san"
FC=(-std=c++20 -O1 -Wall -I core/source synth-open/tools/follow_chord_test.cpp core/source/mtFollowChord.cpp)
c++ "${FC[@]}" -o "$T/follow_chord_test" > "$L/follow_chord_test.build.log" 2>&1 || echo "BUILD FAIL follow_chord_test"
c++ -g -fsanitize=address,undefined -fno-sanitize-recover=all "${FC[@]}" -o "$T/follow_chord_test_san" > "$L/follow_chord_test_san.build.log" 2>&1 \
  || echo "BUILD FAIL follow_chord_test_san"
# the extension ids of the plugin ABI: all distinct, PLUG_EXT_CHORD off the 5 of PLUG_EXT_FILES (host level 2 line)
c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_abi_ids_test.cpp -o "$T/plug_abi_ids_test" \
  > "$L/plug_abi_ids_test.build.log" 2>&1 || echo "BUILD FAIL plug_abi_ids_test"
# the Features registry and its file parser (core/source/features.cpp), plain and with ASan/UBSan
# the Synth play page's voice router (core/source/synthPlayRouter.cpp): notes, velocity curves, the 8 note table, sustain, CC 123 / 120 / 121, exit cleanup, bend, filters
SPT=(-std=c++20 -O1 -Wall -Wextra -DSYNTHPLAY_TEST -I core/source synth-open/tools/synth_play_test.cpp core/source/synthPlayRouter.cpp)
c++ "${SPT[@]}" -o "$T/synth_play_test" > "$L/synth_play_test.build.log" 2>&1 || echo "BUILD FAIL synth_play_test"
c++ -g -fsanitize=address,undefined -fno-sanitize-recover=all "${SPT[@]}" -o "$T/synth_play_test_san" > "$L/synth_play_test_san.build.log" 2>&1 \
  || echo "BUILD FAIL synth_play_test_san"
# the Synth play page REC capture (core/source/synthPlayRec.cpp): the mono mix, whole blocks, the stop at the capacity, the trailing silence trim
SRT=(-std=c++20 -O1 -Wall -Wextra -DSYNTHPLAYREC_TEST -I core/source synth-open/tools/synth_rec_test.cpp core/source/synthPlayRec.cpp)
c++ "${SRT[@]}" -o "$T/synth_rec_test" > "$L/synth_rec_test.build.log" 2>&1 || echo "BUILD FAIL synth_rec_test"
c++ -g -fsanitize=address,undefined -fno-sanitize-recover=all "${SRT[@]}" -o "$T/synth_rec_test_san" > "$L/synth_rec_test_san.build.log" 2>&1 \
  || echo "BUILD FAIL synth_rec_test_san"
FT=(-std=c++20 -O1 -Wall -DFEATURE_EXT_HOOKS -DFEATURES_TEST -I core/source -I core/include synth-open/tools/features_test.cpp core/source/features.cpp)
c++ "${FT[@]}" -o "$T/features_test" > "$L/features_test.build.log" 2>&1 || echo "BUILD FAIL features_test"
c++ -g -fsanitize=address,undefined -fno-sanitize-recover=all "${FT[@]}" -o "$T/features_test_san" > "$L/features_test_san.build.log" 2>&1 \
  || echo "BUILD FAIL features_test_san"
# Skip-back: the SDRAM ring of the master output and its WAV save (core/source/skipback.cpp), plain and with ASan/UBSan
SB=(-std=c++20 -O1 -Wall -Wextra -DFEATURE_EXT_HOOKS -DSKIPBACK_TEST -I core/source synth-open/tools/skipback_test.cpp core/source/skipback.cpp)
c++ "${SB[@]}" -o "$T/skipback_test" > "$L/skipback_test.build.log" 2>&1 || echo "BUILD FAIL skipback_test"
c++ -g -fsanitize=address,undefined -fno-sanitize-recover=all "${SB[@]}" -o "$T/skipback_test_san" > "$L/skipback_test_san.build.log" 2>&1 \
  || echo "BUILD FAIL skipback_test_san"
{
 for t in plug_bank_test plug_ui_nav_test plug_defer_test sat_softknee_test plug_sidecar_test plug_mtff_test plug_work_test plug_scan_cache_test synth_order_test plug_transport_test plug_files_test wave_name_test; do
   echo "### run $t"; "$T/$t"; echo "### rc=$?"
 done
 # the Strum ramp depth: depth 0 == STRUM_RAMP=0, depth 100 == the ramp before the depth (digests over every input)
 echo "### run strum_depth_test"; bash synth-open/tools/run_strum_depth_test.sh "$wt"; echo "### rc=$?"
 # EXT_MODULE="module.plg IMAGE_CRC RUN_ADDRESS" (synth-open/plug/pack_ext.py prints them) adds the real module checks
 read -r -a ext_args <<< "${EXT_MODULE:-}"
 echo "### run ext_loader_test"; "$T/ext_loader_test" ${ext_args[@]+"${ext_args[@]}"}; echo "### rc=$?"
 # the recovery console and the status line of a refused module (with EXT_MODULE also over the real file)
 echo "### run ext_recovery_test"; "$T/ext_recovery_test" ${ext_args[@]+"${ext_args[@]}"}; echo "### rc=$?"
 echo "### run plug_index_test"; "$T/plug_index_test" "$PLUGDIR"; echo "### rc=$?"
 echo "### run seq_tick_test"; "$T/seq_tick_test" 1; echo "### rc=$?"
 echo "### run seq_random_test"; "$T/seq_random_test"; echo "### rc=$?"
 echo "### run tape_fx_test"; "$T/tape_fx_test"; echo "### rc=$?"
 echo "### run kern_sample_read_test"; "$T/kern_sample_read_test"; echo "### rc=$?"
 echo "### run kern_env_steady_test"; "$T/kern_env_steady_test"; echo "### rc=$?"
 echo "### run kern_gain_copy_test"; "$T/kern_gain_copy_test"; echo "### rc=$?"
 echo "### run kern_record_queue_test"; sh synth-open/tools/kern_record_queue_test.sh; echo "### rc=$?"
 echo "### run kern_synth_part_test"; "$T/kern_synth_part_test" full; echo "### rc=$?"
 make -s -C synth-open/tools/ui_shot -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" > "$L/ui_shot_make.log" 2>&1   # a build left from an older tree has no fxmute poke
 # send ducking (duck_math.h, effect_duck.h) on the real AudioStream and mixer: model, thresholds, off, dry path untouched. Needs the host copy of
 # libs/Audio that make -C synth-open/tools/ui_shot generates (build/ui-shot/gen)
 echo "### run duck_test"; sh synth-open/tools/duck_test.sh; echo "### rc=$?"
 # FX mute sleep and USB audio 8+3 mono in the real engine (ui_shot): the sleep states of the Mixer Mute, the tap and interleave bit exact against the Stems tap points
 echo "### run fx_mute_sim"; bash synth-open/tools/ui_shot/fx_mute_sim.sh "$wt/build/ui-shot/ui_shot" "$out/fx-mute-sim" | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run usb_multich_sim"; OUT="$out/ui-shot-usbmc" bash synth-open/tools/ui_shot/usb_multich_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run umc_automute_sim"; OUT="$out/ui-shot-ext" bash synth-open/tools/ui_shot/umc_automute_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # Config > Features and Config > USB (USB audio, USB drive, Audio mode, Restart now, Storage) on the real UI with the goldens. features.sh needs the EXTHOOKS=1 build
# (its Features list has no USB drive row), usb_menu_sim.sh and usb_msc_sim.sh the EXTHOOKS=1 USB_MSC=1 build (the drive rows and the lend of the card)
 make -s -C synth-open/tools/ui_shot EXTHOOKS=1 OUT="$out/ui-shot-ext" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" > "$L/ui_shot_ext_make.log" 2>&1
 make -s -C synth-open/tools/ui_shot EXTHOOKS=1 USB_MSC=1 OUT="$out/ui-shot-msc" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" > "$L/ui_shot_msc_make.log" 2>&1
 echo "### run features_sim"; bash synth-open/tools/ui_shot/features.sh "$out/ui-shot-ext/ui_shot" "$out/features-sim" | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # ALCO's wave file rows on the bridge build (the real alco.plg): hidden without /Waves, Sine, names, Missing, stored paths, byte equal goldens
 echo "### run wave_label_sim"; PLG_DIR="$PLUGDIR" bash synth-open/tools/ui_shot/wave_label_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run usb_menu_sim"; bash synth-open/tools/ui_shot/usb_menu_sim.sh "$out/ui-shot-msc/ui_shot" "$out/usb-menu-sim" | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run usb_msc_sim"; bash synth-open/tools/ui_shot/usb_msc_sim.sh "$out/ui-shot-msc/ui_shot" "$out/usb-msc-sim" | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # MOD import, IT import and IT export (modit.plg): the built in code and the module path (renamed sources behind modit_dispatch.cpp and the image
 # forwarders, EXT=1) against the same goldens, the popups and the first use load on the real Project screen, the module binding checks of the gate and package
 echo "### run modit_golden_sim"; bash synth-open/tools/ui_shot/modit_golden_sim.sh | tail -9; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run modit_golden_sim_ext"; EXT=1 bash synth-open/tools/ui_shot/modit_golden_sim.sh | tail -9; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run modit_popup_sim"; bash synth-open/tools/ui_shot/modit_popup_sim.sh | tail -16; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run modit_pack_test"; bash synth-open/tools/modit_pack_test.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # what a synth part does with its notes: the per note ids (S-ID1..S-ID6), the queued note on, the render hashes of the same pitch class (SAN=address,undefined runs the sanitizer build)
 echo "### run synth_nna_sim"; bash synth-open/tools/ui_shot/synth_nna_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # the Sample Loader enters a folder while its list is drawn: no NULL row text reaches cList::update (SLB1..SLB5, own binary)
 echo "### run sample_loader_browse_sim"; bash synth-open/tools/ui_shot/sample_loader_browse_sim.sh | tail -7; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # the nine Sample Editor effects as card files (-DFW_SE_PTFX, the pinned se_*.ptfx of $PLUGDIR) against the stock build: every effect applied through the UI
 # equals the built in one, a card without the files shows Missing: <name> and Apply is a no-op (SE1..SE8, the card files' own bytes on the
 # emulator bridge build)
 echo "### run septfx_sim"; PTFX_DIR="$PLUGDIR" bash synth-open/tools/ui_shot/septfx_sim.sh | tail -4; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # the MIDI remote key and pad state in the real Keys, ButtonsBaseImpl and midiRemote.cpp (host stand-ins, ASan and UBSan)
 echo "### run remote_held_test"; bash synth-open/tools/remote_held/remote_held_test.sh; echo "### rc=$?"
 # FX3 and FX4 playback (the 4 FX project switch): ranks 2 and 3 on sample, MIDI and synth tracks, the priority FX2 > FX1 > FX4 > FX3, the pairs
 echo "### run fx34_play_sim"; bash synth-open/tools/ui_shot/fx34_play_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # FX3 and FX4 of a pattern step (the 4 FX project switch): the sidecar files, undo, Parts, Save As, the stale rule, the cap. The compat sim builds ui_shot
 # of release-1.4.0-int and of tracker-ff-v1.3.0 from git archives into build/rel140-4fx/ the first time (about 2 minutes)
 echo "### run fx34_store_sim"; bash synth-open/tools/ui_shot/fx34_store_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run fx34_compat_sim"; bash synth-open/tools/ui_shot/fx34_compat_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # the pattern editor side: FX bank, edit operations that carry FX3 and FX4, the cap popup, V and m marks and columns, the V/m move, byte equal goldens
 echo "### run fx34_ui_sim"; bash synth-open/tools/ui_shot/fx34_ui_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # which FX column a held button, Delete and paste act on (a Mic/Vol view paste crossed m and V before 4be287d)
 echo "### run fx_columns_sim"; bash synth-open/tools/ui_shot/fx_columns_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run parts_format_test"; "$T/parts_format_test"; echo "### rc=$?"
 # ALCO (the pinned engines set): the project Strum ramp depth, ALCO Chord Osc A and B, ALCO B and sub in the ramp (engines_para_check.sh, exit 2 = skipped, says why)
 echo "### run engines_para_check"; bash synth-open/tools/engines_para_check.sh "$PLUGDIR" "$out/engines-para"; erc=$?; if [ "$erc" -eq 2 ]; then echo "### rc=0"; else echo "### rc=$erc"; fi
 # every synth .plg of the pinned emu card through the firmware path on the bridge build, run by run against poly-engines emu_render.py
 # (ui_shot/bridge_check.sh --quick, emu parity T7, it replaced twin_render_check). Without a poly-engines checkout (ENG, default
 # ~/prj/poly-engines) or a pinned emu card (CARD, default ~/prj/poly/build/emu/current/card) the leg says SKIP and why, rc 0
 bc_eng=${ENG:-$HOME/prj/poly-engines}; bc_card=${CARD:-$HOME/prj/poly/build/emu/current/card}
 if [ ! -f "$bc_eng/tools/emu_render.py" ]; then echo "### run bridge_check"; echo "bridge_check: SKIP (no poly-engines checkout with tools/emu_render.py at $bc_eng, set ENG)"; echo "### rc=0"
 elif ! ls "$bc_card"/Plugins/Synth/*.plg >/dev/null 2>&1; then echo "### run bridge_check"; echo "bridge_check: SKIP (no pinned emu card with Plugins/Synth/*.plg at $bc_card, set CARD or run poly-devtools tools/mirror/emu-release.sh build)"; echo "### rc=0"
 else echo "### run bridge_check"; ENG="$bc_eng" CARD="$bc_card" bash synth-open/tools/ui_shot/bridge_check.sh --quick "$out/bridge-check" > "$L/bridge_check.log" 2>&1; brc=$?; tail -2 "$L/bridge_check.log"; echo "### rc=$brc"; fi
 # the same part path as Cortex-M4 code in the emulator (the soft clip is a fused multiply-add: the compiler decides the last bit)
 if [ -n "$TOOLCHAIN" ] && "$PY" -c 'import unicorn' 2> /dev/null; then
   echo "### run synth_part_arm"; PYTHON=$PY sh synth-open/tools/audio_opt/run_synth_part_arm.sh 3000; echo "### rc=$?"
 else echo "synth_part_arm: skipped (no TOOLCHAIN or no unicorn module in $PY)"; fi
 echo "### run chord_match_test"; "$T/chord_match_test"; echo "### rc=$?"
 echo "### run chord_match_test_san"; "$T/chord_match_test_san"; echo "### rc=$?"
 echo "### run strum_roll_test"; "$T/strum_roll_test"; echo "### rc=$?"
 echo "### run strum_roll_test_san"; "$T/strum_roll_test_san"; echo "### rc=$?"
 # the same on the real UI: record join and Capture write the roll of a chord as a Strum FX in the other slot (ui_shot)
 echo "### run strum_recog_sim"; bash synth-open/tools/ui_shot/strum_recog_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run scale_snap_test"; "$T/scale_snap_test"; echo "### rc=$?"
 # Scale snap and the Tape FX on the real sequencer and audio callback (plain ui_shot of this tree, built above, no plugin): the stored setting and MIDI notes,
 # the pitch of sample tracks per snap mode (voice tap), every Tape value 1-128 measured on sample and synth tracks (about 80 s for the three), the Tape rows
 echo "### run scale_snap_sim"; bash synth-open/tools/ui_shot/scale_snap_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run scale_snap_sample_sim"; bash synth-open/tools/ui_shot/scale_snap_sample_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run tape_range_sim"; bash synth-open/tools/ui_shot/tape_range_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run tape_fx_sim"; bash synth-open/tools/ui_shot/tape_fx_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run pattern_gen_test"; "$T/pattern_gen_test"; echo "### rc=$?"
 echo "### run pattern_gen_test_san"; "$T/pattern_gen_test_san"; echo "### rc=$?"
 echo "### run follow_chord_test"; "$T/follow_chord_test"; echo "### rc=$?"
 echo "### run follow_chord_test_san"; "$T/follow_chord_test_san"; echo "### rc=$?"
 echo "### run plug_abi_ids_test"; "$T/plug_abi_ids_test"; echo "### rc=$?"
 echo "### run synth_play_test"; "$T/synth_play_test"; echo "### rc=$?"
 echo "### run synth_play_test_san"; "$T/synth_play_test_san"; echo "### rc=$?"
 # the Synth play page on the real UI, router, shim and engines: entry, 8 voices, sustain, CC 123, bend range, exit, transport gate, the stop confirm, settings
 echo "### run synth_rec_test"; "$T/synth_rec_test"; echo "### rc=$?"
 echo "### run synth_rec_test_san"; "$T/synth_rec_test_san"; echo "### rc=$?"
 echo "### run synth_play_sim"; bash synth-open/tools/ui_shot/synth_play_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # the pages of the Perform key: Performance page 1, page 2, the Synth play page (A/B) and back, the Performance state kept, Shift + Perform opens the Synth play page (not on Performance), Shift + Params is the plain Params key (screens in OUT)
 echo "### run perform_pages_sim"; bash synth-open/tools/ui_shot/perform_pages_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 # REC on that page: a take of the master output into the free sample memory, landed as an instrument after the last used one (real UI, file manager and audio callback)
 echo "### run synth_rec_sim"; bash synth-open/tools/ui_shot/synth_rec_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0; echo "### rc=$?"
 echo "### run features_test"; "$T/features_test"; echo "### rc=$?"
 echo "### run features_test_san"; "$T/features_test_san"; echo "### rc=$?"
 echo "### run skipback_test"; "$T/skipback_test"; echo "### rc=$?"
 echo "### run skipback_test_san"; "$T/skipback_test_san"; echo "### rc=$?"
 # USB audio (Config > Features): the ring, and the descriptors served with the switch off (= the image without it) and on
 echo "### run usb_audio_out_test"; bash platforms/mk66/usbaudio/test/run.sh; echo "### rc=$?"
 echo "### run usb_desc_test"; bash platforms/mk66/usbaudio/test/usb_desc_test.sh; echo "### rc=$?"
 # a read of uninitialised stack in the synth path (render fuzz, zero against pattern stack init, 40,000 steps): the two hashes must be equal
 echo "### run uninit_equiv"; ZEROPAT=1 sh synth-open/tools/run_refresh_equiv.sh "ACD FAT VAP WTFM PERC" 40000; echo "### rc=$?"
 echo "### run plug_logo_test"; "$T/plug_logo_test" "$PLUGDIR"/*.plgi; echo "### rc=$?"
} > "$L/plug_tests.log" 2>&1
grep -E '^### (run|rc)' "$L/plug_tests.log" | paste - -

# verdict: every smoke variant and every test built and rc 0 (build_plugs too unless skipped)
bad=$((bad + $(grep -h '^### rc=' "$L/smoke.log" "$L/plug_tests.log" | grep -vc '^### rc=0$')))
nb=$(grep -c '^### rc=' "$L/plug_tests.log")
for f in "$L"/*.build.log; do [ -s "$f" ] && grep -q 'error' "$f" && { echo "BUILD ERROR $f"; bad=$((bad+1)); }; done
nsmoke=5; [ -z "${CHIP_PLUGIN_SRC:-}" ] || nsmoke=7
[ "$(grep -c '^### rc=0$' "$L/smoke.log")" -eq "$nsmoke" ] || { echo "smoke: not $nsmoke variants"; bad=$((bad+1)); }
if [ "$bad" -eq 0 ] && [ "$nb" -ge 10 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($bad failing, $nb tests ran)"; exit 1
