#!/usr/bin/env bash
# ff_flags_check.sh [RELEASE_LOG]
# One define list for the firmware and the simulator (config/ff_release.mk):
#   1. the feature defines (FEATURE_*, FW_*, SYNTH_ENGINE_*) of every core compile line of a make original-release log equal
#      FF_RELEASE_CORE_DEFS + the engine defines, and every platform compile line has FF_RELEASE_PLATFORM_DEFS (with a log)
#   2. the simulator's release flavour (ui_shot EXTHOOKS=1 USB_MSC=1) has exactly the release core feature defines minus FF_SIM_BRIDGE_ONLY_DEFS,
#      plus FEATURE_USB_AUDIO, and the plain flavour is the release minus the flags its knobs name (always)
#   3. core/include/ff_config.h (derived flags, legal combinations) compiles with every flavour: the release core, a stock build (no FF flag),
#      the sim flavours (plain, EXTHOOKS=1 USB_MSC=1, PARTS=0, SKIPBACK=1), Tracker+ and Mini (PLATFORM=IMX, also without the open stack as the
#      plus pin builds), and refuses each illegal combination it lists (always)
# Exit 0 and "RESULT: GREEN", else the differences and "RESULT: RED".
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../.." && pwd)
LOG=${1:-}
bad=0
feat() { grep -E '^-D(FEATURE_|FW_|SYNTH_ENGINE_)' | grep -vE '^-DFW_(VER_|EDITION_NAME)' | sort -u; }
mkvar() { make -s -f - print <<MK
SYNTH_ENGINES := ACD FAT VAP WTFM PERC
include $ROOT/config/ff_release.mk
include $ROOT/synth-open/engines.mk
print: ; @printf '%s\n' \$($1)
MK
}
T=$(mktemp -d "${TMPDIR:-/tmp}/ffflags.XXXXXX"); trap 'rm -rf "$T"' EXIT
mkvar FF_RELEASE_CORE_DEFS | feat > "$T/core"
mkvar FF_RELEASE_PLATFORM_DEFS | feat > "$T/plat"
mkvar FF_SIM_BRIDGE_ONLY_DEFS | feat > "$T/bridge"
mkvar FF_STEREO_IO_ALL | tr " " "\n" | grep . | sed "s/^/-D/" | sort -u > "$T/stereo"
cmp() {  # name expected actual
  if diff -u "$2" "$3" > "$T/d"; then echo "PASS $1"; else echo "FAIL $1 (- expected, + actual)"; sed -n '3,$p' "$T/d" | grep '^[-+]'; bad=$((bad+1)); fi
}
if [ -n "$LOG" ]; then
  grep -q 'arm-none-eabi-c++ ' "$LOG" || { echo "FAIL $LOG has no compile lines"; bad=$((bad+1)); }
  # core objects: the core makefile's compile lines carry the version defines, the platform's do not
  grep 'arm-none-eabi-c++ ' "$LOG" | grep -- '-DFW_VER_MAJOR=' | while read -r l; do
    printf '%s\n' $l | feat | tr '\n' ' '; echo; done | sort | uniq -c > "$T/core_sets"
  n=$(wc -l < "$T/core_sets" | tr -d ' ')
  [ "$n" = 1 ] || { echo "FAIL core compile lines carry $n different feature sets"; cat "$T/core_sets"; bad=$((bad+1)); }
  { cat "$T/core"; mkvar SYNTH_ENGINE_DEFINES | feat; } | sort -u > "$T/core_exp"
  sed -E 's/^ *[0-9]+ //' "$T/core_sets" | head -1 | tr ' ' '\n' | grep . | sort -u > "$T/core_act"
  cmp "release core defines == FF_RELEASE_CORE_DEFS + engines" "$T/core_exp" "$T/core_act"
  grep 'arm-none-eabi-gcc ' "$LOG" | grep -m1 'usb' | tr ' ' '\n' | feat > "$T/plat_line"
  comm -23 "$T/plat" "$T/plat_line" > "$T/plat_miss"
  if [ -s "$T/plat_miss" ]; then echo "FAIL platform line lacks:"; cat "$T/plat_miss"; bad=$((bad+1)); else echo "PASS platform defines include FF_RELEASE_PLATFORM_DEFS"; fi
fi
make -s -C "$ROOT/synth-open/tools/ui_shot" print-defs EXTHOOKS=1 USB_MSC=1 "STEREO_IO=$(mkvar FF_STEREO_IO_ALL | tr "\n" " ")" 2>/dev/null | feat > "$T/sim_rel"
{ comm -23 "$T/core" "$T/bridge"; grep -x -- -DFEATURE_USB_AUDIO "$T/plat"; mkvar SYNTH_ENGINE_DEFINES | feat; } | sort -u > "$T/sim_exp"
cmp "sim EXTHOOKS=1 USB_MSC=1 == release core - bridge only + USB_AUDIO" "$T/sim_exp" "$T/sim_rel"
make -s -C "$ROOT/synth-open/tools/ui_shot" print-defs 2>/dev/null | feat > "$T/sim_plain"
grep -vxE -- '-DFEATURE_(EXT_HOOKS|MODIT_MODULE|CAPTURE_MODULE|THEME_MODULE|SELFTEST_MODULE|USB_MSC)' "$T/sim_exp" | grep -vxFf "$T/stereo" > "$T/plain_exp"
cmp "sim plain == release flavour - EXT_HOOKS, MODIT_MODULE, CAPTURE_MODULE, THEME_MODULE, SELFTEST_MODULE, USB_MSC" "$T/plain_exp" "$T/sim_plain"
# 3 ff_config.h against every flavour (c++ -fsyntax-only, the defines of the flavour), then each illegal combination must fail
CXX=${CXX:-c++}
cfg() {  # expect(ok|fail) name defines...
  local want=$1 name=$2; shift 2
  if printf '#include "%s"\n' "$ROOT/core/include/ff_config.h" | "$CXX" -std=gnu++20 -fsyntax-only -x c++ "$@" - 2>"$T/cfg.err"; then got=ok; else got=fail; fi
  if [ "$got" = "$want" ]; then echo "PASS ff_config.h $name ($want)"; else echo "FAIL ff_config.h $name: expected $want, got $got"; sed -n 1,3p "$T/cfg.err"; bad=$((bad+1)); fi
}
sim() { make -s -C "$ROOT/synth-open/tools/ui_shot" print-defs "$@" 2>/dev/null | tr ' ' '\n' | grep -E '^-D(FEATURE_|FW_TRIM|FW_SE_PTFX|FW_REVERB|SYNTH_ENGINE_|PLATFORM_|TRACKER_MINI)' | tr '\n' ' '; }
cfg ok "release core" $(cat "$T/core") $(mkvar SYNTH_ENGINE_DEFINES)
cfg ok "stock (no FF flag)"
cfg ok "stock Tracker+ (PLATFORM_IMX FEATURE_SYNTH)" -DPLATFORM_IMX -DFEATURE_SYNTH
cfg ok "sim plain" $(sim)
cfg ok "sim EXTHOOKS=1 USB_MSC=1" $(sim EXTHOOKS=1 USB_MSC=1)
cfg ok "sim PARTS=0" $(sim PARTS=0)
cfg ok "sim EXTHOOKS=1 SKIPBACK=1" $(sim EXTHOOKS=1 SKIPBACK=1)
cfg ok "sim Tracker+ (PLATFORM=IMX)" $(sim PLATFORM=IMX)
cfg ok "sim Mini (PLATFORM=IMX PRODUCT=MINI)" $(sim PLATFORM=IMX PRODUCT=MINI)
cfg ok "sim Tracker+ pin (no open stack)" $(sim PLATFORM=IMX | tr ' ' '\n' | grep -vE '^-D(FEATURE_SYNTH_OPEN|SYNTH_ENGINE_)')
cfg fail "SYNTH_OPEN without SYNTH" -DFEATURE_SYNTH_OPEN
cfg fail "PLUG_HOST without SYNTH_OPEN" -DFEATURE_SYNTH -DFEATURE_PLUG_HOST
cfg fail "PLUG_HOST on IMX" -DPLATFORM_IMX -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DFEATURE_PLUG_HOST
cfg fail "SEND_DUCK without PLUG_HOST" -DFEATURE_SYNTH -DFEATURE_SEND_DUCK
cfg fail "PARTS without PROJECT_STORE" -DFEATURE_PARTS
cfg fail "MODIT_MODULE without EXT_HOOKS" -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DFEATURE_PLUG_HOST -DFEATURE_MODIT_MODULE
# the derived flags of the release: no Skip-back (FEATURE_NO_SKIPBACK), REC on the Synth play page, the PERC icons (PERC is in the engine set)
printf '#include "%s"\n#if defined(FEATURE_SKIPBACK) || !defined(FEATURE_SYNTH_REC) || FW_ICON_PERC != 1 || FW_ICON_CHIP != 0\n#error derived\n#endif\n' \
  "$ROOT/core/include/ff_config.h" | "$CXX" -std=gnu++20 -fsyntax-only -x c++ $(cat "$T/core") $(mkvar SYNTH_ENGINE_DEFINES) - 2>/dev/null \
  && echo "PASS ff_config.h release derives no Skip-back, Synth REC on, PERC icons on, CHIP icons off" \
  || { echo "FAIL ff_config.h release derived flags"; bad=$((bad+1)); }
# REC on the Synth play page on Tracker+ and Mini (synthrec-imx): the image's flags (PLATFORM_IMX FEATURE_SYNTH FEATURE_SYNTH_PATCHFX, core/makefile) and the
# plus pin (no open stack) derive FEATURE_SYNTH_REC, a stock Tracker+ core (no page) does not
rec_derived() {  # rec_derived expect(on|off) name defines...
    local want=$1 name=$2 cond=ifndef; shift 2
    [ "$want" = off ] && cond=ifdef
    if printf '#include "%s"\n#%s FEATURE_SYNTH_REC\n#error rec\n#endif\n' "$ROOT/core/include/ff_config.h" "$cond" | "$CXX" -std=gnu++20 -fsyntax-only -x c++ "$@" - 2>/dev/null; then
        echo "PASS ff_config.h $name: FEATURE_SYNTH_REC $want"
    else echo "FAIL ff_config.h $name: FEATURE_SYNTH_REC not $want"; bad=$((bad+1)); fi
}
rec_derived on "Tracker+ image flags (PLATFORM_IMX FEATURE_SYNTH FEATURE_SYNTH_PATCHFX)" -DPLATFORM_IMX -DFEATURE_SYNTH -DFEATURE_SYNTH_PATCHFX
rec_derived on "Mini image flags (TRACKER_MINI)" -DPLATFORM_IMX -DTRACKER_MINI -DFEATURE_SYNTH -DFEATURE_SYNTH_PATCHFX
rec_derived on "sim Tracker+ pin (no open stack)" $(sim PLATFORM=IMX | tr ' ' '\n' | grep -vE '^-D(FEATURE_SYNTH_OPEN|SYNTH_ENGINE_)')
rec_derived off "stock Tracker+ (no Synth play page)" -DPLATFORM_IMX -DFEATURE_SYNTH
if [ "$bad" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($bad failing)"; exit 1
