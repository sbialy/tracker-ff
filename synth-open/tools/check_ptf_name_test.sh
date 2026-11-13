#!/usr/bin/env bash
# Host test of check_ptf_name.sh: the two firmware name shapes, the 31 char limit and the 4 char TAG of a build image.
#   synth-open/tools/check_ptf_name_test.sh        (prints "ok" lines and exits 0, or the failing case and exits 1)
set -u
c="$(cd "$(dirname "$0")" && pwd)/check_ptf_name.sh"
bad=0
t() { # <expect 0|1> <mode or -> <name>
  local mode=(); [ "$2" = - ] || mode=("$2")
  "$c" ${mode[@]+"${mode[@]}"} "$3" > /dev/null 2>&1; local rc=$?
  if [ "$rc" = "$1" ]; then echo "ok   $1 ${2} $3"; else echo "FAIL want $1 got $rc: ${2} $3"; bad=$((bad+1)); fi
}
t 0 --build   PolyendTracker_FF_b2fa25b.ptf       # 29 chars
t 0 --build   PolyendTracker_ABCD_1234567.ptf     # 31 chars, the limit
t 0 --build   PolyendTracker_MS13_b2fa25b.ptf
t 0 --build   PolyendTracker_D130_1b276b7.ptf     # all digits and letters of a sha
t 1 --build   PolyendTracker_ABCDE_1234567.ptf    # 32 chars
t 1 --build   PolyendTracker_AUTOF_abc.ptf        # short enough, but TAG over 4
t 1 --build   PolyendTracker_FF_b2fa25b9.ptf      # 8 char BUILD_TAG (date fallback)
t 1 --build   PolyendTracker_FF_1.3.0.ptf         # a release name is not a build name
t 1 --build   PolyendTracker__b2fa25b.ptf         # empty TAG
t 0 --release PolyendTracker_FF_1.3.0.ptf
t 0 --release PolyendTracker_AUTOF_1.3.0.ptf      # 30 chars, TAG over 4 is fine for a public release
t 1 --release PolyendTracker_FF_b2fa25b.ptf       # a build name is not a release name
t 0 --release PolyendTracker_AUTOFX_1.3.0.ptf     # 31 chars, the limit
t 1 --release PolyendTracker_AUTOFXY_1.3.0.ptf    # 32 chars
t 0 -         PolyendTracker_FF_b2fa25b.ptf
t 0 -         PolyendTracker_FF_1.3.0.ptf
t 1 -         PolyTrk_FF_b2fa25b.ptf              # not listed by Config > Firmware
t 1 -         PolyendTracker_FF_b2fa25b.PTF       # lowercase .ptf only
t 1 -         PolyendTracker.ptf                  # no TAG and no version
t 0 --build   /tmp/some/dir/PolyendTracker_FF_b2fa25b.ptf   # a path is judged by its file name
echo
[ "$bad" = 0 ] && { echo "check_ptf_name_test: all passed"; exit 0; }
echo "check_ptf_name_test: $bad failed"; exit 1
