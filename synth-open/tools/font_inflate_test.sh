#!/usr/bin/env bash
# Host proof of the deflated fonts (font_inflate_test.py), part of run_tests.sh. Needs c++ with ASan/UBSan, no Arm toolchain.
exec python3 -I "$(cd "$(dirname "$0")" && pwd)/font_inflate_test.py" "$@"
