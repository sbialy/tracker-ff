#!/bin/bash
# CI step: the private modules (PHZ, PLK, Console, Tube) against the public identity in engines/engines.json.
#
#   EXTERNAL_DIR=DIR engines/ci_external.sh
#
# EXTERNAL_DIR is a flat directory with the published files of poly-engines and poly-effects. The workflow fills it with
# `engines/external.py fetch` when a token that can read the private repositories is configured. Unset or empty: skipped, exit 0
# (a fork, a pull request from a fork or a repository without the token has nothing to check). Set: every file must match the pins of
# engines/external.lock.json and `build_set.py verify-external` must pass, otherwise exit 1.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "${EXTERNAL_DIR:-}" ]; then
    echo "private modules: EXTERNAL_DIR is not set, skipped"
    exit 0
fi
[ -d "$EXTERNAL_DIR" ] || { echo "private modules: EXTERNAL_DIR=$EXTERNAL_DIR is not a directory"; exit 1; }
python3 -I "$HERE/external.py" check "$EXTERNAL_DIR"
python3 -I "$HERE/build_set.py" verify-external "$EXTERNAL_DIR" --host-abi "$HERE/../synth-open/plug/plug_abi.h"
