#!/bin/bash
# Rebuilds every plugin module of the four module repositories from the tags pinned in engines/external.lock.json and collects
# them in the layout of external.py fetch (modules flat, notice files in <set>/), so a release can be made from source and compared
# with the pins.
#
#   TOOLCHAIN=.../arm-none-eabi- PY=/tmp/phzvenv/bin/python engines/rebuild_pinned.sh OUT [autof|fused] [public|all]
#
# Each set is taken with git archive of its pinned tag from the local checkout named in the lock (or $EXTERNAL_SRC_<SET>, SET upper
# case with - as _), the checkout itself is never touched, and built in OUT/src/<set>/ with that repository's own build:
#   engines-os  poly-engines-os  make FLAVOUR=... OUT=... && make verify && make wasm
#   engines     poly-engines     make FLAVOUR=... OUT=... && make verify && python3 wasm/build_wasm.py --out ... --plg-dir ...
#   effects-os  poly-effects-os  make OUT=... && make verify && make wasm
#   effects     poly-effects     python3 build.py --out ... --py ...
# The pinned files are then copied to OUT/modules (flat, notices in OUT/modules/<set>/). With flavour autof (the 1.2.0 release, the
# default) OUT/modules must match the lock byte for byte (external.py check): that is the proof that the pins are reproducible.
# fused (the unpublished AUTO fallback) rebuilds the engine sets with the compiler's fused multiply add, its files differ from the
# pins by design and only the identity and checkSynth checks run. Then engines/crosscheck.sh, and the SDK examples
# (synth-open/plug/build_plugs.sh OUT/sdk-examples). Exit 1 on any failure.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
mkdir -p "${1:?output dir}"
OUT="$(cd "$1" && pwd)"
FLAVOUR="${2:-autof}"
VIS="${3:-all}"
PY="${PY:-python3}"
: "${TOOLCHAIN:?set TOOLCHAIN to the arm-none-eabi- prefix}"
export TOOLCHAIN
case $FLAVOUR in autof|fused) ;; *) echo "flavour must be autof or fused"; exit 2;; esac
LOCK="$HERE/external.lock.json"
sets=$(python3 -c "
import json,sys
s=json.load(open(sys.argv[1]))['sets']
print(' '.join(n for n in s if sys.argv[2]=='all' or s[n]['visibility']==sys.argv[2]))" "$LOCK" "$VIS")
field() { python3 -c "import json,sys,os; print(os.path.expanduser(json.load(open(sys.argv[1]))['sets'][sys.argv[2]][sys.argv[3]]))" "$LOCK" "$1" "$2"; }
rm -rf "$OUT/src" "$OUT/modules"; mkdir -p "$OUT/src" "$OUT/modules"
for s in $sets; do
    env="EXTERNAL_SRC_$(echo "$s" | tr 'a-z-' 'A-Z_')"
    repo="${!env:-$(field "$s" local_repo)}"
    tag=$(field "$s" tag); want=$(field "$s" tag_commit)
    got=$(git -C "$repo" rev-parse "$tag^{commit}" 2>/dev/null || true)
    [ "$got" = "$want" ] || { echo "$s: tag $tag is '$got' in $repo, the lock pins $want"; exit 1; }
    src="$OUT/src/$s"; mkdir -p "$src"
    git -C "$repo" archive "$tag" | tar -x -C "$src"
    echo "== $s: $repo $tag ($want), flavour $FLAVOUR"
    case $s in
        engines-os)
            make -C "$src" FLAVOUR="$FLAVOUR" OUT="$src/out/set" PY="$PY" > "$OUT/build-$s.log" 2>&1
            make -C "$src" OUT="$src/out/set" verify >> "$OUT/build-$s.log" 2>&1
            make -C "$src" OUT="$src/out/set" WASM_OUT="$src/out/wasm" wasm >> "$OUT/build-$s.log" 2>&1
            dirs="$src/out/set $src/out/wasm";;
        engines)
            make -C "$src" FLAVOUR="$FLAVOUR" OUT="$src/out/set" PY="$PY" > "$OUT/build-$s.log" 2>&1
            make -C "$src" OUT="$src/out/set" verify >> "$OUT/build-$s.log" 2>&1
            (cd "$src" && python3 wasm/build_wasm.py --out "$src/out/wasm" --plg-dir "$src/out/set") >> "$OUT/build-$s.log" 2>&1
            dirs="$src/out/set $src/out/wasm";;
        effects-os)
            make -C "$src" OUT="$src/out/set" PY="$PY" > "$OUT/build-$s.log" 2>&1
            make -C "$src" OUT="$src/out/set" verify >> "$OUT/build-$s.log" 2>&1
            make -C "$src" OUT="$src/out/set" WASM_OUT="$src/out/wasm" wasm >> "$OUT/build-$s.log" 2>&1
            dirs="$src/out/set $src/out/wasm";;
        effects)
            (cd "$src" && python3 build.py --out "$src/out/set" --py "$PY") > "$OUT/build-$s.log" 2>&1
            dirs="$src/out/set";;
        *) echo "$s: no build recipe here, add one"; exit 1;;
    esac
    tail -1 "$OUT/build-$s.log"
    python3 - "$LOCK" "$s" "$OUT/modules" $dirs <<'PY'
import json, os, shutil, sys
s = json.load(open(sys.argv[1]))["sets"][sys.argv[2]]
out, dirs = sys.argv[3], sys.argv[4:]
def get(f, sub):
    src = next((os.path.join(d, f) for d in dirs if os.path.exists(os.path.join(d, f))), None)
    if not src:
        sys.exit("%s: the build made no %s" % (sys.argv[2], f))
    os.makedirs(os.path.join(out, sub), exist_ok=True)
    shutil.copyfile(src, os.path.join(out, sub, f))
for f in s["files"]:
    get(f, "")
for f in s.get("notice_files", {}):
    get(f, sys.argv[2])
PY
done
fail=0
if [ "$FLAVOUR" = autof ]; then
    vflag=(); [ "$VIS" = all ] || vflag=(--visibility "$VIS")
    python3 -I "$HERE/external.py" check "$OUT/modules" ${vflag[@]+"${vflag[@]}"} || fail=1
    PY="$PY" bash "$HERE/crosscheck.sh" "$OUT/modules" "$VIS" > "$OUT/crosscheck.log" 2>&1 || fail=1
else
    NO_LOCK=1 PY="$PY" bash "$HERE/crosscheck.sh" "$OUT/modules" "$VIS" > "$OUT/crosscheck.log" 2>&1 || fail=1
fi
tail -1 "$OUT/crosscheck.log"
PY="$PY" bash "$ROOT/synth-open/plug/build_plugs.sh" "$OUT/sdk-examples" > "$OUT/sdk-examples.log" 2>&1 || fail=1
echo "SDK examples: $(tail -1 "$OUT/sdk-examples.log")"
[ "$fail" = 0 ] && echo "rebuild_pinned PASSED ($FLAVOUR, $VIS): $OUT/modules" || { echo "rebuild_pinned FAILED"; exit 1; }
