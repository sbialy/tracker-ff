#!/usr/bin/env bash
# The host test suite of a tracker-ff tree (Mac or Linux, nothing here touches a device or builds a firmware image):
#   - run_smoke.sh in 5 variants (the five built-in engines, TRIM=1, PLUGENG=1: a run time plugin engine stand-in)
#   - plug/build_plugs.sh: the two worked examples of docs/plugin_sdk.md, built and run in the emulator
#   - the plugin host unit tests (plug_*_test.cpp, sat_softknee_test.cpp), the index and logo tests against the pinned modules
# Ends with one verdict line: "RESULT: GREEN" (exit 0) or "RESULT: RED (...)" (exit 1). Logs in OUTDIR/logs.
#
#   synth-open/tools/run_tests.sh [WORKTREE [OUTDIR]]
#     WORKTREE  the tree to test, default the one this script is in
#     OUTDIR    logs, the example plugins and the fetched modules, default WORKTREE/build/tests (git-ignored)
#   Environment:
#     TOOLCHAIN       Arm GNU prefix (.../bin/arm-none-eabi-) for build_plugs.sh. Default: WORKTREE/agt/12.3.rel1/bin/arm-none-eabi-
#                     when it is there, else arm-none-eabi-gcc on PATH
#     PY              a python3 with the unicorn module (the emulator of build_plugs.sh). Default python3
#                     (python3 -m venv /tmp/phzvenv && /tmp/phzvenv/bin/pip install unicorn, then PY=/tmp/phzvenv/bin/python)
#     PLUGDIR         the shipped plugins (.plg, .plgi) for the index and logo tests. Default: the pinned modules of
#                     engines/external.lock.json, fetched into OUTDIR/modules from the local module builds (external.py fetch --from-local)
#     NO_BUILD_PLUGS  set: skip build_plugs.sh (run_host_tests.sh)
#     CHIP_PLUGIN_SRC engine_chip.cpp of tracker-engines-os (CHIP is chip.plg): adds smoke variants that link that engine and the Polyend
#                     voice natively and list CHIP as a plugin engine at stored 6 (the engine tests of CHIP, all-engine loops with it)
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
export PY=${PY:-python3}
export SMOKE_ARCH=${SMOKE_ARCH:-arm64}
echo "tree $wt ($(git rev-parse --short HEAD 2>/dev/null || echo '?'))"
echo "out $out"
echo "toolchain ${TOOLCHAIN:-none} ($( [ -n "$TOOLCHAIN" ] && "${TOOLCHAIN}gcc" -dumpversion 2>/dev/null))"
echo "python $PY"

L=$out/logs; rm -rf "$L"; mkdir -p "$L" "$out/plugins"
T=$(mktemp -d "${TMPDIR:-/tmp}/tff_tests.XXXXXX")
trap 'rm -rf "$T"' EXIT
bad=0

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
TESTS="plug_bank_test plug_index_test plug_logo_test plug_ui_nav_test sat_softknee_test plug_work_test plug_scan_cache_test"
for t in $TESTS; do
  c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/$t.cpp -o "$T/$t" > "$L/$t.build.log" 2>&1 || echo "BUILD FAIL $t"
done
c++ -std=c++20 -O1 -Wall -I synth-open -I synth-open/tools/plug_stub synth-open/tools/plug_sidecar_test.cpp -o "$T/plug_sidecar_test" \
  > "$L/plug_sidecar_test.build.log" 2>&1 || echo "BUILD FAIL plug_sidecar_test"
c++ -std=c++20 -O1 -Wall -I synth-open -I core/include synth-open/tools/ext_loader_test.cpp -o "$T/ext_loader_test" \
  > "$L/ext_loader_test.build.log" 2>&1 || echo "BUILD FAIL ext_loader_test"
# the Features registry and its file parser (core/source/features.cpp), plain and with ASan/UBSan
FT=(-std=c++20 -O1 -Wall -DFEATURE_EXT_HOOKS -DFEATURES_TEST -I core/source -I core/include synth-open/tools/features_test.cpp core/source/features.cpp)
c++ "${FT[@]}" -o "$T/features_test" > "$L/features_test.build.log" 2>&1 || echo "BUILD FAIL features_test"
c++ -g -fsanitize=address,undefined -fno-sanitize-recover=all "${FT[@]}" -o "$T/features_test_san" > "$L/features_test_san.build.log" 2>&1 \
  || echo "BUILD FAIL features_test_san"
{
 for t in plug_bank_test plug_ui_nav_test sat_softknee_test plug_sidecar_test plug_work_test plug_scan_cache_test; do
   echo "### run $t"; "$T/$t"; echo "### rc=$?"
 done
 # EXT_MODULE="module.plg IMAGE_CRC RUN_ADDRESS" (synth-open/plug/pack_ext.py prints them) adds the real module checks
 read -r -a ext_args <<< "${EXT_MODULE:-}"
 echo "### run ext_loader_test"; "$T/ext_loader_test" ${ext_args[@]+"${ext_args[@]}"}; echo "### rc=$?"
 echo "### run plug_index_test"; "$T/plug_index_test" "$PLUGDIR"; echo "### rc=$?"
 echo "### run features_test"; "$T/features_test"; echo "### rc=$?"
 echo "### run features_test_san"; "$T/features_test_san"; echo "### rc=$?"
 # USB audio (Config > Features): the ring, and the descriptors served with the switch off (= the image without it) and on
 echo "### run usb_audio_out_test"; bash platforms/mk66/usbaudio/test/run.sh; echo "### rc=$?"
 echo "### run usb_desc_test"; bash platforms/mk66/usbaudio/test/usb_desc_test.sh; echo "### rc=$?"
 echo "### run plug_logo_test"; "$T/plug_logo_test" "$PLUGDIR"/*.plgi; echo "### rc=$?"
} > "$L/plug_tests.log" 2>&1
grep -E '^### (run|rc)' "$L/plug_tests.log" | paste - -

# verdict: every smoke variant and every test built and rc 0 (build_plugs too unless skipped)
bad=$((bad + $(grep -h '^### rc=' "$L/smoke.log" "$L/plug_tests.log" | grep -vc '^### rc=0$')))
nb=$(grep -c '^### rc=' "$L/plug_tests.log")
for f in "$L"/*.build.log; do [ -s "$f" ] && grep -q 'error' "$f" && { echo "BUILD ERROR $f"; bad=$((bad+1)); }; done
nsmoke=5; [ -z "${CHIP_PLUGIN_SRC:-}" ] || nsmoke=7
[ "$(grep -c '^### rc=0$' "$L/smoke.log")" -eq "$nsmoke" ] || { echo "smoke: not $nsmoke variants"; bad=$((bad+1)); }
if [ "$bad" -eq 0 ] && [ "$nb" -ge 9 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($bad failing, $nb tests ran)"; exit 1
