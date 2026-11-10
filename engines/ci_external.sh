#!/bin/bash
# CI step: the plugin modules of the four module repositories against the public identity in engines/engines.json.
#
#   EXTERNAL_DIR=DIR [EXTERNAL_VISIBILITY=public|all] engines/ci_external.sh
#
# EXTERNAL_DIR is a flat directory with the published files (engines/external.py fetch). The workflow always fetches the public sets
# (tracker-engines-os, tracker-effects-os: plain https, no token) and the private sets (poly-engines, poly-effects) only when a token that
# can read them is configured (EXTERNAL_VISIBILITY=all then). Unset EXTERNAL_DIR: skipped, exit 0. Set: every file must match the
# pins of engines/external.lock.json and check_modules.py verify-external must pass, otherwise exit 1. EXTERNAL_WITH=formflux also checks
# the optional group (FORM and FLUX), off by default like in the release package.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "${EXTERNAL_DIR:-}" ]; then
    echo "plugin modules: EXTERNAL_DIR is not set, skipped"
    exit 0
fi
[ -d "$EXTERNAL_DIR" ] || { echo "plugin modules: EXTERNAL_DIR=$EXTERNAL_DIR is not a directory"; exit 1; }
VIS="${EXTERNAL_VISIBILITY:-public}"
vflag=(); [ "$VIS" = all ] || vflag=(--visibility "$VIS")
wflag=(); for g in ${EXTERNAL_WITH:-}; do wflag+=(--with "$g"); done
python3 -I "$HERE/external.py" check "$EXTERNAL_DIR" ${vflag[@]+"${vflag[@]}"} ${wflag[@]+"${wflag[@]}"}
python3 -I "$HERE/check_modules.py" verify-external "$EXTERNAL_DIR" --host-abi "$HERE/../synth-open/plug/plug_abi.h" --visibility "$VIS" ${wflag[@]+"${wflag[@]}"}
