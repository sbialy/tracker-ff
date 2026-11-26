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
#     RT_CACHE        the shared content keyed caches (see below), default build/test-cache of the main checkout, off = none
#     RT_JOBS_WANT    the most job tokens the run takes, default 6 (see "Width W" below). JOBS: the sim jobs at a time, default W
#     UNIT_JOBS       parallel unit test compiles, default W. CCACHE: the compiler cache of those compiles (empty = off)
set -uo pipefail
t_start=${EPOCHREALTIME:-$(date +%s)}   # the total and setup legs of timings.tsv start here
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
# RT_CACHE: the content keyed caches the worktrees of one checkout share (default build/test-cache of the main checkout): the engines_para_check
# pass (engines_para_check.sh), the release axf of the bridge build (ui_shot/bridge_env.sh), the old firmware hosts of fx34_compat_sim. off = none
RT_CACHE_DIR=${RT_CACHE:-$(cd "$(git rev-parse --git-common-dir)/.." && pwd)/build/test-cache}; export RT_CACHE=$RT_CACHE_DIR
[ "$RT_CACHE_DIR" = off ] && RT_CACHE_DIR=$out/cache-off
echo "tree $wt ($(git rev-parse --short HEAD 2>/dev/null || echo '?'))"
echo "out $out"
echo "toolchain ${TOOLCHAIN:-none} ($( [ -n "$TOOLCHAIN" ] && "${TOOLCHAIN}gcc" -dumpversion 2>/dev/null))"
echo "python $PY"
echo "shared cache $RT_CACHE"

L=$out/logs
T=$(mktemp -d "${TMPDIR:-/tmp}/tff_tests.XXXXXX")
trap 'rm -rf "$T"' EXIT
# Timings: every leg's wall seconds go to $L/timings.tsv, longest first (seconds, name, kind, start, end, 1 min load average at the start).
# Each job log also has a "### start" and an "### end" line. With JOBS > 1 the sim jobs start longest first by the last run's timings.tsv
# (TIMINGS=file names another one, sims it does not list start first in the listed order), without one in the listed order.
prev_tsv=${TIMINGS:-$L/timings.tsv}; [ -f "$prev_tsv" ] && cp "$prev_tsv" "$T/prev_timings.tsv"
rm -rf "$L"; mkdir -p "$L" "$out/plugins"
now() { echo "${EPOCHREALTIME:-$(date +%s)}"; }
load1() { { sysctl -n vm.loadavg 2> /dev/null || echo "{ $(cut -d' ' -f1 /proc/loadavg 2> /dev/null) }"; } | awk '{print $2}'; }
secs() { awk -v a="$1" -v b="$2" 'BEGIN { printf "%.1f", b - a }'; }
# phase NAME KIND ... phase_end NAME: a timed leg that is not a job (a build, a smoke variant), one line in $L/phases.raw
phase() { printf -v "ph_s_$1" '%s' "$(now)"; printf -v "ph_l_$1" '%s' "$(load1)"; printf -v "ph_k_$1" '%s' "$2"; }
phase_end() { local s=ph_s_$1 l=ph_l_$1 k=ph_k_$1 e; e=$(now)
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$(secs "${!s}" "$e")" "$1" "${!k}" "${!s}" "$e" "${!l}" >> "$L/phases.raw"; }
phase total run; phase setup setup; ph_s_total=$t_start; ph_s_setup=$t_start   # from the script start (CHIP source, pyenv check)
bad=0
ORCH_JOBS_IN=${ORCH_JOBS:-}
TIER=${TIER:-all}; PLUGDIR_SET=${PLUGDIR:+1}
case "$TIER" in unit|sim|all) ;; *) echo "TIER=$TIER: unit, sim or all"; echo "RESULT: RED (bad TIER)"; exit 1;; esac
# Width W (jobs.sh): ORCH_JOBS when set, else the orchestrator's `orch-jobs.sh n` (free machine-wide job tokens and load), else the CPU
# count, capped by RT_JOBS_WANT (default 6). With the orchestrator the run holds W job tokens for its whole length: a holder process
# outside this shell's job table takes them (`orch-jobs.sh run`, at most RT_JOBS_WAIT s, default 300, then width 1) and lets them go
# within a second of this shell's end, a killed run included. W is the sim pool (JOBS), the unit compiles (UNIT_JOBS), the smoke
# variants at a time and every make -j below, and ORCH_JOBS=W with ORCH_JOBS_HELD=1 goes to every child, so a sim's own make or a
# nested jobs.sh takes no tokens of its own. JOBS and UNIT_JOBS still win. A caller that wraps this script in its own `orch-jobs.sh run`
# sets ORCH_JOBS_HELD=1 too (the grant is then used as is).
JW=$(sh "$HERE/jobs.sh" n "${RT_JOBS_WANT:-6}")
( sh "$HERE/jobs.sh" run "$JW" -- sh -c 'echo "$ORCH_JOBS" > "$1.tmp" && mv "$1.tmp" "$1"; while kill -0 "$2" 2> /dev/null; do sleep 1; done' \
    sh "$T/grant" "$$" > "$T/grant.log" 2>&1 < /dev/null & )
gw=$(now); gwn=0
while [ ! -s "$T/grant" ] && [ "$gwn" -lt $(( (${RT_JOBS_WAIT:-300} + 30) * 10 )) ]; do
  [ "$gwn" -eq 20 ] && echo "waiting for job tokens (want $JW)"; sleep 0.1; gwn=$((gwn+1)); done
W=$(cat "$T/grant" 2> /dev/null); case ${W:-x} in *[!0-9]*|''|0) W=1;; esac
export ORCH_JOBS=$W ORCH_JOBS_HELD=1
JOBS=${JOBS:-$W}
echo "tier $TIER, $JOBS parallel sim jobs"
echo "job width $W (wanted $JW, ORCH_JOBS before the run ${ORCH_JOBS_IN:-unset}, grant wait $(secs "$gw" "$(now)") s, load $(load1))$(grep -h 'orch-jobs:' "$T/grant.log" 2> /dev/null | sed 's/^/, /' | tr -d '\n')"

phase_end setup
# The serial prefix runs side by side: the unit test compiles start in the background here, beside the smoke variants, the plugin build and
# the module fetch, and are waited for before the sim jobs start (their output is printed then, in order). The firmware name checks are queued
# unit jobs below (they write nothing into the tree, poly-test-speed-r2).
UJ=${UNIT_JOBS:-$W}
echo "### host unit tests (compiles in the background)"
# the host unit tests: every test binary is one queued compile (ucc NAME FLAGS AND SOURCES, the binary is $T/NAME), run UJ at a time by
# xargs -P, each source compiled with ccache when it is installed (CCACHE=, empty, turns it off), then linked
CCACHE=${CCACHE-$(command -v ccache 2> /dev/null)}; export CCACHE_BASEDIR=${CCACHE_BASEDIR:-$wt} CCACHE_NOHASHDIR=${CCACHE_NOHASHDIR:-1}
mkdir -p "$T/ucc" "$T/uobj"; nucc=0
ucc() {  # ucc NAME ARGS...: the sources (*.cpp) are compiled one by one with -c, every other argument goes to the compiles and the link
  local n=$1 a i=0 fl=() src=() objs=(); shift
  for a in "$@"; do case $a in *.cpp) src+=("$a");; *) fl+=("$a");; esac; done
  nucc=$((nucc+1))
  { printf 'cd %q || exit 1\n{ ' "$wt"
    for a in "${src[@]}"; do i=$((i+1)); objs+=("$T/uobj/$n.$i.o"); printf '%s c++ %s -c %q -o %q && ' "${CCACHE:-}" "$(printf '%q ' "${fl[@]}")" "$a" "$T/uobj/$n.$i.o"; done
    printf 'c++ %s %s -o %q; } > %q 2>&1 || echo "BUILD FAIL %s"\n' "$(printf '%q ' "${fl[@]}")" "$(printf '%q ' "${objs[@]}")" "$T/$n" "$L/$n.build.log" "$n"
  } > "$T/ucc/$(printf '%03d' "$nucc")-$n.sh"
}
{
phase unit_builds build
python3 -I synth-open/tools/waves_curated.py check > "$L/waves_curated.log" 2>&1 || { echo "FAIL waves_curated check (releases/waves vs MANIFEST.txt), see $L/waves_curated.log"; echo 1 > "$T/units.bad"; }
TESTS="plug_bank_test plug_index_test plug_logo_test plug_ui_nav_test plug_defer_test sat_softknee_test plug_work_test plug_scan_cache_test synth_order_test plug_transport_test plug_files_test wave_name_test"
for t in $TESTS; do
  ucc "$t" -std=c++20 -O1 -Wall -I synth-open synth-open/tools/$t.cpp
done
ucc plug_sidecar_test -std=c++20 -O1 -Wall -I synth-open -I synth-open/tools/plug_stub synth-open/tools/plug_sidecar_test.cpp
# MPE out: the allocator of core/source/mtMpeOut.h against a recording sink, random streams (stuck notes, over the member count, bends)
ucc mpe_out_test -std=c++20 -O1 -Wall -Wextra synth-open/tools/mpe_out_test.cpp
# project.mtff: the codec and the file rules on an in-memory card (power loss at every step), unknown lines kept, stale stamp, nested paths
ucc plug_mtff_test -std=c++20 -O1 -Wall -Wextra -I synth-open -I synth-open/tools/plug_stub synth-open/tools/plug_mtff_test.cpp
# the same test on the MK66 layout of Mtff (no synth.N.auto.K fields: the Tracker+ / Mini lines are unknown lines there)
# the Scala (.scl) parser of the tuning: good files and every reject, plain and under the sanitizers (microtuning phase 2)
MS=(-std=c++20 -O1 -Wall -Wextra -I core/source synth-open/tools/mtscala_test.cpp)
ucc mtscala_test "${MS[@]}"
ucc mtscala_test_san -g -fsanitize=address,undefined -fno-sanitize-recover=all "${MS[@]}"
ucc plug_mtff_mk66_test -std=c++20 -O1 -Wall -Wextra -DMTFF_NO_SYNTH_AUTO -I synth-open -I synth-open/tools/plug_stub synth-open/tools/plug_mtff_test.cpp
# the integer sequencer clock against the stock double accumulator (core/source/internalTickClock.h), 1 simulated minute per run here
# the IMX plugin loader test, both hosts (job plug_load_imx), and the run time engine list of the stock (Tracker+ and Mini) synth path,
# IMX port P7 H6 (core/synth/synth_types.h)
ucc plug_load_test_imx -std=c++20 -O1 -Wall -Wextra -DPLATFORM_IMX -I synth-open synth-open/tools/plug_load_test.cpp
ucc plug_load_test_mk66 -std=c++20 -O1 -Wall -Wextra -I synth-open synth-open/tools/plug_load_test.cpp
ucc synth_types_test -std=c++20 -O1 -Wall -Wextra -Werror -I core/synth synth-open/tools/synth_types_test.cpp
ucc seq_tick_test -std=c++20 -O2 -Wall -I core/source synth-open/tools/seq_tick_test.cpp
# the sequencer's private random generator of the seeded random takes (core/source/seqRandom.h)
ucc seq_random_test -std=c++20 -O2 -Wall -Wextra -I core/source synth-open/tools/seq_random_test.cpp
ucc linein_ring_test -std=c++20 -O2 -Wall -Wextra -include algorithm -I core/libs/Audio synth-open/tools/linein_ring_test.cpp
# the Tape FX arithmetic (Stop 1-64, Start 65-128) and the id 51 load migration (core/source/tapeFx.h)
ucc tape_fx_test -std=c++20 -O2 -Wall -I core/source synth-open/tools/tape_fx_test.cpp
# the CPU kernels of core/libs/Audio (the firmware includes the very headers the tests include): old loop against new loop, bit for bit
ucc kern_sample_read_test -std=c++20 -O2 -Wall -I core/libs/Audio synth-open/tools/kern_sample_read_test.cpp
ucc kern_env_steady_test -std=c++20 -O2 -Wall -I synth-open/tools/kern_host -I core/libs/Audio synth-open/tools/kern_env_steady_test.cpp
ucc kern_gain_copy_test -std=c++20 -O2 -Wall -I synth-open/tools/kern_host -I core/libs/Audio synth-open/tools/kern_gain_copy_test.cpp
ucc kern_synth_part_test -std=c++20 -O2 -ffp-contract=fast -Wall -I core/libs/Audio synth-open/tools/kern_synth_part_test.cpp
# the Parts sidecar codec and song order (core/source/parts_format.h)
ucc parts_format_test -std=c++20 -O1 -Wall -I core/source synth-open/tools/parts_format_test.cpp
ucc ext_loader_test -std=c++20 -O1 -Wall -I synth-open -I core/include synth-open/tools/ext_loader_test.cpp
ucc ext_recovery_test -std=c++20 -O1 -Wall -Wextra -I synth-open -I core/include synth-open/tools/ext_recovery_test.cpp
# the chord matcher and the chord names of the chord capture (core/source/mtChord.cpp), plain and with ASan/UBSan
CM=(-std=c++20 -O1 -Wall -I core/source synth-open/tools/chord_match_test.cpp core/source/mtChord.cpp)
ucc chord_match_test "${CM[@]}"
ucc chord_match_test_san -g -fsanitize=address,undefined -fno-sanitize-recover=all "${CM[@]}"
# the hand roll of a recognised chord: the Strum FX value and its slot (core/source/mtRoll.h, header only), plain and with ASan/UBSan
SR=(-std=c++20 -O1 -Wall -Wextra -I core/source synth-open/tools/strum_roll_test.cpp)
ucc strum_roll_test "${SR[@]}"
ucc strum_roll_test_san -g -fsanitize=address,undefined -fno-sanitize-recover=all "${SR[@]}"
# the scale snap function (core/source/scales.cpp, per track snap at playback): every note x root x scale x mode against a brute force
SS=(-std=c++20 -O1 -Wall -DFEATURE_FF_CORE -DAUDIO_CORE_RAM= -I synth-open/tools/scale_stub -I core/source synth-open/tools/scale_snap_test.cpp core/source/scales.cpp)
ucc scale_snap_test "${SS[@]}"
# the pattern idea generator of the Fill dialog (core/source/mtPatternGen.cpp, needs scales.cpp), plain and with ASan/UBSan
PG=(-std=c++20 -O1 -Wall -Wextra -I synth-open/tools/kern_host -I core/source synth-open/tools/pattern_gen_test.cpp core/source/mtPatternGen.cpp core/source/scales.cpp)
ucc pattern_gen_test "${PG[@]}"
ucc pattern_gen_test_san -g -fsanitize=address,undefined -fno-sanitize-recover=all "${PG[@]}"
FC=(-std=c++20 -O1 -Wall -I core/source synth-open/tools/follow_chord_test.cpp core/source/mtFollowChord.cpp)
ucc follow_chord_test "${FC[@]}"
ucc follow_chord_test_san -g -fsanitize=address,undefined -fno-sanitize-recover=all "${FC[@]}"
# the extension ids of the plugin ABI: all distinct, PLUG_EXT_CHORD off the 5 of PLUG_EXT_FILES (host level 2 line)
ucc plug_abi_ids_test -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_abi_ids_test.cpp
# the Features registry and its file parser (core/source/features.cpp), plain and with ASan/UBSan
# the Synth play page's voice router (core/source/synthPlayRouter.cpp): notes, velocity curves, the 8 note table, sustain, CC 123 / 120 / 121, exit cleanup, bend, filters
SPT=(-std=c++20 -O1 -Wall -Wextra -DSYNTHPLAY_TEST -I core/source synth-open/tools/synth_play_test.cpp core/source/synthPlayRouter.cpp)
ucc synth_play_test "${SPT[@]}"
ucc synth_play_test_san -g -fsanitize=address,undefined -fno-sanitize-recover=all "${SPT[@]}"
# the Synth play page REC capture (core/source/synthPlayRec.cpp): the mono mix, whole blocks, the stop at the capacity, the trailing silence trim
SRT=(-std=c++20 -O1 -Wall -Wextra -DSYNTHPLAYREC_TEST -I core/source synth-open/tools/synth_rec_test.cpp core/source/synthPlayRec.cpp)
ucc synth_rec_test "${SRT[@]}"
ucc synth_rec_test_san -g -fsanitize=address,undefined -fno-sanitize-recover=all "${SRT[@]}"
FT=(-std=c++20 -O1 -Wall -DFEATURE_EXT_HOOKS -DFEATURES_TEST -I core/source -I core/include synth-open/tools/features_test.cpp core/source/features.cpp)
ucc features_test "${FT[@]}"
ucc features_test_san -g -fsanitize=address,undefined -fno-sanitize-recover=all "${FT[@]}"
# Skip-back: the SDRAM ring of the master output and its WAV save (core/source/skipback.cpp), plain and with ASan/UBSan
SB=(-std=c++20 -O1 -Wall -Wextra -DFEATURE_EXT_HOOKS -DSKIPBACK_TEST -I core/source synth-open/tools/skipback_test.cpp core/source/skipback.cpp)
ucc skipback_test "${SB[@]}"
ucc skipback_test_san -g -fsanitize=address,undefined -fno-sanitize-recover=all "${SB[@]}"
ls "$T"/ucc/*.sh | xargs -P "$UJ" -n 1 bash
phase_end unit_builds
} > "$T/units.out" 2>&1 < /dev/null &
units_pid=$!
echo "### smoke"
# the variants run side by side (each builds into its own temp dir and only reads the tree), smoke.log is their logs in the listed order
[ "$TIER" = unit ] || {
# Only the five built-in engines can be linked in (engines.mk), every other engine is a plugin (CHIP too since 1.3.0).
# PLUGENG=1 adds a run time plugin engine (a 100 engine library, missing numbers, names.txt).
smv=("|ACD FAT VAP WTFM" "TRIM=1|ACD FAT VAP WTFM PERC" "TRIM=1 PLUGENG=1|ACD FAT VAP WTFM PERC" "|ACD FAT VAP WTFM PERC" "PLUGENG=1|ACD FAT VAP WTFM PERC")
[ -z "${CHIP_PLUGIN_SRC:-}" ] || smv+=("TRIM=1 CHIP|ACD FAT VAP WTFM PERC" "TRIM=1 PLUGENG=1 CHIP|ACD FAT VAP WTFM PERC")
smp=(); i=0
for v in "${smv[@]}"; do
  e=${v%%|*}; s=${v#*|}; i=$((i+1))
  case $e in
    *CHIP) e=${e% CHIP}; sv=smoke_chip_$(echo "$e" | tr -c 'A-Za-z0-9\n' '_'); hdr="### env $e CHIP_PLUGIN_SRC | $s";;
    *) sv=smoke_$(echo "${e:-plain}_$s" | tr -c 'A-Za-z0-9\n' '_'); hdr="### env $e | $s";;
  esac
  while [ "$(for p in ${smp[@]+"${smp[@]}"}; do kill -0 "$p" 2> /dev/null && echo; done | wc -l)" -ge "$W" ]; do sleep 0.2; done
  ( echo "$hdr"; phase "$sv" smoke
    env $e sh synth-open/tools/run_smoke.sh "$s"; echo "### rc=$?"; phase_end "$sv" ) > "$T/smoke.$i.log" 2>&1 < /dev/null &
  smp+=($!)
done
wait "${smp[@]}"
for j in $(seq 1 "$i"); do cat "$T/smoke.$j.log"; done; } > "$L/smoke.log" 2>&1
[ "$TIER" = unit ] || { grep -c '^### rc=0' "$L/smoke.log" | sed 's/^/smoke variants rc=0: /'; grep '^### rc=' "$L/smoke.log" | grep -v 'rc=0' | sed 's/^/smoke NONZERO /'; }

if [ -z "${NO_BUILD_PLUGS:-}" ] && [ "$TIER" != unit ]; then
  echo "### build_plugs"
  if [ -z "$TOOLCHAIN" ]; then
    echo "build_plugs: no toolchain (set TOOLCHAIN)"; bprc=1
  elif ! "$PY" -c 'import unicorn' 2> /dev/null; then
    echo "build_plugs: $PY has no unicorn module (set PY)"; bprc=1
  else
    phase build_plugs build+run
    (bash synth-open/plug/build_plugs.sh "$out/plugins") > "$L/build_plugs.log" 2>&1; bprc=$?
    phase_end build_plugs
  fi
  echo "build_plugs rc=$bprc"
  [ "$bprc" -eq 0 ] || { echo "build_plugs FAILED"; bad=$((bad+1)); }
fi

# the shipped plugins the index and logo tests read: $PLUGDIR, else the pinned modules (engines/external.lock.json) from the local module
# builds (build_plugs.sh builds only the SDK examples since the five repository split)
if [ -z "${PLUGDIR:-}" ] && [ "$TIER" != unit ]; then
  PLUGDIR=$out/modules; PLUGDIR_SET=1; rm -rf "$PLUGDIR"; phase fetch_modules setup
  python3 -I engines/external.py fetch "$PLUGDIR" --from-local > "$L/fetch_modules.log" 2>&1 ||
    { echo "fetch of the pinned modules FAILED (set PLUGDIR), see $L/fetch_modules.log"; bad=$((bad+1)); }
  phase_end fetch_modules
fi
echo "plugins for the index and logo tests: ${PLUGDIR:-none (unit tier)}"

# Tiers (TIER): unit = the firmware name checks and the host unit tests only (no smoke, no plugin build, no ui_shot, about two minutes),
# sim = unit + the ui_shot sims and the other heavy host legs, all (default) = everything. JOBS (default W): sim jobs run in parallel, each
# writes its own log ($L/jobs/NNN-name.log, folded into plug_tests.log in the listed order) and its own output dir. ui_shot builds a sim
# starts itself are serialised by a make lock (a PATH shim), so two sims never build one OUT dir at once. JOBS=1 is the old sequential run.
mkdir -p "$L/jobs" "$T/bin"; nj=0
REAL_MAKE=$(command -v make)
cat > "$T/bin/make" <<MK
#!/usr/bin/env bash
# run_tests make lock: one ui_shot build at a time across parallel sims (a recursive make of the holder passes through)
[ -n "\${RT_MAKE_HELD:-}" ] && exec "$REAL_MAKE" "\$@"
until mkdir "$T/make.lock" 2> /dev/null; do sleep 0.2; done
RT_MAKE_HELD=1 "$REAL_MAKE" "\$@"; rc=\$?
rmdir "$T/make.lock"; exit \$rc
MK
chmod +x "$T/bin/make"; export PATH="$T/bin:$PATH"
# job name tier command: tier unit, unitmod (unit with the shipped plugins) or sim. The log number is the listed position (the fold order).
# With JOBS > 1 every job is queued: run_queue starts the sim jobs (SQ) longest first, JOBS at a time, and runs the unit jobs (UQ) in this
# shell while all JOBS slots are busy. JOBS=1 runs each job where it is listed.
SQ=(); UQ=(); declare -A SQ_TIER SQ_CMD SQ_FILE
job_run() {  # name tier command logfile
  local s rc; s=$(now)
  echo "### run $1"; echo "### start $s load $(load1)"; ( eval "$3" ); rc=$?
  echo "### end $(now) secs $(secs "$s" "$(now)") tier $2"; echo "### rc=$rc"
}
job() {
  local name=$1 tier=$2 cmd=$3 f
  case "$TIER:$tier" in unit:sim) return 0;; unit:unitmod) [ -n "${PLUGDIR_SET:-}" ] || return 0;; esac
  nj=$((nj+1)); f=$(printf '%s/jobs/%03d-%s.log' "$L" "$nj" "$name")
  if [ "$JOBS" -gt 1 ]; then
    if [ "$tier" = sim ]; then SQ+=("$name"); else UQ+=("$name"); fi
    SQ_TIER[$name]=$tier; SQ_CMD[$name]=$cmd; SQ_FILE[$name]=$f
  else
    job_run "$name" "$tier" "$cmd" > "$f" 2>&1 < /dev/null
  fi
}
run_unit() { local n=${UQ[$ui]}; ui=$((ui+1)); job_run "$n" "${SQ_TIER[$n]}" "${SQ_CMD[$n]}" > "${SQ_FILE[$n]}" 2>&1 < /dev/null; }
run_queue() {  # the queued jobs, sims longest first by the last timings.tsv
  local order=() n; ui=0
  if [ -s "$T/prev_timings.tsv" ] && [ "${#SQ[@]}" -gt 0 ]; then
    mapfile -t order < <(printf '%s\n' "${SQ[@]}" | awk -F'\t' 'NR == FNR { if ($1 ~ /^[0-9.]+$/ && sub(/^job:/, "", $2)) t[$2] = $1; next }
      { k = ($0 in t); printf "%d\t%012.1f\t%06d\t%s\n", k, k ? 1e9 - t[$0] : 0, FNR, $0 }' "$T/prev_timings.tsv" - | sort | cut -f4)
    echo "sim job order (longest first by $prev_tsv): ${order[*]}"
  else
    order=(${SQ[@]+"${SQ[@]}"}); [ "${#order[@]}" -eq 0 ] || echo "sim job order: listed (no timings.tsv from an earlier run)"
  fi
  for n in ${order[@]+"${order[@]}"}; do
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do
      if [ "$ui" -lt "${#UQ[@]}" ]; then run_unit; else wait -n 2> /dev/null || sleep 0.2; fi
    done
    job_run "$n" "${SQ_TIER[$n]}" "${SQ_CMD[$n]}" > "${SQ_FILE[$n]}" 2>&1 < /dev/null &
  done
  while [ "$ui" -lt "${#UQ[@]}" ]; do run_unit; done
}

exec 3>&1
{
 # sim tier: every ui_shot flavour the run_tests sims share is built once here, before any sim starts. A sim of the plain flavour gets
 # the binary as UI_SHOT_BIN and runs no make of its own. Sims that build a flavour of their own go through the make lock below
 if [ "$TIER" != unit ]; then
  phase ui_shot_make_plain build
  make -s -C synth-open/tools/ui_shot -j"$W" > "$L/ui_shot_make.log" 2>&1
  phase_end ui_shot_make_plain
  # a build left from an older tree has no fxmute poke
  phase ui_shot_make_ext build
  make -s -C synth-open/tools/ui_shot EXTHOOKS=1 OUT="$out/ui-shot-ext" -j"$W" > "$L/ui_shot_ext_make.log" 2>&1
  phase_end ui_shot_make_ext; phase ui_shot_make_msc build
  make -s -C synth-open/tools/ui_shot EXTHOOKS=1 USB_MSC=1 OUT="$out/ui-shot-msc" -j"$W" > "$L/ui_shot_msc_make.log" 2>&1
  phase_end ui_shot_make_msc
 fi
 # the background leg of the prefix (the unit compiles) ends before any job starts
 wait "$units_pid"
 { cat "$T/units.out"; } >&3
 bad=$((bad + $(cat "$T/units.bad" 2> /dev/null || echo 0)))
 # inode and mtime of the three ui_shot binaries the sims share, to see after the pool whether a relink happened under a running sim
 relink_sig() { ls -li "$1/ui_shot" 2> /dev/null | awk '{ print $1, $6, $7, $8 }'; }
 for f in ui-shot ui-shot-ext ui-shot-msc; do [ "$f" = ui-shot ] && d="$wt/build/ui-shot" || d="$out/$f"; relink_sig "$d" > "$T/relink.$f"; done
 phase jobs_wall run
 # one define list (config/ff_release.mk): the sim flavours against the release list, and a release build log of this tree when there is one
 rl=$(ls -t "$wt"/build/poly-release*.log 2>/dev/null | head -1)
 # our feature conditionals in stock files may only go down (core/include/ff_hooks.h is the way in)
 # the firmware name and table checks (host scripts that write nothing into the tree), queued unit jobs like the rest
 job check_ptf_name_test unit 'bash synth-open/tools/check_ptf_name_test.sh | tail -1'
 job release_gate_tail_test unit 'bash synth-open/tools/release_gate_tail_test.sh | tail -1'
 job package_zip_test unit 'bash synth-open/tools/package_zip_test.sh | tail -1 | grep -q PASS$'
 job tail_pad_test unit 'python3 -I synth-open/tools/tail_pad_test.py | tail -1'
 job check_static_init_test unit 'python3 -I synth-open/tools/check_static_init_test.py | tail -1'
 job icons_inflate_test unit 'bash synth-open/tools/icons_inflate_test.sh | tail -1'
 job font_inflate_test unit 'bash synth-open/tools/font_inflate_test.sh | tail -1'
 job logo_inflate_test unit 'bash synth-open/tools/logo_inflate_test.sh | tail -1'
 job fxdesc_test unit 'bash synth-open/tools/fxdesc_test.sh | tail -1'
 job randname_test unit 'bash synth-open/tools/randname_test.sh | tail -1'
 job playorder_test unit 'bash synth-open/tools/playorder_test.sh | tail -1'
 job fxnames_test unit 'bash synth-open/tools/fxnames_test.sh | tail -1'
 job ff_ifdef_ratchet unit 'python3 -I synth-open/tools/ff_ifdef_ratchet.py'
 job ff_flags_check unit 'bash synth-open/tools/ff_flags_check.sh ${rl:+"$rl"}'
 # the quick plugin host unit tests are queued unit jobs too (with JOBS > 1 they run while the sim jobs fill every slot)
 for t in plug_bank_test plug_ui_nav_test plug_defer_test sat_softknee_test plug_sidecar_test plug_mtff_test plug_mtff_mk66_test plug_work_test plug_scan_cache_test synth_order_test plug_transport_test plug_files_test wave_name_test; do
   job "$t" unit "\"\$T/$t\""
 done
 # the Strum ramp depth: depth 0 == STRUM_RAMP=0, depth 100 == the ramp before the depth (digests over every input)
 job strum_depth_test sim 'bash synth-open/tools/run_strum_depth_test.sh "$wt"'
 # EXT_MODULE="module.plg IMAGE_CRC RUN_ADDRESS" (synth-open/plug/pack_ext.py prints them) adds the real module checks
 read -r -a ext_args <<< "${EXT_MODULE:-}"
 job ext_loader_test unit '"$T/ext_loader_test" ${ext_args[@]+"${ext_args[@]}"}'
 # build/extras.plg of make original-release (the release card modules): loaded into the release region by its own image id and address,
 # the MOD/IT tenant inside its sub-slot (the binding to the .ptf is check_ext.py's, in make original and the release gate)
 job ext_loader_test_extras unit 'if [ -f build/extras.plg ]; then read -r xid xrun < <(python3 -I -c "import struct,sys; h=open(sys.argv[1],\"rb\").read(64); print(\"0x%08x 0x%08x\" % struct.unpack_from(\"<II\", h, 56))" build/extras.plg); "$T/ext_loader_test" build/extras.plg "$xid" "$xrun" rel; fi'
 # the recovery console and the status line of a refused module (with EXT_MODULE also over the real file)
 job ext_recovery_test unit '"$T/ext_recovery_test" ${ext_args[@]+"${ext_args[@]}"}'
 job plug_index_test unitmod '"$T/plug_index_test" "$PLUGDIR"'
 # the IMX plugin loader (synth-open/plug_load.h, port plan P5): tilt.c built --cpu cm7 and cm4f, loaded by the IMX host (both) and by
 # the MK66 host (cm4f only), then run under Unicorn on the M7 model at an IMX SDRAM address: the output must equal the MK66 run
 job plug_load_imx sim 'for c in cm7 cm4f; do (cd synth-open && "$PY" -I plug/build_plug.py --id org.example.fx.tilt --kind master --version 1.0.0 --cpu $c --out "$T/tilt_$c.plg" plug/examples/tilt.c) > "$L/tilt_$c.build.log" 2>&1 || { echo "BUILD FAIL tilt_$c.plg"; exit 1; }; done
   r=0; "$T/plug_load_test_imx" "$T/tilt_cm7.plg" "$T/tilt_cm4f.plg" || r=1; "$T/plug_load_test_mk66" "$T/tilt_cm7.plg" "$T/tilt_cm4f.plg" || r=1
   m=$("$PY" -I synth-open/plug/emu_plug.py "$T/tilt_cm4f.plg" --blocks 16 --param 1=4 2>&1 | grep "^output checksum")
   i=$("$PY" -I synth-open/plug/emu_plug.py "$T/tilt_cm7.plg" --imx --base 0x80100000 --blocks 16 --param 1=4 2>&1 | grep "^output checksum")
   echo "mk66 cm4f: $m"; echo "imx cm7: $i"; [ -n "$m" ] && [ "$m" = "$i" ] || r=1; exit $r'
 job synth_types_test unit '"$T/synth_types_test"'
 job seq_tick_test unit '"$T/seq_tick_test" 1'
 job seq_random_test unit '"$T/seq_random_test"'
 job linein_ring_test unit '"$T/linein_ring_test"'
 job tape_fx_test unit '"$T/tape_fx_test"'
 job kern_sample_read_test unit '"$T/kern_sample_read_test"'
 job kern_env_steady_test unit '"$T/kern_env_steady_test"'
 job kern_gain_copy_test unit '"$T/kern_gain_copy_test"'
 job kern_record_queue_test sim 'sh synth-open/tools/kern_record_queue_test.sh'
 job kern_synth_part_test unit '"$T/kern_synth_part_test" full'
 # send ducking (duck_math.h, effect_duck.h) on the real AudioStream and mixer: model, thresholds, off, dry path untouched. Needs the host copy of
 # libs/Audio that make -C synth-open/tools/ui_shot generates (build/ui-shot/gen)
 job duck_test sim 'sh synth-open/tools/duck_test.sh'
 # the input limit of AudioMixer (mixer.h kMaxInputs): 13 on the original, 21 on Tracker+/Mini, ASan, an over-limit mixer must not compile
 job mixer_inputs_test sim 'sh synth-open/tools/mixer_inputs_test.sh'
 # FX mute sleep and USB audio 8+3 mono in the real engine (ui_shot): the sleep states of the Mixer Mute, the tap and interleave bit exact against the Stems tap points
 job fx_mute_sim sim 'bash synth-open/tools/ui_shot/fx_mute_sim.sh "$wt/build/ui-shot/ui_shot" "$out/fx-mute-sim" | tail -3; test "${PIPESTATUS[0]}" = 0'
 # the M8 style sample play modes (1-Shot Rev, Pingpong Rev, Oscillator, Repitch Steps) rendered by the real audio engine: mirror, pitch, length, tempo change mid note, stereo, step FX Reverse
 job m8_modes_sim sim 'bash synth-open/tools/ui_shot/m8_modes_sim.sh "$wt/build/ui-shot/ui_shot" "$out/m8-modes-sim" | tail -3; test "${PIPESTATUS[0]}" = 0'
 job usb_multich_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/usb_multich_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 job umc_automute_sim sim 'UI_SHOT_BIN="$out/ui-shot-ext/ui_shot" bash synth-open/tools/ui_shot/umc_automute_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # Config > Features and Config > USB (USB audio, USB drive, Audio mode, Restart now, Storage) on the real UI with the goldens. features.sh needs the EXTHOOKS=1 build
# (its Features list has no USB drive row), usb_menu_sim.sh and usb_msc_sim.sh the EXTHOOKS=1 USB_MSC=1 build (the drive rows and the lend of the card)
 job features_sim sim 'bash synth-open/tools/ui_shot/features.sh "$out/ui-shot-ext/ui_shot" "$out/features-sim" | tail -3; test "${PIPESTATUS[0]}" = 0'
 # ALCO's wave file rows on the bridge build (the real alco.plg): hidden without /Waves, Sine, names, Missing, stored paths, byte equal goldens
 job wave_label_sim sim 'PLG_DIR="$PLUGDIR" bash synth-open/tools/ui_shot/wave_label_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 job usb_menu_sim sim 'bash synth-open/tools/ui_shot/usb_menu_sim.sh "$out/ui-shot-msc/ui_shot" "$out/usb-menu-sim" | tail -3; test "${PIPESTATUS[0]}" = 0'
 job usb_msc_sim sim 'bash synth-open/tools/ui_shot/usb_msc_sim.sh "$out/ui-shot-msc/ui_shot" "$out/usb-msc-sim" | tail -3; test "${PIPESTATUS[0]}" = 0'
 # MOD import, IT import and IT export (modit.plg): the built in code and the module path (renamed sources behind modit_dispatch.cpp and the image
 # forwarders, EXT=1) against the same goldens, the popups and the first use load on the real Project screen, the module binding checks of the gate and package
 job modit_golden_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/modit_golden_sim.sh | tail -9; test "${PIPESTATUS[0]}" = 0'
 job modit_golden_sim_ext sim 'UI_SHOT_BIN="$out/ui-shot-ext/ui_shot" EXT=1 bash synth-open/tools/ui_shot/modit_golden_sim.sh | tail -9; test "${PIPESTATUS[0]}" = 0'
 # the same goldens in develop mode with the console module loaded beside extras.plg (the console slot and the release region together)
 job modit_golden_sim_console sim 'UI_SHOT_BIN="$out/ui-shot-ext/ui_shot" EXT=1 HOSTCON=1 bash synth-open/tools/ui_shot/modit_golden_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # the device vs host import differences, pinned in a seeded sim
 job modit_seeded_sim sim 'BIN_PREBUILT=1 BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/modit_seeded_sim.sh | tail -4; test "${PIPESTATUS[0]}" = 0'
 # the theme and self test tenants of extras.plg (custom theme via the module, fallback without it, self test start or its popup), against the resident build
 job ext_tenants_sim sim 'EXT_BIN="$out/ui-shot-ext/ui_shot" PLAIN_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/ext_tenants_sim.sh | tail -12; test "${PIPESTATUS[0]}" = 0'
 job modit_popup_sim sim 'UI_SHOT_BIN="$out/ui-shot-ext/ui_shot" bash synth-open/tools/ui_shot/modit_popup_sim.sh | tail -16; test "${PIPESTATUS[0]}" = 0'
 job modit_pack_test sim 'bash synth-open/tools/modit_pack_test.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # what a synth part does with its notes: the per note ids (S-ID1..S-ID6), the queued note on, the render hashes of the same pitch class (SAN=address,undefined runs the sanitizer build)
 job synth_nna_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/synth_nna_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # the Sample Loader enters a folder while its list is drawn: no NULL row text reaches cList::update (SLB1..SLB5, own binary)
 job sample_loader_browse_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/sample_loader_browse_sim.sh | tail -7; test "${PIPESTATUS[0]}" = 0'
 # the nine Sample Editor effects as card files (-DFW_SE_PTFX, the pinned se_*.ptfx of $PLUGDIR) against the stock build: every effect applied through the UI
 # equals the built in one, a card without the files shows Missing: <name> and Apply is a no-op (SE1..SE8, the card files' own bytes on the
 # emulator bridge build)
 job septfx_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" PTFX_DIR="$PLUGDIR" bash synth-open/tools/ui_shot/septfx_sim.sh | tail -4; test "${PIPESTATUS[0]}" = 0'
 # the MIDI remote key and pad state in the real Keys, ButtonsBaseImpl and midiRemote.cpp (host stand-ins, ASan and UBSan)
 job remote_held_test sim 'bash synth-open/tools/remote_held/remote_held_test.sh'
 # FX3 and FX4 playback (the 4 FX project switch): ranks 2 and 3 on sample, MIDI and synth tracks, the priority FX2 > FX1 > FX4 > FX3, the pairs
 job fx34_play_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/fx34_play_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # synth automation on the part's Pan, Gain and sends (ModKind kPart): the slot output reaches the engine's kAuto source, Off and a project load clear it
 job part_auto_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/part_auto_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # FX3 and FX4 of a pattern step (the 4 FX project switch): the sidecar files, undo, Parts, Save As, the stale rule, the cap. The compat sim builds ui_shot
 # of release-1.4.0-int and of tracker-ff-v1.3.0 from git archives into build/rel140-4fx/ the first time (about 2 minutes)
 job fx34_store_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/fx34_store_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 job fx34_compat_sim sim 'FX34_CACHE=${FX34_CACHE:-$RT_CACHE_DIR/fx34-hosts} bash synth-open/tools/ui_shot/fx34_compat_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # the pattern editor side: FX bank, edit operations that carry FX3 and FX4, the cap popup, V and m marks and columns, the V/m move, byte equal goldens
 job fx34_ui_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/fx34_ui_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # which FX column a held button, Delete and paste act on (a Mic/Vol view paste crossed m and V before 4be287d)
 job fx_columns_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/fx_columns_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 job parts_format_test unit '"$T/parts_format_test"'
 # ALCO (the pinned engines set): the project Strum ramp depth, ALCO Chord Osc A and B, ALCO B and sub in the ramp (engines_para_check.sh, exit 2 = skipped, says why)
 job engines_para_check sim 'bash synth-open/tools/engines_para_check.sh "$PLUGDIR" "$out/engines-para"; erc=$?; [ "$erc" -eq 2 ] && exit 0; exit "$erc"'
 # every synth .plg of the pinned emu card through the firmware path on the bridge build, run by run against poly-engines emu_render.py
 # (ui_shot/bridge_check.sh --quick, emu parity T7, it replaced twin_render_check). Without a poly-engines checkout (ENG, default
 # ~/prj/poly-engines) or a pinned emu card (CARD, default ~/prj/poly/build/emu/current/card) the leg says SKIP and why, rc 0
 bc_eng=${ENG:-$HOME/prj/poly-engines}; bc_card=${CARD:-$HOME/prj/poly/build/emu/current/card}
 if [ ! -f "$bc_eng/tools/emu_render.py" ]; then job bridge_check sim 'echo "bridge_check: SKIP (no poly-engines checkout with tools/emu_render.py at $bc_eng, set ENG)"'
 elif ! ls "$bc_card"/Plugins/Synth/*.plg >/dev/null 2>&1; then job bridge_check sim 'echo "bridge_check: SKIP (no pinned emu card with Plugins/Synth/*.plg at $bc_card, set CARD or run poly-devtools tools/mirror/emu-release.sh build)"'
 else job bridge_check sim 'ENG="$bc_eng" CARD="$bc_card" bash synth-open/tools/ui_shot/bridge_check.sh --quick "$out/bridge-check" > "$L/bridge_check.log" 2>&1; brc=$?; tail -2 "$L/bridge_check.log"; exit $brc'; fi
 # IMX port P5 H8: every synth .plg of the pinned emu card loaded by plug::imx::load into the IMX SDRAM slot (plus BRIDGE objects, Unicorn at
 # 0x80000000) byte equal to the MK66 placement. ALCO (33,216 B) is over the 32 KiB slot and allowed (IMX_TOOBIG_OK), any other too big fails
 job imx_plug_test sim 'CARD="$bc_card" bash synth-open/tools/ui_shot/imx_plug_test.sh "$L" | tail -4; exit "${PIPESTATUS[0]}"'
 # IMX port P7 H5: every synth .plg of that card but chip.plg attached in the plus_synth harness guest (the image's loader and PlugSynthVoice
 # over the closed libPolySynth), one note each renders non-silent (synth-open/tools/plus_synth/plug_guest_test.sh, SKIP without the card)
 job plug_guest_test sim 'CARD="$bc_card" PY="$PY" bash synth-open/tools/plus_synth/plug_guest_test.sh "$out/plug-guest" > "$L/plug_guest_test.log" 2>&1; pgrc=$?; tail -2 "$L/plug_guest_test.log"; exit $pgrc'
 # T-Type has no Volume parameter and needs the host gain to be the note velocity: the same step (Volume FX 10, 50, 100) on the original sim and the
 # Tracker+ and Mini sims (guest harness of this tree) must give byte identical WAVs, FM4 as the control (plus_ttype_vel_sim.sh, SKIP without the card
 # or a devtools checkout with the UISHOT_PLUS guards, DEVTOOLS_DIR)
 job plus_ttype_vel_sim sim 'CARD="$bc_card" bash synth-open/tools/ui_shot/plus_ttype_vel_sim.sh > "$L/plus_ttype_vel_sim.log" 2>&1; tvrc=$?; tail -3 "$L/plus_ttype_vel_sim.log"; exit $tvrc'
 # the mutant: the harness built from a PlugSynthVoice.cpp without the no-Volume gain must make that test RED (SKIP stays green, as above)
 job plus_ttype_vel_sim_mutant sim 'o=$(TTV_MUTANT=1 CARD="$bc_card" bash synth-open/tools/ui_shot/plus_ttype_vel_sim.sh PLUS || true); echo "$o" | tail -3; echo "$o" | tail -1 | grep -Eq "^plus_ttype_vel_sim: (RED|SKIP)"'
 # plugin engines on Tracker+ and Mini with the features of the original Tracker: the plus_synth guest rows (bend, tuning, Scala degrees, MIDI Chord
 # paraphony, note ids, New note action, plus_synth/plug_parity_sim.sh) and the same features through the real UI and sequencer on the plus and the
 # mini sim (MIDI Chord at Para, New note action and the NNA FX, two tracks on one pitch, MPE in per note, the Scala mirror: plug_parity_ui_sim.sh)
 job plug_parity_sim sim 'PLG_DIR="$PLUGDIR" PY="$PY" OUT="$out/plug-parity" bash synth-open/tools/plus_synth/plug_parity_sim.sh > "$L/plug_parity_sim.log" 2>&1; pprc=$?; tail -1 "$L/plug_parity_sim.log"; exit $pprc'
 for p in plus mini; do
   job plug_parity_ui_$p sim 'PRODUCT='$p' PLG_DIR="$PLUGDIR" PY="$PY" OUT="$out/plug-parity-ui-'$p'" bash synth-open/tools/ui_shot/plug_parity_ui_sim.sh > "$L/plug_parity_ui_'$p'.log" 2>&1; pprc=$?; tail -1 "$L/plug_parity_ui_'$p'.log"; exit $pprc'
 done
 # the New note row of the Synth page for plugin engine parts on Tracker+ and Mini (plug_nna_page_sim.sh, the file rows use the plain sim)
 for p in plus mini; do
   job plug_nna_page_$p sim 'PRODUCT='$p' PLG_DIR="$PLUGDIR" PY="$PY" OGBIN="$wt/build/ui-shot/ui_shot" OUT="$out/plug-nna-page-'$p'" bash synth-open/tools/ui_shot/plug_nna_page_sim.sh > "$L/plug_nna_page_'$p'.log" 2>&1; pprc=$?; tail -1 "$L/plug_nna_page_'$p'.log"; exit $pprc'
 done
 # the same part path as Cortex-M4 code in the emulator (the soft clip is a fused multiply-add: the compiler decides the last bit)
 if [ -n "$TOOLCHAIN" ] && "$PY" -c 'import unicorn' 2> /dev/null; then
   job synth_part_arm sim 'PYTHON=$PY sh synth-open/tools/audio_opt/run_synth_part_arm.sh 3000'
 else echo "synth_part_arm: skipped (no TOOLCHAIN or no unicorn module in $PY)"; fi
 job chord_match_test unit '"$T/chord_match_test"'
 job chord_match_test_san unit '"$T/chord_match_test_san"'
 job strum_roll_test unit '"$T/strum_roll_test"'
 job strum_roll_test_san unit '"$T/strum_roll_test_san"'
 # the same on the real UI: record join and Capture write the roll of a chord as a Strum FX in the other slot (ui_shot)
 job strum_recog_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/strum_recog_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 job scale_snap_test unit '"$T/scale_snap_test"'
 # Scale snap and the Tape FX on the real sequencer and audio callback (plain ui_shot of this tree, built above, no plugin): the stored setting and MIDI notes,
 # the pitch of sample tracks per snap mode (voice tap), every Tape value 1-128 measured on sample and synth tracks (about 80 s for the three), the Tape rows
 job scale_snap_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/scale_snap_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 job scale_snap_sample_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/scale_snap_sample_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/microtune_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_synth_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/microtune_synth_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job mtscala_test unit '"$T/mtscala_test"'
job mtscala_test_san unit '"$T/mtscala_test_san"'
job microtune_scl_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/microtune_scl_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_scl_synth_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/microtune_scl_synth_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_scl_plus_sim sim 'bash synth-open/tools/ui_shot/microtune_scl_imx_sim.sh PLUS | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_scl_mini_sim sim 'bash synth-open/tools/ui_shot/microtune_scl_imx_sim.sh MINI | tail -3; test "${PIPESTATUS[0]}" = 0'
job projset_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/projset_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job tuning_label_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/tuning_label_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job tuning_label_plus_sim sim 'SCRIPT=tuning_label_sim.sh bash synth-open/tools/ui_shot/microtune_scl_imx_sim.sh PLUS | tail -3; test "${PIPESTATUS[0]}" = 0'
job tuning_label_mini_sim sim 'SCRIPT=tuning_label_sim.sh bash synth-open/tools/ui_shot/microtune_scl_imx_sim.sh MINI | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_names_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/microtune_names_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_names_plus_sim sim 'bash synth-open/tools/ui_shot/microtune_p3_imx_sim.sh PLUS microtune_names_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_names_mini_sim sim 'bash synth-open/tools/ui_shot/microtune_p3_imx_sim.sh MINI microtune_names_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_snap_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/microtune_snap_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_snap_plus_sim sim 'bash synth-open/tools/ui_shot/microtune_p3_imx_sim.sh PLUS microtune_snap_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_snap_mini_sim sim 'bash synth-open/tools/ui_shot/microtune_p3_imx_sim.sh MINI microtune_snap_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_fine_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/microtune_fine_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_fine_plus_sim sim 'bash synth-open/tools/ui_shot/microtune_p3_imx_sim.sh PLUS microtune_fine_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_chord_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/microtune_chord_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_chord_plus_sim sim 'bash synth-open/tools/ui_shot/microtune_p3_imx_sim.sh PLUS microtune_chord_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_chord_mini_sim sim 'bash synth-open/tools/ui_shot/microtune_p3_imx_sim.sh MINI microtune_chord_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_chip_sim sim 'PLG_DIR="$PLUGDIR" bash synth-open/tools/ui_shot/microtune_chip_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
job microtune_scl_chip_sim sim 'PLG_DIR="$PLUGDIR" bash synth-open/tools/ui_shot/microtune_scl_chip_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 job tape_range_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/tape_range_sim.sh > "$L/tape_range_sim.out" 2>&1; r=$?; grep -A1 -E "^(FAIL|LENBAD)" "$L/tape_range_sim.out"; tail -3 "$L/tape_range_sim.out"; test "$r" = 0'
 job tape_fx_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/tape_fx_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # Tables (Table lite): N, V and the row FX on a sample track (voice tap, amp gain) and a synth part, the TBL step FX, per track position, project.mtff keys
 job tables_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/tables_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # Linked stereo track pairs (core/source/stereoPairs.h): unlinked == the previous tip when BASE_BIN names its ui_shot, a mono sample on a pair == a centred mono track, L and R of a stereo .pti, the follower, project.mtff keys
 # MIDI input in the audio interrupt never redraws the pattern view (core/source/mtMidi.cpp midiUiUpdate): the preempt point race row
 job midi_isr_race_sim sim 'bash synth-open/tools/ui_shot/midi_isr_race_sim.sh "$wt/build/ui-shot/ui_shot" | tail -3; test "${PIPESTATUS[0]}" = 0'
 # MIDI input caps in the audio interrupt (poly-midi-isr-harden round 2): 32 USB events and 1,000 us per block, leftovers in order (U1..U5), the external clock catch-up cap and its backlog (C1..C5)
 job midi_isr_cap_sim sim 'bash synth-open/tools/ui_shot/midi_isr_cap_sim.sh "$wt/build/ui-shot/ui_shot" | tail -4; test "${PIPESTATUS[0]}" = 0'
 # the DIN MIDI out TX ring in the audio interrupt (platforms/mk66/3rdparty/teensy_core/serial5.c, host stand-ins): full ring drops, counts, cuts the message, the next status goes through
 job serial5_drop_test sim 'bash synth-open/tools/serial5_drop/serial5_drop_test.sh'
 job stereo_pairs_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/stereo_pairs_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 job pattern_gen_test unit '"$T/pattern_gen_test"'
 job pattern_gen_test_san unit '"$T/pattern_gen_test_san"'
 job follow_chord_test unit '"$T/follow_chord_test"'
 job follow_chord_test_san unit '"$T/follow_chord_test_san"'
 job plug_abi_ids_test unit '"$T/plug_abi_ids_test"'
 job synth_play_test unit '"$T/synth_play_test"'
 job synth_play_test_san unit '"$T/synth_play_test_san"'
 # the Synth play page on the real UI, router, shim and engines: entry, 8 voices, sustain, CC 123, bend range, exit, transport gate, the stop confirm, settings
 job synth_rec_test unit '"$T/synth_rec_test"'
 job synth_rec_test_san unit '"$T/synth_rec_test_san"'
 job synth_play_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/synth_play_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # MPE IN phase 1 on the real UI and engines: a 3 note chord with independent bends (zero-crossing pitch), pressure on one voice, the 16th note stealing, note off after stealing, master bend, zone Off equal to the firmware before MPE (golden), a 1,500 events/s burst, RPN 6 and RPN 0
 # Capture on the real UI: the ring, the take file, and the card file pool (24 slots like the unit, section 16.9): a take file that keeps a slot after it closes locked every later file open on the unit, so the sim goes RED on a leaked slot or a refused open
 job capture_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
# the direct write rows of capture_sim.sh must go RED when the thing they guard is not done (CAP_MUTANT, see the sim): each mutant RED and its own row failing
job capture_sim_mut_off sim 'o=$(CAP_MUTANT=off CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  capture: the notes are in the pattern"'
job capture_sim_mut_nopress sim 'o=$(CAP_MUTANT=nopress CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  re-press: the first press wrote 4 steps"'
job capture_sim_mut_noundo sim 'o=$(CAP_MUTANT=noundo CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  undo: one Undo takes the second press back"'
job capture_sim_mut_nopat sim 'o=$(CAP_MUTANT=nopat CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  another pattern: pattern 2 is empty"'
job capture_sim_mut_twice sim 'o=$(CAP_MUTANT=twice CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  re-press: the second press in the same pattern leaves exactly the same steps"'
job capture_sim_mut_emptynote sim 'o=$(CAP_MUTANT=emptynote CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  empty take: the press writes nothing"'
job capture_sim_mut_nocopy sim 'o=$(CAP_MUTANT=nocopy CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  copy buffer: and it holds"'
job capture_sim_mut_nohint sim 'o=$(CAP_MUTANT=nohint CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  destination: no armed track, the notes came in on"'
job capture_sim_mut_pastend sim 'o=$(CAP_MUTANT=pastend CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  take past the end of the pattern"'
job capture_sim_mut_nocollide sim 'o=$(CAP_MUTANT=nocollide CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  collision: the captured note wins"'
job capture_sim_mut_spillcap sim 'o=$(CAP_MUTANT=spillcap CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  spill cap:"'
job capture_sim_mut_spillundo sim 'o=$(CAP_MUTANT=spillundo CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  spill undo:"'
job capture_sim_mut_spillorder sim 'o=$(CAP_MUTANT=spillorder CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  spill order:"'
job capture_sim_mut_spillparts sim 'o=$(CAP_MUTANT=spillparts CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  spill while a Part plays:"'
job capture_sim_mut_spillelse sim 'o=$(CAP_MUTANT=spillelse CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  spill re-press:"'
job capture_sim_mut_spillsame sim 'o=$(CAP_MUTANT=spillsame CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  spill same pattern: the second press on pattern 1 rewrites pattern 1 and the first block"'
job capture_sim_mut_spillgrow sim 'o=$(CAP_MUTANT=spillgrow CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  spill same pattern, longer take: the block grows by the pattern it needs"'
job capture_sim_mut_spilloff sim 'o=$(CAP_MUTANT=spilloff CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  spill note off across: the OFFs of the notes of patterns 1 and 2"'
job capture_sim_mut_spillend sim 'o=$(CAP_MUTANT=spillend CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  note off past the last pattern: the OFF lands on the last step of the last pattern"'
job capture_sim_mut_nopace sim 'o=$(CAP_MUTANT=nopace CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  spill pace:"'
job capture_sim_mut_repundo sim 'o=$(CAP_MUTANT=repundo CAP_SKIP16=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/capture_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -qF "FAIL  re-press undo:"'
 job synth_mpe_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/synth_mpe_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # the same sim with the zone forced off must go RED: a sim that cannot fail proves nothing
 job synth_mpe_sim_mutant sim 'o=$(MPE_MUTANT=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/synth_mpe_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED"'
 job synth_mpe_sim_m9_mutant sim 'o=$(M9_MUTANT=1 UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/synth_mpe_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -q "^FAIL  M9"'
 # the whole Synth play sim in the EXTHOOKS=1 flavour (the card file exists only there): P12 and P13 (the settings file) and the MP rows run here, the plain job above skips them
 job synth_play_sim_ext sim 'UI_SHOT_BIN="$out/ui-shot-ext/ui_shot" EXTHOOKS=1 bash synth-open/tools/ui_shot/synth_play_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # the same run with a seeded file that disagrees with the P12 write-back row must go RED on P12, not just anywhere
 job synth_play_sim_ext_mutant sim 'o=$(P12_MUTANT=1 UI_SHOT_BIN="$out/ui-shot-ext/ui_shot" EXTHOOKS=1 bash synth-open/tools/ui_shot/synth_play_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED" && echo "$o" | grep -q "^FAIL  P12"'
 # the MPE settings (mpezone, mperange, mpepress of /Features/synthplay.cfg) written by the page, read by a new process, and no key at the defaults: the EXTHOOKS=1 flavour has the card file
 job synth_mpe_persist sim 'UI_SHOT_BIN="$out/ui-shot-ext/ui_shot" EXTHOOKS=1 PERSIST_ONLY=1 bash synth-open/tools/ui_shot/synth_play_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # the same rows with the reload file cut and the encoder never back at the defaults must go RED
 job synth_mpe_persist_mutant sim 'o=$(PERSIST_MUTANT=1 UI_SHOT_BIN="$out/ui-shot-ext/ui_shot" EXTHOOKS=1 PERSIST_ONLY=1 bash synth-open/tools/ui_shot/synth_play_sim.sh || true); echo "$o" | tail -1; echo "$o" | tail -1 | grep -q "^RESULT: RED"'
 # the router host test against a router whose memberOf never finds a member: the R13 rows must fail
 job synth_mpe_router_mutant unit 'm="$T/mpe_mut"; rm -rf "${m:?}"; mkdir -p "$m" && sed "s/channel >= 2 \&\& channel <= 1 + value\[kMpeZone\]/false/" core/source/synthPlayRouter.cpp > "$m/synthPlayRouter.cpp" && ! cmp -s "$m/synthPlayRouter.cpp" core/source/synthPlayRouter.cpp && c++ -std=c++20 -O1 -DSYNTHPLAY_TEST -I core/source synth-open/tools/synth_play_test.cpp "$m/synthPlayRouter.cpp" -o "$m/t" && ! "$m/t" > "$m/out" 2>&1; test $? = 0'
 job midi_pc_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/midi_pc_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # MPE out (Project Settings > MPE out): the MIDI out byte stream of the real sequencer, Off identical to the golden, chord on member channels, stealing, Stop, 31 EDO bends against the table, M FX sum, RPN 6 and 0, the row through the UI
 job mpe_out_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/mpe_out_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 job mpe_out_test unit '"$T/mpe_out_test"'
 # Tracker+ and Mini: MPE in with the REAL IMX MIDI driver (usb_midi.c, UsbMidiImpl, UartMidiImpl + the Arduino MIDI library) over a byte level stub, raw bytes injected below it (usbmidi / dinmidi), the same scenario on the original, plus and mini sims, per voice bend, pressure, master bend, note off, DIN, parity
 job imx_midi_mpe_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/imx_midi_sim.sh in | tail -3; test "${PIPESTATUS[0]}" = 0'
 # MPE out on the plus and mini sims: the same mpe_out_sim.sh rows on the real UsbMidiImpl::send / usb_midi.c packet path
 job imx_midi_mpe_out_sim sim 'bash synth-open/tools/ui_shot/imx_midi_sim.sh out | tail -4; test "${PIPESTATUS[0]}" = 0'
 # the pages of the Perform key: Performance page 1, page 2, the Synth play page (A/B) and back, the Performance state kept, Shift + Perform opens the Synth play page (not on Performance), Shift + Params is the plain Params key (screens in OUT)
 job perform_pages_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/perform_pages_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 # REC on that page: a take of the master output into the free sample memory, landed as an instrument after the last used one (real UI, file manager and audio callback)
 job synth_rec_sim sim 'UI_SHOT_BIN="$wt/build/ui-shot/ui_shot" bash synth-open/tools/ui_shot/synth_rec_sim.sh | tail -3; test "${PIPESTATUS[0]}" = 0'
 job features_test unit '"$T/features_test"'
 job features_test_san unit '"$T/features_test_san"'
 job skipback_test unit '"$T/skipback_test"'
 job skipback_test_san unit '"$T/skipback_test_san"'
 # USB audio (Config > Features): the ring, and the descriptors served with the switch off (= the image without it) and on
 job usb_audio_out_test unit 'bash platforms/mk66/usbaudio/test/run.sh'
 job usb_desc_test unit 'bash platforms/mk66/usbaudio/test/usb_desc_test.sh'
 # a read of uninitialised stack in the synth path (render fuzz, zero against pattern stack init, 40,000 steps): the two hashes must be equal
 job uninit_equiv sim 'ZEROPAT=1 sh synth-open/tools/run_refresh_equiv.sh "ACD FAT VAP WTFM PERC" 40000'
 job plug_logo_test unitmod '"$T/plug_logo_test" "$PLUGDIR"/*.plgi'
} > "$L/plug_tests.log" 2>&1
run_queue
wait
phase_end jobs_wall
# a ui_shot binary relinked while sims ran it (the link is atomic since poly-test-speed-r3, so this is information, not a failure)
if [ "$TIER" != unit ]; then
  for f in ui-shot ui-shot-ext ui-shot-msc; do [ "$f" = ui-shot ] && d="$wt/build/ui-shot" || d="$out/$f"
    [ "$(relink_sig "$d")" = "$(cat "$T/relink.$f" 2> /dev/null)" ] && echo "ui_shot binary $f: not relinked during the pool" || echo "ui_shot binary $f: RELINKED during the pool"
  done
fi
cat "$L"/jobs/*.log >> "$L/plug_tests.log" 2>/dev/null
grep -E '^### (run|rc)' "$L/plug_tests.log" | paste - -

# verdict: every smoke variant and every test built and rc 0 (build_plugs too unless skipped)
bad=$((bad + $(cat "$L/smoke.log" "$L/plug_tests.log" 2> /dev/null | grep '^### rc=' | grep -vc '^### rc=0$')))
nb=$(grep -c '^### rc=' "$L/plug_tests.log")
for f in "$L"/*.build.log; do [ -s "$f" ] && grep -q 'error' "$f" && { echo "BUILD ERROR $f"; bad=$((bad+1)); }; done
nsmoke=5; [ -z "${CHIP_PLUGIN_SRC:-}" ] || nsmoke=7
[ "$TIER" = unit ] || [ "$(grep -c '^### rc=0$' "$L/smoke.log")" -eq "$nsmoke" ] || { echo "smoke: not $nsmoke variants"; bad=$((bad+1)); }
phase_end total
{ printf 'secs\tname\tkind\tstart\tend\tload1\n'
  { cat "$L/phases.raw"
    for f in "$L"/jobs/*.log; do [ -f "$f" ] || continue
      awk -v n="$(basename "$f" .log | cut -d- -f2-)" '$1 == "###" && $2 == "start" { s = $3; l = $5 }
        $1 == "###" && $2 == "end" { printf "%s\tjob:%s\t%s\t%s\t%s\t%s\n", $5, n, $7, s, $3, l }' "$f"
    done; } | sort -t "$(printf '\t')" -k1,1 -g -r; } > "$L/timings.tsv"
echo "timings: $L/timings.tsv (longest: $(sed -n 2,6p "$L/timings.tsv" | awk -F'\t' '{printf "%s %ss, ", $2, $1}'))"
if [ "$bad" -eq 0 ] && [ "$nb" -ge 10 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($bad failing, $nb tests ran)"; exit 1
