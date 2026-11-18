#!/usr/bin/env bash
# Host test of package_release.sh --zip with a RELATIVE image and out_dir (the zip step runs after a cd into the folder).
#   synth-open/tools/package_zip_test.sh
# Makes a stub release image in a temp dir, runs package_release.sh from there with relative paths and --zip, and checks the zip exists
# and holds Firmware/<image>.ptf at its root. Layout test only (no plugins, so no stack or mismatch gate).
set -u
here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
bad=0
ok()   { echo "ok   $*"; }
fail() { echo "FAIL $*"; bad=$((bad+1)); }
printf 'stub' > "$tmp/PolyendTracker_FF_1.4.0.ptf"
( cd "$tmp" && bash "$here/package_release.sh" PolyendTracker_FF_1.4.0.ptf rel/nested --zip ) > "$tmp/log.txt" 2>&1 \
  && ok "package_release --zip with relative image and dest exits 0" || { fail "package_release --zip with relative paths: $(tail -3 "$tmp/log.txt")"; }
z="$tmp/rel/nested/PolyendTracker_FF_1.4.0.zip"
[ -f "$z" ] && ok "zip exists in the relative dest" || fail "no zip at $z"
if [ -f "$z" ]; then
  unzip -Z1 "$z" | grep -qx 'Firmware/PolyendTracker_FF_1.4.0.ptf' && ok "zip root holds Firmware/<image>.ptf" || fail "Firmware/<image>.ptf not in the zip"
fi
[ -d "$tmp/rel/nested/PolyendTracker_FF_1.4.0/Firmware" ] && ok "folder made in the relative dest" || fail "no folder"
echo "package_zip_test: $([ "$bad" = 0 ] && echo PASS || echo "FAIL ($bad)")"
[ "$bad" = 0 ]
