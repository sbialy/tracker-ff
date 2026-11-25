#!/bin/bash
# imx_check.sh: the IMX build hygiene gate of the Tracker+ and Mini sims (ref/ff-imx-port-plan.md P2). Host only, NOT firmware, no device, NO
# make plus / make mini (only `make -n`, which prints and builds nothing). Catches the 1.4.0 RC class of IMX compile errors (kInputsChannels,
# the Mini `place` argument) on every branch without a real IMX link.
#   1 DEFS parity: the -D set of the real core compile line (make -n -B plus|mini) equals the sim's (ui_shot PLATFORM=IMX, the plus pin config
#     CONSOLE=1 BRIDGE=1), up to imx_defs_exceptions.txt. A define added to core/makefile or to the sim alone fails here
#   2 the sim build with the real IMX warning set: clang -Wall -Wextra -Werror on the core objects (ui_shot Makefile IMX_WERROR=1, fresh build dir)
#   3 the real compiler's own diagnostics: the exact core compile lines of make -n -B plus|mini run with -fsyntax-only -Werror (no object, no link),
#     $TOOLCHAIN (default agt/12.3.rel1, native, the image itself is GCC 10.3). SKIP_GCC=1 skips it
#   4 the assembler pass: the same compile lines assembled (-c -w, object to /dev/null, no link). Only the assembler refuses a non-zero
#     initial value in a __NOINIT(EXTERNAL_RAM) object (`attempt to store non-zero value in section .noinit.$EXTERNAL_RAM`, the 10.3 image
#     build found ModSlot in SynthAutomation.cpp that way, -fsyntax-only never reaches the assembler). SKIP_GCC=1 skips it as well
# Usage: imx_check.sh [plus|mini|both]    Env: TOOLCHAIN, DEVTOOLS_DIR, JOBS, SKIP_GCC, IMX_CHECK_STEPS (default "1 2 3 4"), IMX_CHECK_OUT (default build/imx-check)
# Exit 0 when every step passes.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
WHICH="${1:-both}"
OUTD="${IMX_CHECK_OUT:-$ROOT/build/imx-check}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
for t in "${TOOLCHAIN:-}" "$ROOT/agt/12.3.rel1/bin/arm-none-eabi-" "$HOME/prj/poly/agt/12.3.rel1/bin/arm-none-eabi-"; do
    [ -n "$t" ] && [ -x "${t}c++" ] && { TOOLCHAIN="$t"; break; }
done
[ -x "${TOOLCHAIN:-}c++" ] || { echo "imx_check: no ${TOOLCHAIN:-}c++ (set TOOLCHAIN=.../arm-none-eabi-)" >&2; exit 2; }
if [ -z "${DEVTOOLS_DIR:-}" ]; then
    for d in "$HOME/prj/poly-devtools/.wt/rel141-int" "$HOME/prj/poly-devtools/.wt/main" "$HOME/prj/poly-devtools"; do
        grep -q UISHOT_PLUS "$d/firmware/include/UsbTest.h" 2>/dev/null && { DEVTOOLS_DIR="$d"; break; }
    done
fi
[ -n "${DEVTOOLS_DIR:-}" ] || { echo "imx_check: DEVTOOLS_DIR must name a console checkout with the UISHOT_PLUS guards" >&2; exit 2; }
. "$HERE/bridge_env.sh"
bridge_env "$ROOT" || exit 2
case "$WHICH" in plus) PRODS="PLUS";; mini) PRODS="MINI";; both) PRODS="PLUS MINI";; *) echo "usage: imx_check.sh [plus|mini|both]" >&2; exit 2;; esac
mkdir -p "$OUTD"
fails=0
STEPS=" ${IMX_CHECK_STEPS:-1 2 3 4} "
step() { case "$STEPS" in *" $1 "*) return 0;; *) return 1;; esac; }
ok()  { printf 'ok    %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails+1)); }
lower() { echo "$1" | tr 'A-Z' 'a-z'; }

# the -D tokens of the first compile line that matches $2 in the make -n output $1 (quotes of the shell are kept out: one token per line)
defs_of() { python3 -I - "$1" "$2" <<'PY'
import re,shlex,sys
for l in open(sys.argv[1], errors="replace"):
    if re.search(sys.argv[2], l):
        try: t=shlex.split(l)
        except ValueError: t=l.split()
        for x in t:
            if x.startswith("-D"): print(x)
        break
PY
}

# 1 DEFS parity
for P in $(step 1 && echo $PRODS); do
    p=$(lower "$P")
    make -n -B -C "$ROOT" "$p" TOOLCHAIN="$TOOLCHAIN" > "$OUTD/real-$p.n" 2>&1
    make -n -B -C "$HERE" PLATFORM=IMX PRODUCT=$P CONSOLE=1 "${BRIDGE_ARGS[@]}" DEVTOOLS_DIR="$DEVTOOLS_DIR" > "$OUTD/sim-$p.n" 2>/dev/null
    defs_of "$OUTD/real-$p.n" ' -DPLATFORM_IMX .*-std=gnu\+\+20' | sort -u > "$OUTD/real-$p.defs"
    defs_of "$OUTD/sim-$p.n" 'obj/core/source/mtSequencer\.o' | sort -u > "$OUTD/sim-$p.defs"
    if [ ! -s "$OUTD/real-$p.defs" ] || [ ! -s "$OUTD/sim-$p.defs" ]; then bad "defs parity $p: no compile line found (logs $OUTD/*-$p.n)"; continue; fi
    python3 -I - "$HERE/imx_defs_exceptions.txt" "$OUTD/real-$p.defs" "$OUTD/sim-$p.defs" > "$OUTD/defs-$p.diff" <<'PY'
import sys
rules=[l.split(None,1) for l in open(sys.argv[1]) if l.strip() and not l.startswith("#")]
real={l.strip() for l in open(sys.argv[2])}; sim={l.strip() for l in open(sys.argv[3])}
def ex(side,tok): return any(s==side and tok.startswith(p.strip()) for s,p in rules)
bad=[("real only",t) for t in sorted(real-sim) if not ex("real",t)]+[("sim only",t) for t in sorted(sim-real) if not ex("sim",t)]
for k,t in bad: print("%s: %s"%(k,t))
sys.exit(1 if bad else 0)
PY
    if [ $? = 0 ]; then ok "defs parity $P: make $p core DEFS == the sim's ($(wc -l <"$OUTD/real-$p.defs" | tr -d ' ') real, $(wc -l <"$OUTD/sim-$p.defs" | tr -d ' ') sim, exceptions in imx_defs_exceptions.txt)"
    else cat "$OUTD/defs-$p.diff"; bad "defs parity $P: drift between make $p and the sim (above)"; fi
done

# 2 the sim build with the real warning set, fresh dir (make does not track flags)
for P in $(step 2 && echo $PRODS); do
    p=$(lower "$P")
    BUILD="$OUTD/sim-werror-$p"
    rm -rf "$BUILD"
    if make -C "$HERE" -j"$JOBS" PLATFORM=IMX PRODUCT=$P CONSOLE=1 "${BRIDGE_ARGS[@]}" DEVTOOLS_DIR="$DEVTOOLS_DIR" IMX_WERROR=1 OUT="$BUILD" >"$OUTD/sim-werror-$p.log" 2>&1; then
        ok "sim -Werror build $P (clang -Wall -Wextra on the core objects, IMX DEFS)"
    else grep -E ' error:|Error [0-9]' "$OUTD/sim-werror-$p.log" | head -20; bad "sim -Werror build $P (log $OUTD/sim-werror-$p.log)"; fi
done

# 3 the real compiler's diagnostics, no object
if [ "${SKIP_GCC:-}" = 1 ] || ! step 3; then echo "skip  gcc syntax pass"; else
for P in $PRODS; do
    p=$(lower "$P")
    python3 -I - "$OUTD/real-$p.n" "$ROOT/core" "$OUTD/gcc-$p.cmds" <<'PY'
import re,sys
n,core,out=sys.argv[1:4]
cmds=[]
for l in open(n, errors="replace"):
    if re.search(r'c\+\+ .* -DPLATFORM_IMX .*-std=gnu\+\+20', l):
        l=re.sub(r'\s-MMD\s-MP',' ',l)
        l=re.sub(r'-o\s+"[^"]*"',' -fsyntax-only ',l.rstrip("\n"))
        cmds.append(l)
open(out,"w").write("\n".join(cmds)+"\n")
print(len(cmds))
PY
    # known stock diagnostics that cannot change without changing behaviour: mtSequencer.cpp play_microStep (row may be -1 to the compiler, -Warray-bounds),
    # UartMidiImpl.cpp (the MIDI library marks sendPolyPressure deprecated)
    sed -i.bak 's# -fsyntax-only # -fsyntax-only -Werror -Wno-error=array-bounds -Wno-error=deprecated-declarations #' "$OUTD/gcc-$p.cmds"
    n=$(grep -c . "$OUTD/gcc-$p.cmds")
    if python3 -I - "$ROOT/core" "$OUTD/gcc-$p.cmds" "$JOBS" >"$OUTD/gcc-$p.log" 2>&1 <<'PY'
import subprocess,sys
from concurrent.futures import ThreadPoolExecutor
core,cmds,jobs=sys.argv[1],sys.argv[2],int(sys.argv[3])
def run(c):
    r=subprocess.run(c,shell=True,cwd=core,capture_output=True,text=True)
    return r.returncode,r.stderr+r.stdout
rc=0
with ThreadPoolExecutor(jobs) as ex:
    for code,out in ex.map(run,[l for l in open(cmds).read().split("\n") if l.strip()]):
        if out.strip(): print(out)
        rc|=code!=0
sys.exit(1 if rc else 0)
PY
    then ok "gcc -fsyntax-only -Werror $P ($n core files, $(${TOOLCHAIN}gcc -dumpversion), the real flags of make $p)"
    else grep -E 'error' "$OUTD/gcc-$p.log" | head -20; bad "gcc -fsyntax-only $P (log $OUTD/gcc-$p.log)"; fi
done; fi

# 4 the assembler pass (same lines as 3, assembled with the real -O2 flags, no warnings, nothing kept)
if [ "${SKIP_GCC:-}" = 1 ] || ! step 4; then echo "skip  gcc assembler pass"; else
for P in $PRODS; do
    p=$(lower "$P")
    python3 -I - "$OUTD/real-$p.n" "$OUTD/asm-$p.cmds" <<'PY'
import re,sys
n,out=sys.argv[1:3]
cmds=[]
for l in open(n, errors="replace"):
    if re.search(r'c\+\+ .* -DPLATFORM_IMX .*-std=gnu\+\+20', l):
        l=re.sub(r'\s-MMD\s-MP',' ',l)
        l=re.sub(r'\s-fstack-usage',' ',l)
        l=re.sub(r'-o\s+"[^"]*"',' -c -o /dev/null -w ',l.rstrip("\n"))
        cmds.append(l)
open(out,"w").write("\n".join(cmds)+"\n")
PY
    n=$(grep -c . "$OUTD/asm-$p.cmds")
    if python3 -I - "$ROOT/core" "$OUTD/asm-$p.cmds" "$JOBS" >"$OUTD/asm-$p.log" 2>&1 <<'PY'
import subprocess,sys
from concurrent.futures import ThreadPoolExecutor
core,cmds,jobs=sys.argv[1],sys.argv[2],int(sys.argv[3])
def run(c):
    r=subprocess.run(c,shell=True,cwd=core,capture_output=True,text=True)
    return r.returncode,r.stderr+r.stdout
rc=0
with ThreadPoolExecutor(jobs) as ex:
    for code,out in ex.map(run,[l for l in open(cmds).read().split("\n") if l.strip()]):
        if out.strip(): print(out)
        rc|=code!=0
sys.exit(1 if rc else 0)
PY
    then ok "gcc assembler pass $P ($n core files assembled, no NOLOAD section gets a non-zero initial value)"
    else grep -E 'rror' "$OUTD/asm-$p.log" | head -20; bad "gcc assembler pass $P (log $OUTD/asm-$p.log)"; fi
done; fi

[ "$fails" = 0 ] && { echo "imx_check: PASS"; exit 0; } || { echo "imx_check: FAIL ($fails)"; exit 1; }
