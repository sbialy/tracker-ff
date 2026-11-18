#!/usr/bin/env bash
# Host proof of the deflated icons (icons_inflate_test.py), part of run_tests.sh. Needs c++ with ASan/UBSan, no Arm toolchain.
exec python3 -I "$(cd "$(dirname "$0")" && pwd)/icons_inflate_test.py" "$@"
