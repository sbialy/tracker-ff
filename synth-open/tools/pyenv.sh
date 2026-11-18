#!/bin/sh
# The one pinned Python for every poly Mac tool that needs unicorn, capstone or numpy.
#   synth-open/tools/pyenv.sh [setup]   create or verify the venv (idempotent: a complete venv is a no-op), prints its python
#   synth-open/tools/pyenv.sh python    print the venv python, or fail with one line (exit 1) when the venv is not set up
#   synth-open/tools/pyenv.sh check     setup check only: exit 0 when complete, 1 when not (nothing is installed)
# Default location ~/prj/poly/build/pyenv (outside every worktree, shared, survives reboots: build/ is git-ignored, not /tmp).
# Contents: synth-open/tools/pyenv/requirements.txt (unicorn 2.1.4, capstone, numpy, all hash checked, nothing else).
# Environment: PYENV_DIR the venv directory, PYENV_BASE the python to build it from (default /opt/homebrew/bin/python3.14, then 3.13),
#              UNICORN_WHEEL a local copy of the pinned unicorn wheel (offline use, default ~/prj/poly/unicorn-2.1.4-*.whl, sha256 checked).
# Scripts: PY=${PY:-$(sh .../pyenv.sh python)} || exit 2, an explicit PY (or PYTHON) always wins.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
REQ=$HERE/pyenv/requirements.txt
DIR=${PYENV_DIR:-$HOME/prj/poly/build/pyenv}
SETUP="synth-open/tools/pyenv.sh"
WHEEL_SHA=d6c93e0f60328d8f4a1792af3f834137a28050fcc2305f2ec01efe8558a9844e

want=$(shasum -a 256 "$REQ" | cut -c1-64)
have() { [ -x "$DIR/bin/python" ] && [ "$(cat "$DIR/.pyenv-stamp" 2> /dev/null)" = "$want" ]; }

case ${1:-setup} in
  python)
    have || { echo "pyenv: $DIR is not set up, run: sh $HERE/pyenv.sh" >&2; exit 1; }
    echo "$DIR/bin/python"; exit 0 ;;
  check)
    have && "$DIR/bin/python" -c 'import unicorn, capstone, numpy' 2> /dev/null; exit $? ;;
  setup) ;;
  *) echo "usage: $SETUP [setup|python|check]" >&2; exit 2 ;;
esac

if have && "$DIR/bin/python" -c 'import unicorn, capstone, numpy' 2> /dev/null; then echo "$DIR/bin/python"; exit 0; fi

base=${PYENV_BASE:-}
if [ -z "$base" ]; then
  for c in /opt/homebrew/bin/python3.14 /opt/homebrew/bin/python3.13; do [ -x "$c" ] && { base=$c; break; }; done
fi
[ -n "$base" ] || { echo "pyenv: no python 3.13 or 3.14 (brew install python@3.14), or set PYENV_BASE" >&2; exit 1; }
[ "$(uname -sm)" = "Darwin arm64" ] || { echo "pyenv: the pinned wheels are macOS arm64 only (this is $(uname -sm))" >&2; exit 1; }

rm -rf "$DIR"; mkdir -p "$(dirname "$DIR")"
"$base" -m venv "$DIR"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/pyenv.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
links=
wheel=${UNICORN_WHEEL:-$(ls "$HOME"/prj/poly/unicorn-2.1.4-*macosx_11_0_arm64.whl 2> /dev/null | head -1 || true)}
if [ -n "$wheel" ] && [ "$(shasum -a 256 "$wheel" | cut -c1-64)" = "$WHEEL_SHA" ]; then
  cp "$wheel" "$tmp/"; links="--find-links $tmp"      # the local wheel is the same file as PyPI's (same sha256), used first
fi
# shellcheck disable=SC2086
"$DIR/bin/python" -m pip install --quiet --disable-pip-version-check --require-hashes --only-binary=:all: $links -r "$REQ" ||
  { rm -rf "$DIR"; echo "pyenv: pip install failed (offline? set UNICORN_WHEEL and retry)" >&2; exit 1; }
"$DIR/bin/python" -c 'import unicorn, capstone, numpy; assert unicorn.__version__.startswith("2.1.4"), unicorn.__version__' ||
  { rm -rf "$DIR"; echo "pyenv: import check failed" >&2; exit 1; }
printf '%s\n' "$want" > "$DIR/.pyenv-stamp"
echo "$DIR/bin/python"
