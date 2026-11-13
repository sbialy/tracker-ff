#!/usr/bin/env bash
# Check the name of a firmware image for the original Tracker (MK66). Prints the shape on success, the reason on failure.
#   check_ptf_name.sh [--release | --build] <name.ptf | path/to/name.ptf>
# Two shapes, both start with PolyendTracker_:
#   build image   PolyendTracker_<TAG>_<sha7>.ptf   TAG 1 to 4 chars [A-Za-z0-9], sha7 = BUILD_TAG (1 to 7 chars [A-Za-z0-9]), the string
#                 baked into the image and reported by the console ping. rc, dev, test and console images.
#   release       PolyendTracker_<TAG>_<A.B.C>.ptf  a public release.
# --release accepts only the release shape, --build only the build shape, neither accepts both (package_release.sh).
# Limits of the original bootloader: the list in Config > Firmware needs a name that starts with PolyendTrac and is >= 15 chars
# (platforms/mk66/3rdparty/SD/SD.cpp:590), the bootloader reads the name into char[32], so <= 31 chars.
# 15 + 4 (TAG) + 1 + 7 (sha7) + 4 (.ptf) = 31, which is why the TAG of a build image stops at 4 chars.
set -euo pipefail
mode=any
case "${1:-}" in
  --release) mode=release; shift ;;
  --build) mode=build; shift ;;
esac
[ $# = 1 ] || { echo "usage: $0 [--release|--build] <name.ptf>" >&2; exit 2; }
name="$(basename "$1")"
n=${#name}
[ "$n" -le 31 ] || { echo "ERROR: $name is $n chars, the bootloader reads at most 31 (char[32])" >&2; exit 1; }
[ "$n" -ge 15 ] || { echo "ERROR: $name is $n chars, Config > Firmware lists only names of 15 or more" >&2; exit 1; }
case "$name" in PolyendTracker_*) ;; *) echo "ERROR: $name must start with PolyendTracker_ (Config > Firmware lists only PolyendTrac*)" >&2; exit 1 ;; esac
shape=
if [[ "$name" =~ ^PolyendTracker_[A-Za-z0-9]{1,4}_[A-Za-z0-9]{1,7}\.ptf$ ]]; then
  shape=build
elif [[ "$name" =~ ^PolyendTracker_[A-Za-z0-9]+_[0-9]+\.[0-9]+\.[0-9]+\.ptf$ ]]; then
  shape=release
fi
if [ -z "$shape" ]; then
  tag=${name#PolyendTracker_}; tag=${tag%%_*}
  if [ "${#tag}" -gt 4 ] && [ "$mode" != release ]; then
    echo "ERROR: $name: TAG '$tag' is ${#tag} chars, a build image allows at most 4 (PolyendTracker_<TAG>_<sha7>.ptf must stay <= 31 chars)" >&2
  else
    echo "ERROR: $name must be PolyendTracker_<TAG 1-4 chars>_<BUILD_TAG 1-7 chars>.ptf (build image) or PolyendTracker_<TAG>_<A.B.C>.ptf (public release)" >&2
  fi
  exit 1
fi
if [ "$mode" != any ] && [ "$mode" != "$shape" ]; then
  if [ "$mode" = release ]; then
    echo "ERROR: $name is a build image name, a public release (RELEASE=1) is PolyendTracker_<TAG>_<A.B.C>.ptf" >&2
  else
    echo "ERROR: $name is a release name, a build image is PolyendTracker_<TAG>_<sha7>.ptf (build with RELEASE=1 for a public release)" >&2
  fi
  exit 1
fi
echo "$name: $shape name, $n chars"
