#!/bin/bash
# fw_engine_mismatch_sim.sh [PNG_DIR]
#
# Firmware and engine plugins from different releases, on the Mac host simulator. The two releases are the tree this script sits in
# (1.3.0, the "new" firmware, its ui_shot host with the CHIP twin) and FW12_REF (default release-1.2.0-pub6, the published 1.2.0, built
# from a git archive into build/fw-eng-mismatch with the one extra "poke picker" action of fw_mismatch_probe12.py). The "old" plugins are the
# 1.2.0 release files, rebuilt from the module repository tags the 1.2.0 lock pins (engines-os, effects-os, engines) and checked against
# the sha256 of that lock, so they are the shipped bytes. The "new" plugins are the 1.3.0 set (PLG_DIR).
#
#   A  1.3.0 firmware + 1.2.0 plugins       (engines and /Plugins/Master inserts, a 1.2 project)
#   B  1.2.0 firmware + 1.3.0 plugins       (chip.plg duplicates the built-in CHIP, a project that 1.3.0 wrote, .patch field 100)
#   C  a card with plugins of both releases (half upgraded, two versions of one plugin, the other order of installing)
#
# What the host does and does not prove: the scan of /Plugins/Synth and /Plugins/Master, the header and CRC checks, the engine list, the
# Missing state, the insert slots (Tape, Lo-Fi, Dirt twins), the project and patch files and the codecs are the real firmware code of each
# tree. A .plg is Thumb code and never runs here (a synth engine is only listed, the CHIP twin aside), so a runtime ABI mismatch
# (a plugin that calls a service the host does not have) is proven by comparing the ABI structs and the service table of both trees, not
# by running. Nothing touches a device. Every scenario has its own card folder.
#
# The release gate (synth-open/tools/release_gate.sh) runs this script against the previous release tag and fails the release on any RED
# check whose F<n> tag is not in synth-open/tools/ui_shot/mismatch_known_findings.txt. Name a new finding check "F<n>: ..." so it can be listed.
#
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)" (exit 1). A RED check is a finding about the release pair, see
# the project notes for what each one means. A KNOWN line is a finding the project accepted (F1: a
# 1.3.0 Scale patch with a macro amount plays as Add on 1.2.x): it is listed and never makes the result RED.
#
#   synth-open/tools/ui_shot/fw_engine_mismatch_sim.sh [PNG_DIR]
#   PLG_DIR        the 1.3.0 plugin set, flat folder of .plg and .plgi (default build/release-1.3.0-rc/modules)
#   FW12_REF       git ref of the old firmware in this repository (default release-1.2.0-pub6, else release-1.2.0, the gate passes the latest
#                  published release tag)
#   TOOLCHAIN      arm-none-eabi- prefix of Arm GNU 12.3.rel1 (default agt/12.3.rel1/bin/arm-none-eabi-), builds the 1.2.0 plugins
#   PLG12_DIR      a flat folder of the 1.2.0 .plg and .plgi instead of building them (still checked against the 1.2.0 lock)
#   ENGINES_OS_REPO EFFECTS_OS_REPO ENGINES_REPO   module repositories (default ../poly-engines-os, poly-effects-os, poly-engines), the
#                  private one is optional: without it PHZ and PLK of 1.2.0 are not on the 1.2 card and their checks say SKIP
#   CHIP_PLUGIN_SRC engine_chip.cpp of the 1.3.0 engines-os (default ../poly-engines-os/src/synth-open/engine_chip.cpp)
#   REBUILD=1      throw the cache of build/fw-eng-mismatch away first
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
CACHE="$ROOT/build/fw-eng-mismatch"
[ "${REBUILD:-}" = 1 ] && rm -rf "$CACHE"
mkdir -p "$CACHE"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/fw_engine_mismatch.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
NCPU="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

PLG13="${PLG_DIR:-$ROOT/build/release-1.3.0-rc/modules}"
[ -f "$PLG13/ju6.plg" ] || { echo "fw_engine_mismatch_sim.sh: no 1.3.0 plugins (set PLG_DIR to a folder with ju6.plg ...)" >&2; exit 2; }
FW12_REF="${FW12_REF:-}"
if [ -z "$FW12_REF" ]; then
    for r in release-1.2.0-pub6 release-1.2.0; do git -C "$ROOT" rev-parse --verify -q "$r^{commit}" >/dev/null && { FW12_REF="$r"; break; }; done
fi
[ -n "$FW12_REF" ] || { echo "fw_engine_mismatch_sim.sh: no 1.2.0 ref (set FW12_REF)" >&2; exit 2; }
TC="${TOOLCHAIN:-$ROOT/agt/12.3.rel1/bin/arm-none-eabi-}"
EOS="${ENGINES_OS_REPO:-$ROOT/../poly-engines-os}"; FOS="${EFFECTS_OS_REPO:-$ROOT/../poly-effects-os}"; PEN="${ENGINES_REPO:-$ROOT/../poly-engines}"
CHIP_SRC="${CHIP_PLUGIN_SRC:-$ROOT/../poly-engines-os/src/synth-open/engine_chip.cpp}"
[ -f "$CHIP_SRC" ] || { echo "fw_engine_mismatch_sim.sh: no engine_chip.cpp (set CHIP_PLUGIN_SRC)" >&2; exit 2; }
MASTER_NAMES="bbd console dirt glue lofi phaser tape tube"   # the other .plg files of a set are synth engines

# ------------------------------------------------------------------------------------------------------------------ python helpers
cat >"$WORK/fwm.py" <<'PY'
import hashlib, json, os, re, struct, subprocess, sys

def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()

def lock_files(path):          # {file: sha256} of every .plg/.plgi of every set of an external.lock.json
    d = json.load(open(path))
    out = {}
    for s in d['sets'].values():
        for f, h in s.get('files', {}).items():
            if f.endswith(('.plg', '.plgi')):
                out[f] = h
    return out

def lock_tags(path):
    d = json.load(open(path))
    return {k: v['tag'] for k, v in d['sets'].items()}

def hdr(p):
    b = open(p, 'rb').read()
    (magic, hs, amaj, amin, kind, cpu, flags, uid, ver, minhost, isz, bss, rc, ent, ram, cyc, crc, rw) = struct.unpack_from('<IHBBBBHIIIIIIIIIII', b, 0)
    return dict(magic=magic, header_size=hs, abi_major=amaj, abi_minor=amin, kind=kind, cpu=cpu, flags=flags, uid=uid, version=ver,
                min_host=minhost, image_size=isz, bss_size=bss, rebase_count=rc, crc32=crc, rw_offset=rw)

FIELDS = {'magic': (0, '<I'), 'header_size': (4, '<H'), 'abi_major': (6, 'B'), 'abi_minor': (7, 'B'), 'kind': (8, 'B'), 'cpu': (9, 'B'),
          'flags': (10, '<H'), 'uid': (12, '<I'), 'version': (16, '<I'), 'min_host': (20, '<I'), 'image_size': (24, '<I'),
          'bss_size': (28, '<I'), 'rebase_count': (32, '<I'), 'crc32': (48, '<I')}

def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)

def abi_facts(path):
    t = strip_comments(open(path, encoding='utf-8', errors='replace').read())
    defs = dict(re.findall(r'#define\s+(PLUG_[A-Z0-9_]+)\s+(\S+)', t))
    structs = {}
    for m in re.finditer(r'typedef\s+struct[^{]*\{(.*?)\}\s*(\w+)\s*;', t, flags=re.S):
        structs[m.group(2)] = re.sub(r'\s+', ' ', m.group(1)).strip()
    enums = {}
    for m in re.finditer(r'enum[^{;]*\{(.*?)\}', t, flags=re.S):
        for name, val in re.findall(r'(\w+)\s*=\s*([^,}]+)', m.group(1)):
            enums[name] = re.sub(r'\s+', ' ', val.strip())
    return defs, structs, enums

cmd = sys.argv[1]
if cmd == 'tag':                       # tag LOCK SET
    print(lock_tags(sys.argv[2])[sys.argv[3]])
elif cmd == 'delta':                   # delta LOCK_OLD LOCK_NEW -> "changed|added|removed FILE"
    a, b = lock_files(sys.argv[2]), lock_files(sys.argv[3])
    for f in sorted(set(a) | set(b)):
        if a.get(f) != b.get(f):
            print('added' if f not in a else ('removed' if f not in b else 'changed'), f)
elif cmd == 'verify':                  # verify LOCK DIR... -> "ok N" and "bad FILE" / "none FILE" for files not in the lock
    lk = lock_files(sys.argv[2])
    n = 0
    for d in sys.argv[3:]:
        for f in sorted(os.listdir(d)):
            if not f.endswith(('.plg', '.plgi')):
                continue
            if f not in lk:
                print('none', f)
            elif sha(os.path.join(d, f)) != lk[f]:
                print('bad', f)
            else:
                n += 1
    print('ok', n)
elif cmd == 'mut':                     # mut SRC DST FIELD VALUE   (FIELD payload: flip the image byte VALUE, the header CRC stays)
    b = bytearray(open(sys.argv[2], 'rb').read())
    f, v = sys.argv[4], int(sys.argv[5], 0)
    if f == 'payload':
        b[v] ^= 0xFF
    else:
        o, fmt = FIELDS[f]
        struct.pack_into(fmt, b, o, v)
    open(sys.argv[3], 'wb').write(b)
elif cmd == 'seed':                    # seed PROJECT P0 P1 P2: the stored engine numbers of the three synth parts (synthType bytes)
    b = bytearray(open(sys.argv[2], 'rb').read())
    for o, v in zip((1876, 1940, 2004), sys.argv[3:6]):
        b[o] = int(v)
    open(sys.argv[2], 'wb').write(b)
elif cmd == 'stored':                  # stored PROJECT -> the three synthType bytes
    b = open(sys.argv[2], 'rb').read()
    print(*[b[o] for o in (1876, 1940, 2004)])
elif cmd == 'stamp':                   # stamp PROJECT -> fwVersion of the header, bytes 4..7
    b = open(sys.argv[2], 'rb').read()
    print('%d.%d.%d' % (b[4], b[5], b[6]))
elif cmd == 'projdiff':                # projdiff A B -> the byte offsets that differ
    a, b = open(sys.argv[2], 'rb').read(), open(sys.argv[3], 'rb').read()
    print('size', len(a), len(b), 'diff', ','.join(str(i) for i in range(min(len(a), len(b))) if a[i] != b[i]))
elif cmd == 'abi':                     # abi OLD_H NEW_H -> lines "same X" / "DIFF X" / "added X" for constants, structs, enum members
    d1, s1, e1 = abi_facts(sys.argv[2])
    d2, s2, e2 = abi_facts(sys.argv[3])
    for k in ('PLUG_MAGIC', 'PLUG_ABI_MAJOR', 'PLUG_ABI_MINOR', 'PLUG_HOST_LEVEL'):
        print('same' if d1.get(k) == d2.get(k) else 'DIFF', k, d1.get(k), d2.get(k))
    for k in sorted(s1):
        print('same' if s1[k] == s2.get(k) else 'DIFF', 'struct', k)
    for k in sorted(e1):
        print('same' if e1[k] == e2.get(k) else 'DIFF', 'enum', k)
    for k in sorted(set(e2) - set(e1)):
        print('added', 'enum', k, e2[k])
    for k in sorted(set(s2) - set(s1)):
        print('added', 'struct', k)
elif cmd == 'pairs':                   # pairs DIR... -> the insert pairs (slot 1, slot 2) that do not fit the shared 8 KB slot, plug_index.h insertPlace
    sizes = {}
    for d in sys.argv[2:]:
        for f in sorted(os.listdir(d)):
            if f.endswith('.plg'):
                h = hdr(os.path.join(d, f))
                sizes[f[:-4]] = h['image_size'] + h['bss_size']
    room = 8192
    top = lambda foot: (room - foot) & ~15
    bad = []
    for a in sizes:
        for b in sizes:
            if a != b and sizes[a] > top(sizes[b]):
                bad.append('%s+%s=%d' % (a, b, sizes[a] + sizes[b]))
    print('sizes', ' '.join('%s=%d' % kv for kv in sorted(sizes.items())))
    print('nofit', ' '.join(bad) if bad else '-')
elif cmd == 'edgepatch':               # edgepatch BASE OUTDIR -> patches whose unknown-field block is at the cap of 1.2.0 (kRawMax 768) with and without field 100
    base = open(sys.argv[2], 'rb').read()
    def varint(n):
        o = bytearray()
        while True:
            c = n & 0x7f
            n >>= 7
            if n:
                o.append(c | 0x80)
            else:
                o.append(c)
                return bytes(o)
    def unk(total):
        for plen in range(total):
            t = bytes([0x62]) + varint(plen) + bytes(plen)
            if len(t) == total:
                return t
    raw = int(sys.argv[4])             # the raw block the base patch already has
    for tag, room in (('fits', 768 - 3 - raw), ('over', 768 - 2 - raw)):
        blk = unk(room)
        open(os.path.join(sys.argv[3], 'edge_%s_add.patch' % tag), 'wb').write(base + blk)
        open(os.path.join(sys.argv[3], 'edge_%s_scale.patch' % tag), 'wb').write(base + blk + bytes([0xA0, 6, 1]))
PY
py() { python3 -I "$WORK/fwm.py" "$@"; }

fails=0; checks=0; knowns=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
expect() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }
note() { printf '      %s\n' "$*"; }
# a known accepted finding of the release pair: listed, counted as a check, never a failure
known() { checks=$((checks+1)); knowns=$((knowns+1)); printf 'KNOWN %s\n' "$1"; }

# ------------------------------------------------------------------------------------------------------------------ builds
echo "### setup: the two hosts and the 1.2.0 plugin set"
# the version of the tree under test (the stamp of its project header, 1.2.0 is 1.2.0): 1.3.0 differs from 1.2.0 in byte 5 only, 1.3.1 in bytes 5 and 6
VER13="$(sed -n 's/^SYNTH_OPEN_VER[[:space:]]*:\{0,1\}=[[:space:]]*//p' "$ROOT/version.mk" | head -1 | sed 's/-.*//')"
PROJDIFF="5,2320,2321,2322,2323"; [ "${VER13##*.}" = 0 ] || PROJDIFF="5,6,2320,2321,2322,2323"
LOCK12="$WORK/lock12.json"; LOCK13="$WORK/lock13.json"
git -C "$ROOT" show "$FW12_REF:engines/external.lock.json" >"$LOCK12" || { echo "no external.lock.json in $FW12_REF" >&2; exit 2; }
cp "$ROOT/engines/external.lock.json" "$LOCK13"
echo "     new firmware: $(git -C "$ROOT" describe --always --dirty 2>/dev/null || echo '?') ($(git -C "$ROOT" rev-parse --abbrev-ref HEAD))   old firmware: $FW12_REF $(git -C "$ROOT" rev-parse --short "$FW12_REF")"

# new host (1.3.0): CHIP twin, insert twins from the effects-os checkout at the pinned tag (without them Tape and Lo-Fi cannot load)
B13="$ROOT/build/ui-shot-chipplg"; H13="$B13/ui_shot"
mkdir -p "$B13/external"
TWINS=1
python3 -I "$ROOT/engines/external.py" source effects-os "$B13/external" plug/fx/ot_tape_plug.c plug/fx/ot_lofi_plug.c plug/fx/ot_bbd_plug.c plug/fx/ot_phaser_plug.c plug/fx_dirt_plug.cpp >"$WORK/twin13.log" 2>&1 || { TWINS=0; echo "     no effects-os twin sources for the new host: the insert checks are skipped"; }
make -s -C "$HERE" -j"$NCPU" CHIP_PLUGIN_SRC="$CHIP_SRC" >"$B13.make.log" 2>&1 || { tail -30 "$B13.make.log"; exit 2; }

# old plugins: rebuilt from the tags of the 1.2.0 lock, then checked against the sha256 of that lock
SET12="$CACHE/set12"
build_module() {   # build_module NAME REPO LOCKSET -> $CACHE/src-NAME-TAG/out (a git archive of the pinned tag)
    local name="$1" repo="$2" set="$3" tag dir
    tag="$(py tag "$LOCK12" "$set")"
    dir="$CACHE/src-$name-$tag"
    if [ ! -f "$dir/out/SHA256SUMS" ]; then
        [ -d "$repo/.git" ] || [ -f "$repo/.git" ] || return 1
        rm -rf "$dir"; mkdir -p "$dir"
        git -C "$repo" archive "$tag" | tar -x -C "$dir" || return 1
        make -C "$dir" TOOLCHAIN="$TC" OUT="$dir/out" PY=python3 >"$dir/make.log" 2>&1 || { tail -20 "$dir/make.log"; return 1; }
    fi
    echo "$dir"
}
if [ ! -f "$SET12/.ok" ]; then
    rm -rf "$SET12"; mkdir -p "$SET12/synth" "$SET12/master"
    if [ -n "${PLG12_DIR:-}" ]; then
        for f in "$PLG12_DIR"/*.plg "$PLG12_DIR"/*.plgi; do [ -f "$f" ] && cp "$f" "$SET12/_flat_$(basename "$f")"; done
    else
        [ -f "${TC}gcc" ] || { echo "fw_engine_mismatch_sim.sh: no ${TC}gcc (set TOOLCHAIN, or PLG12_DIR with the 1.2.0 plugins)" >&2; exit 2; }
        d="$(build_module engines-os "$EOS" engines-os)" || { echo "the 1.2.0 engines-os build failed" >&2; exit 2; }
        for f in "$d"/out/*.plg "$d"/out/*.plgi; do cp "$f" "$SET12/_flat_$(basename "$f")"; done
        d="$(build_module effects-os "$FOS" effects-os)" || { echo "the 1.2.0 effects-os build failed" >&2; exit 2; }
        for f in "$d"/out/*.plg; do cp "$f" "$SET12/_flat_$(basename "$f")"; done
        if d="$(build_module engines "$PEN" engines)"; then
            for f in phz pluck; do cp "$d/out/$f.plg" "$d/out/$f.plgi" "$SET12/" 2>/dev/null; for g in "$SET12/$f".plg*; do mv "$g" "$SET12/_flat_$(basename "$g")"; done; done
        else echo "     SKIP: no private engines repository, PHZ and PLK of 1.2.0 are not on the old card"; fi
    fi
    # Console and Tube (private effects repo) did not change between the releases: the 1.3.0 files stand in for the 1.2.0 ones, the lock check below proves it
    for f in console tube; do cp "$PLG13/$f.plg" "$SET12/_flat_$f.plg"; done
    for f in "$SET12"/_flat_*; do
        b="$(basename "$f")"; b="${b#_flat_}"; n="${b%.*}"
        case " $MASTER_NAMES " in *" $n "*) mv "$f" "$SET12/master/$b" ;; *) mv "$f" "$SET12/synth/$b" ;; esac
    done
    touch "$SET12/.ok"
fi
SYN12="$SET12/synth"; FX12="$SET12/master"
# the 1.3.0 set split the same way
SET13="$CACHE/set13"; rm -rf "$SET13"; mkdir -p "$SET13/synth" "$SET13/master"
for f in "$PLG13"/*.plg "$PLG13"/*.plgi; do
    [ -f "$f" ] || continue
    b="$(basename "$f")"; n="${b%.*}"
    case "$n" in dirt|bbd|tape|lofi|phaser|console|tube|glue) [ "${b##*.}" = plg ] && cp "$f" "$SET13/master/" ;; *) cp "$f" "$SET13/synth/" ;; esac
done
SYN13="$SET13/synth"; FX13="$SET13/master"

# old host (1.2.0): git archive of FW12_REF, the probe, insert twins from the 1.2.0 effects-os archive
B12="$CACHE/fw12"; H12="$B12/build/ui-shot/ui_shot"
PROBE_SUM="$(shasum "$HERE/fw_mismatch_probe12.py" | cut -c1-12) $FW12_REF"
[ "$(cat "$B12/.probe" 2>/dev/null)" = "$PROBE_SUM" ] || rm -rf "$B12"
if [ ! -x "$H12" ]; then
    rm -rf "$B12"; mkdir -p "$B12"
    git -C "$ROOT" archive "$FW12_REF" | tar -x -C "$B12" || { echo "git archive $FW12_REF failed" >&2; exit 2; }
    python3 -I "$HERE/fw_mismatch_probe12.py" "$B12" || exit 2
    mkdir -p "$B12/build/ui-shot/external"
    if d="$(build_module effects-os "$FOS" effects-os)"; then
        for f in ot_tape_plug.c ot_lofi_plug.c ot_bbd_plug.c ot_phaser_plug.c; do cp "$d/src/synth-open/plug/fx/$f" "$B12/build/ui-shot/external/"; done
        cp "$d/src/synth-open/plug/fx_dirt_plug.cpp" "$B12/build/ui-shot/external/"
    fi
    make -s -C "$B12/synth-open/tools/ui_shot" -j"$NCPU" >"$B12/make.log" 2>&1 || { tail -30 "$B12/make.log"; exit 2; }
    echo "$PROBE_SUM" >"$B12/.probe"
fi
[ -x "$H12" ] || { echo "no old host binary $H12" >&2; exit 2; }

# ------------------------------------------------------------------------------------------------------------------ helpers
SYNTH='instr 64; tap Params; wait 1500'
runh() {   # runh HOST SD "script": the output of "poke" actions in $WORK/poke.out
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$1" "$2" "$3" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] || : >"$WORK/poke.out"
}
card() {   # card DIR SYNTH_DIR FX_DIR [base names left out]: an SD folder with those plugins
    local sd="$1" sy="$2" fx="$3" f b x skip; shift 3
    mkdir -p "$sd"; rm -rf "$sd/Plugins"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    for f in "$sy"/*.plg "$sy"/*.plgi; do
        [ -f "$f" ] || continue
        b="$(basename "$f")"; skip=0; for x in "$@"; do [ "${b%.*}" = "$x" ] && skip=1; done
        [ "$skip" -eq 1 ] || cp "$f" "$sd/Plugins/Synth/"
    done
    for f in "$fx"/*.plg; do [ -f "$f" ] || continue; b="$(basename "$f")"; skip=0; for x in "$@"; do [ "${b%.*}" = "$x" ] && skip=1; done; [ "$skip" -eq 1 ] || cp "$f" "$sd/Plugins/Master/"; done
}
prime() { "$1" "$2" "wait 100" >/dev/null 2>&1; }   # a first power up writes the Workspace project and patches
picker_line() { grep '^picker' "$WORK/poke.out" | sed -n "${1}p"; }
list_of() { picker_line "$1" | sed 's/.* list //'; }
cursor_of() { picker_line "$1" | sed 's/.* cursor \([-0-9]*\) list.*/\1/'; }
part_line() { grep '^part' "$WORK/poke.out" | sed -n "$(( ($1 - 1) * 3 + $2 + 1 ))p"; }     # picker N, part P (0..2)
part_engine() { part_line "$1" "$2" | sed 's/.* engine //'; }
rhs_stored() { grep '^rhs' "$WORK/poke.out" | sed -n "${1}p" | sed 's/.*stored\[\([^]]*\)\].*/\1/'; }
count_lines() { grep -c "$1" "$2" 2>/dev/null || true; }
syn_listed() { [ -f "$1/Plugins/synth.txt" ] || { echo 0; return; }; sed -n 's/^synth library: files [0-9]* listed \([0-9]*\) .*/\1/p' "$1/Plugins/synth.txt"; }
syn_files() { sed -n 's/^synth library: files \([0-9]*\) listed .*/\1/p' "$1/Plugins/synth.txt"; }
syn_engine() { grep "^$2 " "$1/Plugins/synth.txt" | head -1 | tr -d '\r'; }                      # the line of a .plg in synth.txt
ins_line() { grep "^insert $2:" "$1/Plugins/host.txt" | head -1 | tr -d '\r'; }                  # insert 1 / 2 of host.txt
ins_state() { ins_line "$1" "$2" | sed 's/.* state \([0-9]*\) err .*/\1/'; }
ins_err() { ins_line "$1" "$2" | sed 's/.* err \([0-9]*\) text.*/\1/'; }
scan_line() { sed -n 's/^scan \/Plugins\/Master: \(.*\)/\1/p' "$1/Plugins/host.txt" | tr -d '\r'; }
sum() { shasum "$@" 2>/dev/null | cut -c1-12 | tr '\n' ' '; }
nfiles() { ls "$1"/*.plg 2>/dev/null | wc -l | tr -d ' '; }
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 10"; }
# state of insert slot N from the last "poke inserts" line (running or bypassed: the plugin is loaded, the project's mixer stage is not Inserts here)
st_of() { grep '^inserts' "$WORK/poke.out" | tail -1 | tr ' ' '\n' | grep "^$1=" | sed 's/^[0-9]=\([a-z]*\):.*/\1/'; }
loaded() { case "$1" in running|bypassed) echo loaded ;; *) echo "$1" ;; esac; }
mk12proj() {   # mk12proj DIR: a Workspace the 1.2.0 firmware wrote, part 1 on the built-in CHIP (with its CHIP patch), parts 2 and 3 stored as JU6 and PLK
    local sd="$1"; rm -rf "$sd"; mkdir -p "$sd"; prime "$H12" "$sd"
    "$H12" "$sd" "$SYNTH; tap Down 2; wait 600; tap Pattern; wait 600" >/dev/null 2>&1
    py seed "$sd/Workspace/project.mt" 6 7 18
}
sidecar() {   # sidecar FILE slot1-uid slot1-name slot2-uid slot2-name
    cat >"$1" <<EOF
tffplug 1
format 2
mixer 10
slot 1 $2 00010000 $3
p Drive=30
p Mix=80
slot 2 $4 00010000 $5
p Mix=40
EOF
}

# ------------------------------------------------------------------------------------------------------------------ 0: what differs
echo "### scenario 0: what differs between the two releases (ABI, plugin files)"
py abi "$B12/synth-open/plug/plug_abi.h" "$ROOT/synth-open/plug/plug_abi.h" >"$WORK/abi.txt"
cat "$WORK/abi.txt" | grep -v '^same' | sed 's/^/        /'
# PLUG_ABI_MINOR counts additive changes (plug_abi.h: new tail fields, optional entry points, host services): 1.3.0 raised it to 2 for
# PLUG_EXT_CHORD. Major, host level and magic must stay, minor must not go down. The struct and enum checks below catch a real break.
expect "ABI: major, host level and magic are the same constants" "3" "$(grep '^same PLUG_' "$WORK/abi.txt" | grep -vc '^same PLUG_ABI_MINOR ')"
expect "ABI: the minor version did not go down (additive changes only)" "ok" "$(awk '$2=="PLUG_ABI_MINOR"{print ($4+0 >= $3+0) ? "ok" : "lower " $3 " -> " $4}' "$WORK/abi.txt")"
expect "ABI: every struct that crosses the boundary (header, manifest, param, desc, host table, synth extension) is field for field the same" "0" "$(grep -c '^DIFF struct' "$WORK/abi.txt")"
expect "ABI: no enum value of 1.2.0 changed (kinds, cpus, extension ids, param flags are only added)" "0" "$(grep -c '^DIFF enum' "$WORK/abi.txt")"
HT12="$(sed -n '/^const plug_host s_host = {/,/^};/p' "$B12/synth-open/plug_host.cpp" | tr -d ' \r\n')"
HT13="$(sed -n '/^const plug_host s_host = {/,/^};/p' "$ROOT/synth-open/plug_host.cpp" | tr -d ' \r\n')"
[ -n "$HT13" ] && expect "the host service table the plugin gets (plug_host.cpp s_host) is the same list of functions" "$HT12" "$HT13" || fail "found the service table"
expect "the synth extension layout constant (kLayout, plug_synth.cpp) is the same" "$(sed -n 's/.*constexpr uint32_t kLayout *= *\([^;]*\);.*/\1/p' "$B12/synth-open/plug_synth.cpp" | head -1)" "$(sed -n 's/.*constexpr uint32_t kLayout *= *\([^;]*\);.*/\1/p' "$ROOT/synth-open/plug_synth.cpp" | head -1)"
py verify "$LOCK12" "$SYN12" "$FX12" >"$WORK/v12.txt"
expect "the 1.2.0 plugin files on the old card are the shipped ones (sha256 of the 1.2.0 lock)" "0 0" "$(grep -c '^bad' "$WORK/v12.txt") $(grep -c '^none' "$WORK/v12.txt")"
note "checked $(grep '^ok' "$WORK/v12.txt")"
py verify "$LOCK13" "$SYN13" "$FX13" >"$WORK/v13.txt"
expect "the 1.3.0 plugin files are the ones the 1.3.0 lock pins" "0" "$(grep -c '^bad' "$WORK/v13.txt")"
py delta "$LOCK12" "$LOCK13" >"$WORK/delta.txt"
echo "     files that differ between the two locks:"; sed 's/^/        /' "$WORK/delta.txt"
unknown="$(grep -v -E '^(added|changed) (chip\.plg|chip\.plgi|glue\.plg|pluck\.plg)$' "$WORK/delta.txt" | tr '\n' ' ')"
expect "the plugin files of 1.2.0 and 1.3.0 differ only in CHIP (new), Glue (new) and PLK (1.0 -> 1.1): anything else is a new pair to review" "" "$unknown"
for f in ju6 fm4 btype dtype cstype atype ttype eight hand arc; do cmp -s "$SYN12/$f.plg" "$SYN13/$f.plg" || unknown="$unknown $f"; done
for f in bbd dirt lofi phaser tape console tube; do cmp -s "$FX12/$f.plg" "$FX13/$f.plg" || unknown="$unknown $f"; done
expect "the 10 public engines and the 7 inserts of 1.2.0 are byte for byte the 1.3.0 files" "" "$unknown"
for f in "$SYN12/ju6.plg" "$SYN13/chip.plg" "$SYN13/pluck.plg"; do
    python3 -I - "$f" <<'PY'
import struct, sys
b = open(sys.argv[1], 'rb').read()
print('        %-9s abi %d.%d min_host %d version %06x' % (sys.argv[1].split('/')[-1], b[6], b[7], struct.unpack_from('<I', b, 20)[0], struct.unpack_from('<I', b, 16)[0]))
PY
done

# ------------------------------------------------------------------------------------------------------------------ A
echo "### scenario A1: 1.3.0 firmware, card with the 1.2.0 plugins (no chip.plg)"
N12="$(nfiles "$SYN12")"
sd="$WORK/a1"; card "$sd" "$SYN12" "$FX12"; prime "$H13" "$sd"
runh "$H13" "$sd" "$SYNTH; $(shot a1-card12); poke picker"
expect "every 1.2.0 engine file is in the library" "$N12 $N12" "$(syn_files "$sd") $(syn_listed "$sd")"
expect "no engine file has an error" "0" "$(grep ' stored ' "$sd/Plugins/synth.txt" | grep -vc ' err 0')"
expect "the master insert scan lists every 1.2.0 insert, none rejected" "listed $(nfiles "$FX12"), rejected 0" "$(scan_line "$sd")"
expect "the Model list is the 5 built-in engines and the $N12 plugin engines" "$((5 + N12))" "$(list_of 1 | tr ',' '\n' | wc -l | tr -d ' ')"
case ",$(list_of 1)," in *,CHIP,*) fail "CHIP is not in the list without chip.plg" "$(list_of 1)" ;; *) pass "CHIP is not in the list without chip.plg (it is a plugin since 1.3.0)" ;; esac
LIST_A1="$(list_of 1)"

echo "### scenario A2: a 1.2.0 project (parts on CHIP, JU6, PLK) on 1.3.0 with the 1.2.0 card"
sd="$WORK/a2"; mk12proj "$sd"; card "$sd" "$SYN12" "$FX12"
expect "the project was written by the 1.2.0 firmware (header stamp)" "1.2.0" "$(py stamp "$sd/Workspace/project.mt")"
S0="$(sum "$sd"/Workspace/project.mt "$sd"/Workspace/?.patch)"
runh "$H13" "$sd" "$SYNTH; $(shot a2-chip-missing); poke picker; poke rhs; instr 65; tap Pattern; wait 300; tap Params; wait 1500; poke picker; poke rhs; instr 66; tap Pattern; wait 300; tap Params; wait 1500; poke picker; poke rhs"
expect "part 1 keeps its stored number 6" "6" "$(part_line 1 0 | sed 's/.* stored \([0-9]*\) engine.*/\1/')"
expect "part 1 (CHIP) is Missing: CHIP, silent (R7: a flash alone breaks CHIP parts)" "Missing: CHIP" "$(rhs_stored 1)"
expect "part 2 (JU6, ju6.plg on the card) is in the Model list at its own row" "JU6" "$(list_of 2 | tr ',' '\n' | sed -n "$(( $(cursor_of 2) + 1 ))p")"
expect "part 3 (PLK, pluck.plg 1.0 on the card) is in the Model list at its own row" "PLK" "$(list_of 3 | tr ',' '\n' | sed -n "$(( $(cursor_of 3) + 1 ))p")"
expect "boot and the Synth screens did not rewrite the project or the patches" "$S0" "$(sum "$sd"/Workspace/project.mt "$sd"/Workspace/?.patch)"
# R1 and R2 (hide-and-legacy-engines.md): the Missing CHIP part highlights no row of the Model list. A change of the list (encoder, arrow keys,
# F1) moves it onto a row like on any part and the engine of that row is picked only after the rest of the list (kModelRestMs, 1 s), so the
# part is never trapped on the Missing engine (chip-missing-fix) and a stray click does not change it at once. Every key runs on a copy of the
# card, the card of the 1.2.0 project itself stays byte identical for the checks after this loop.
bar_of() { grep '^listbar' "$WORK/poke.out" | sed -n "${1}p" | sed 's/^listbar \([0-9]*\) pos.*/\1/'; }
popup_of() { grep '^popup' "$WORK/poke.out" | sed -n "${1}p"; }
runh "$H13" "$sd" "$SYNTH; poke picker"
expect "R2: the Missing CHIP part highlights no row of the Model list" "0" "$(bar_of 1)"
for keys in "enc -1" "enc 1" "tap Up" "tap Down" "tap F1"; do
    rm -rf "$sd.k"; cp -R "$sd" "$sd.k"
    runh "$H13" "$sd.k" "$SYNTH; poke popup; $keys; wait 300; poke picker; poke popup; wait 2500; poke picker; poke popup"
    expect "R1: $keys on the Missing CHIP part moves the list onto a row and does not change the engine before the rest (stored 6)" "1 6 popup none" "$(bar_of 1) $(part_line 1 0 | sed 's/.* stored \([0-9]*\) engine.*/\1/') $(popup_of 2)"
    expect "R1: $keys: after the rest the part is on the engine of that row (not stored 6, Init patch loaded)" "1 popup 'Init patch loaded'" "$([ "$(part_line 2 0 | sed 's/.* stored \([0-9]*\) engine.*/\1/')" != 6 ] && echo 1 || echo 0) $(popup_of 3)"
done
rm -rf "$sd.k"
# the user installs the 1.3.0 chip.plg: the part is back
cp "$SYN13/chip.plg" "$SYN13/chip.plgi" "$sd/Plugins/Synth/"
runh "$H13" "$sd" "$SYNTH; poke picker; poke rhs"
case "$(rhs_stored 1)" in Missing*) fail "with chip.plg added the CHIP part is no longer Missing" "$(grep '^rhs' "$WORK/poke.out" | head -1)" ;; *) pass "with chip.plg added the CHIP part is no longer Missing (engine $(grep '^rhs' "$WORK/poke.out" | head -1 | sed 's/^rhs engine\[\([^]]*\)\].*/\1/'))" ;; esac
expect "and the project is still the one of 1.2.0" "$S0" "$(sum "$sd"/Workspace/project.mt "$sd"/Workspace/?.patch)"

echo "### scenario A3: the header gate of the loader, 1.2.0 plugin files with one header field changed (new host and old host)"
gate() {   # gate HOST NAME SRC FIELD VALUE EXPECT(1 listed, 0 refused)
    local host="$1" name="$2" src="$3" field="$4" val="$5" want="$6" sd="$WORK/g_$2"
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Synth"
    py mut "$src" "$sd/Plugins/Synth/ju6.plg" "$field" "$val"
    "$host" "$sd" "wait 200" >/dev/null 2>&1
    echo "$(syn_listed "$sd")"
}
GATE_ROWS="control:magic:0x31474C50:1
abi_minor_0:abi_minor:0:1
abi_minor_9:abi_minor:9:1
abi_major_2:abi_major:2:0
abi_major_0:abi_major:0:0
min_host_1:min_host:1:1
min_host_2:min_host:2:0
cpu_cm7:cpu:2:0
kind_master:kind:1:0
kind_ext:kind:6:0
flag_s16:flags:1:0
magic:magic:0:0
header_size_63:header_size:63:0
header_size_2000:header_size:2000:0
image_65537:image_size:65540:0
bss_65537:bss_size:65537:0
rebase_16385:rebase_count:16385:0
version_9:version:0x090000:1
payload_flip:payload:200:1
crc_wrong:crc32:1:1"
for hostname in new old; do
    if [ "$hostname" = new ]; then host="$H13"; else host="$H12"; fi
    bad=0; got_all=""
    for row in $GATE_ROWS; do
        IFS=: read -r name field val want <<<"$row"
        got="$(gate "$host" "${hostname}_$name" "$SYN12/ju6.plg" "$field" "$val" "$want")"
        got_all="$got_all $name=$got"
        [ "$got" = "$want" ] || { bad=$((bad+1)); note "$hostname host: $name ($field=$val) expected listed=$want got $got"; }
    done
    expect "$hostname firmware: a 1.2.0 plugin file is accepted, any header outside the rules is refused (20 header variants)" "0" "$bad"
    [ "$hostname" = new ] && GATE_NEW="$got_all" || GATE_OLD="$got_all"
done
expect "both firmwares answer every header variant the same" "$GATE_OLD" "$GATE_NEW"
note "abi_minor (0 and 9), plugin version and the CRC are not checked by the scan (the CRC is checked at install: the insert test below, device for engines)"

echo "### scenario A4: the 1.2.0 master inserts on 1.3.0 (Tape and Lo-Fi in the slots, a corrupt Tape, Glue missing)"
if [ "$TWINS" = 1 ]; then
    TAPE=9c9b2979; LOFI=e5b380f1; GLUE=9f6aa8f0
    for hostname in new old; do
        if [ "$hostname" = new ]; then host="$H13"; else host="$H12"; fi
        sd="$WORK/a4_$hostname"; card "$sd" "$SYN12" "$FX12"; prime "$host" "$sd"
        sidecar "$sd/Workspace/plugins.txt" $TAPE Tape $LOFI Lo-Fi; cp "$sd/Workspace/plugins.txt" "$WORK/a4.side"
        runh "$host" "$sd" "wait 6000; poke inserts"
        expect "$hostname firmware: Tape in insert 1 and Lo-Fi in insert 2 load" "loaded loaded" "$(loaded "$(st_of 1)") $(loaded "$(st_of 2)")"
        expect "$hostname firmware: the sidecar is untouched by the load" "$(shasum "$WORK/a4.side" | cut -c1-12)" "$(shasum "$sd/Workspace/plugins.txt" | cut -c1-12)"
        # a corrupt Tape (one image byte flipped, header and CRC field as shipped)
        py mut "$FX12/tape.plg" "$sd/Plugins/Master/tape.plg" payload 300
        runh "$host" "$sd" "wait 6000; poke inserts"
        expect "$hostname firmware: a corrupt Tape is Failed with the CRC error (6), nothing runs from it" "failed 6" "$(st_of 1) $(ins_err "$sd" 1)"
        expect "$hostname firmware: and the other insert still loads" "loaded" "$(loaded "$(st_of 2)")"
        # a 1.3.0 project with Glue in insert 2 and no glue.plg on the card: Missing, the values stay in the file
        sd="$WORK/a4m_$hostname"; card "$sd" "$SYN12" "$FX12"; prime "$host" "$sd"
        sidecar "$sd/Workspace/plugins.txt" $TAPE Tape $GLUE Glue; cp "$sd/Workspace/plugins.txt" "$WORK/a4m.side"
        runh "$host" "$sd" "wait 12000; poke inserts"
        expect "$hostname firmware: Glue in the project, no glue.plg: the slot is Missing, the other insert loads" "missing loaded" "$(st_of 2) $(loaded "$(st_of 1)")"
        expect "$hostname firmware: the sidecar with the Glue values is untouched" "$(shasum "$WORK/a4m.side" | cut -c1-12)" "$(shasum "$sd/Workspace/plugins.txt" | cut -c1-12)"
    done
else
    echo "     SKIP: no insert twin sources (effects-os checkout at the pinned tag)"
fi
py pairs "$FX12" >"$WORK/pairs12.txt"
note "$(grep '^sizes' "$WORK/pairs12.txt" | cut -c1-200)"
expect "every pair of the 1.2.0 inserts fits the one 8 KB slot of 1.3.0 (insert 2 ran from the flash in 1.2.0)" "nofit -" "$(grep '^nofit' "$WORK/pairs12.txt")"

echo "### scenario A5: the host unit tests that cover the old formats (the bank index of 1.2.0, the sidecar)"
for t in plug_bank_test plug_sidecar_test; do
    if c++ -std=c++20 -O1 -I "$ROOT/synth-open" -I "$ROOT/synth-open/tools/plug_stub" "$ROOT/synth-open/tools/$t.cpp" -o "$WORK/$t" >"$WORK/$t.log" 2>&1; then
        "$WORK/$t" >"$WORK/$t.out" 2>&1
        expect "$t passes ($(grep -E '^PASSED' "$WORK/$t.out" | head -1))" "0" "$(grep -c '^FAIL' "$WORK/$t.out")"
    else fail "$t builds" "$(head -3 "$WORK/$t.log")"; fi
done
expect "plug_bank_test has the cases for a 1.2.0 installed.txt (30 counters) and for the 1.2.0 reader of a 32 counter line" "2" "$(grep -c -E '1\.2\.0 index \(30 counters\)|the 1\.2\.0 parser \(30 only\)' "$ROOT/synth-open/tools/plug_bank_test.cpp")"
grep -q 'a missing plugin keeps its values' "$ROOT/synth-open/tools/plug_sidecar_test.cpp" && pass "plug_sidecar_test has the case that a plugin missing from the card keeps its values through a save" || fail "plug_sidecar_test has the missing plugin case"

# ------------------------------------------------------------------------------------------------------------------ B
echo "### scenario B1: 1.2.0 firmware, card with the whole 1.3.0 set (chip.plg duplicates the built-in CHIP)"
N13="$(nfiles "$SYN13")"
sd="$WORK/b1"; card "$sd" "$SYN13" "$FX13"; prime "$H12" "$sd"
runh "$H12" "$sd" "$SYNTH; poke picker"
expect "no crash, the old firmware lists every file of the 1.3.0 set it can" "$N13 $((N13 - 1))" "$(syn_files "$sd") $(syn_listed "$sd")"
case "$(syn_engine "$sd" chip.plg)" in "chip.plg CHIP stored 6 not listed err 6") pass "chip.plg is refused as a duplicate of the built-in number 6 (err 6, not listed)" ;; *) fail "chip.plg on 1.2.0" "$(syn_engine "$sd" chip.plg)" ;; esac
expect "CHIP is in the Model list exactly once (the built-in one)" "1" "$(list_of 1 | tr ',' '\n' | grep -c '^CHIP$')"
expect "the other engines of 1.3.0 are listed, PLK 1.1 and PHZ included" "$((5 + 1 + N13 - 1))" "$(list_of 1 | tr ',' '\n' | wc -l | tr -d ' ')"
expect "the 1.3.0 inserts, Glue included, pass the old scan" "listed $(nfiles "$FX13"), rejected 0" "$(scan_line "$sd")"
expect "no engine line has another error than the CHIP duplicate" "1" "$(grep ' stored ' "$sd/Plugins/synth.txt" | grep -vc ' err 0')"
note "synth.txt is the only place that says chip.plg was ignored, nothing on the screen"

echo "### scenario B2: a project that 1.3.0 wrote (parts on CHIP, ACD, VAP) opened by 1.2.0, then 1.3.0 again"
sd="$WORK/b2"; card "$sd" "$SYN13" "$FX13"; prime "$H13" "$sd"
"$H13" "$sd" "$SYNTH; tap Down 2; wait 2500; tap Pattern; wait 600" >/dev/null 2>&1     # the pick of CHIP (a plugin on 1.3.0) writes its patch
py seed "$sd/Workspace/project.mt" 6 0 2
S1="$(sum "$sd"/Workspace/project.mt "$sd"/Workspace/?.patch)"
expect "the 1.3.x project says its own version in its header ($VER13)" "$VER13" "$(py stamp "$sd/Workspace/project.mt")"
runh "$H12" "$sd" "$SYNTH; poke picker; instr 65; tap Pattern; wait 300; tap Params; wait 1500; poke picker; instr 66; tap Pattern; wait 300; tap Params; wait 1500; poke picker"
expect "1.2.0 reads the project: part 1 is on the built-in CHIP (stored 6)" "part 0 stored 6 engine CHIP" "$(part_line 1 0)"
expect "the CHIP row is the part's row in the list (it is not Missing, cursor on CHIP)" "CHIP" "$(list_of 1 | tr ',' '\n' | sed -n "$(( $(cursor_of 1) + 1 ))p")"
expect "parts 2 and 3 (ACD, VAP) are the same engines" "ACD VAP" "$(part_engine 2 1) $(part_engine 3 2)"
expect "opening it and showing the Synth screens rewrote neither the project nor the patches" "$S1" "$(sum "$sd"/Workspace/project.mt "$sd"/Workspace/?.patch)"
runh "$H13" "$sd" "$SYNTH; poke picker"
expect "and 1.3.0 reads it again as before" "part 0 stored 6 engine CHIP" "$(part_line 1 0)"
# the two firmwares write the same project structure: a default project of each differs in the version stamp and the CRC only
sd12="$WORK/b2d12"; sd13="$WORK/b2d13"; rm -rf "$sd12" "$sd13"; mkdir -p "$sd12" "$sd13"; prime "$H12" "$sd12"; prime "$H13" "$sd13"
py projdiff "$sd12/Workspace/project.mt" "$sd13/Workspace/project.mt" >"$WORK/pd.txt"
expect "a default project of 1.2.0 and of 1.3.x: same size, only the firmware stamp (byte 5, the minor, and byte 6 when the patch number is not 0) and the CRC (last 4 bytes) differ" "size 2324 2324 diff $PROJDIFF" "$(cat "$WORK/pd.txt")"
expect "the Synth stored numbers sit at the same offsets (1.2.0 default project: WTFM, ACD, VAP = 3 0 2)" "3 0 2" "$(py stored "$sd12/Workspace/project.mt")"
expect "the project file loader (fileManagerProject.cpp) is the same file in both firmwares: no gate on the firmware stamp" "same" "$(cmp -s "$B12/core/FileManager/fileManagerProject.cpp" "$ROOT/core/FileManager/fileManagerProject.cpp" && echo same || (diff <(tr -d '\r' <"$B12/core/FileManager/fileManagerProject.cpp") <(tr -d '\r' <"$ROOT/core/FileManager/fileManagerProject.cpp") >/dev/null && echo same || echo different))"

echo "### scenario B3: a patch that 1.3.0 wrote, through the patch codec of 1.2.0 (the load and the save path)"
cat >"$WORK/rt.cpp" <<'EOF'
#include "open_patchdata.h"
#include <cstdio>
#include <vector>
using namespace polysynth::open;
int main(int argc, char** argv)
{
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 4;
    std::vector<uint8_t> in(8192);
    size_t n = fread(in.data(), 1, in.size(), f);
    fclose(f);
    static PatchData* pd = new PatchData();
    if (!decodePatch(in.data(), n, *pd)) { printf("decode failed\n"); return 2; }
    std::vector<uint8_t> out(8192);
    size_t m = encodePatch(*pd, out.data(), out.size());
    if (!m) { printf("encode failed\n"); return 3; }
    f = fopen(argv[2], "wb");
    fwrite(out.data(), 1, m, f);
    fclose(f);
    printf("in %zu out %zu raw %d\n", n, m, (int)pd->extra.len);
    return 0;
}
EOF
build_rt() {   # build_rt TREE OUT
    c++ -std=c++20 -O1 -I "$1/synth-open" -I "$1/synth-open/ot" -I "$1/synth/include" -I "$1/core/synth" -I "$1/util/include" "$WORK/rt.cpp" "$1/synth-open/open_patchio.cpp" -o "$2" >"$WORK/rt.log" 2>&1
}
if build_rt "$B12" "$WORK/rt12" && build_rt "$ROOT" "$WORK/rt13"; then
    sd="$WORK/b3"; rm -rf "$sd"; mkdir -p "$sd"; prime "$H13" "$sd"
    P="$sd/Workspace/1.patch"
    expect "a new patch of 1.3.0 is written with field 100 (macro depth mode Scale is the default of a new patch): bytes a0 06 01 at the end" "a00601" "$(tail -c 3 "$P" | xxd -p)"
    # the codec contract of both releases is macro_mode_compat_check.sh: a 1.2 patch loads identically on 1.3, a 1.3 patch round-trips through the 1.2 codec
    if mmc="$("$ROOT/synth-open/tools/macro_mode_compat_check.sh" 2>&1)"; then
        pass "the macro depth mode codec contract holds in both directions (macro_mode_compat_check.sh, $(printf '%s\n' "$mmc" | grep -c '^PASS') checks)"
    else
        fail "macro_mode_compat_check.sh" "$(printf '%s\n' "$mmc" | grep -A3 '^FAIL' | head -12)"
    fi
    "$WORK/rt12" "$P" "$WORK/rt12.out" >/dev/null && "$WORK/rt13" "$P" "$WORK/rt13.out" >/dev/null
    expect "1.2.0 loads that patch and saves it back byte for byte (the field is kept as an unknown field)" "$(shasum "$P" | cut -c1-12)" "$(shasum "$WORK/rt12.out" | cut -c1-12)"
    expect "1.3.0 round trips it too" "$(shasum "$P" | cut -c1-12)" "$(shasum "$WORK/rt13.out" | cut -c1-12)"
    for f in "$ROOT"/synth-open/tools/fixtures/ot-test-07/*.patch; do
        "$WORK/rt12" "$f" "$WORK/o12.patch" >/dev/null; "$WORK/rt13" "$f" "$WORK/o13.patch" >/dev/null
        cmp -s "$f" "$WORK/o12.patch" && cmp -s "$f" "$WORK/o13.patch" || fail "OpenTracker fixture $(basename "$f") round trip" "$(sum "$f" "$WORK/o12.patch" "$WORK/o13.patch")"
    done
    pass "the 4 OpenTracker fixture patches round trip byte for byte in both codecs"
    # a patch of 1.3.0 that 1.2.0 would take for Add: field 100 is not read by 1.2.0 (macro depth follows the Add rule, the sound differs)
    # F1 is a known accepted finding (user decision 2026-11-12): the codec of 1.3.0 always writes field 100 for Scale, 1.2.x plays Scale as Add, data kept
    if [ "$(grep -c 'kMacroModeField' "$B12/synth-open/open_patchdata.h")" = 0 ]; then
        known "F1: 1.2.0 does not read the macro depth mode (field 100): a 1.3.0 Scale patch with a macro amount plays as Add on 1.2.x (accepted, the data is kept)"
    else
        pass "F1: this 1.2.0 reference reads field 100, so Scale patches sound the same on both releases"
    fi
    # the unknown fields 1.2.0 keeps are limited to kRawMax 768 bytes: the 3 bytes of field 100 are 3 more
    mkdir -p "$WORK/edge"
    head -c $(( $(wc -c <"$P") - 3 )) "$P" >"$WORK/base_add.patch"
    "$WORK/rt12" "$WORK/base_add.patch" "$WORK/x.patch" >"$WORK/raw.txt"; RAW="$(sed 's/.*raw //' "$WORK/raw.txt")"
    py edgepatch "$WORK/base_add.patch" "$WORK/edge" "$RAW" >/dev/null
    r12=0; r13=0
    "$WORK/rt12" "$WORK/edge/edge_over_scale.patch" "$WORK/x.patch" >/dev/null 2>&1 && r12=1
    "$WORK/rt13" "$WORK/edge/edge_over_scale.patch" "$WORK/x.patch" >/dev/null 2>&1 && r13=1
    "$WORK/rt12" "$WORK/edge/edge_fits_scale.patch" "$WORK/x.patch" >/dev/null 2>&1 && f12=1 || f12=0
    expect "F2 (edge): 1.2.0 decodes a 1.3.0 patch whose unknown-field block is over 765 bytes (kRawMax 768 less the 3 of field 100; real patches: 8 to 52 bytes)" "1 1 1" "$r12 $r13 $f12"
else
    fail "build the patch codec test (open_patchio.cpp) for both trees" "$(head -5 "$WORK/rt.log")"
fi

echo "### scenario B4: names, synth order file and sidecar written by 1.3.0, on 1.2.0"
sd="$WORK/b4"; card "$sd" "$SYN13" "$FX13"; prime "$H13" "$sd"
runh "$H13" "$sd" "$SYNTH; poke picker"          # 1.3.0 boot: writes names.txt and synth.txt
printf 'CHIP\r\n-JU6\r\nACD\r\n' >"$sd/Plugins/synth-order.txt"
cp "$sd/Plugins/names.txt" "$WORK/names13.txt"; cp "$sd/Plugins/synth-order.txt" "$WORK/order13.txt"
sidecar "$sd/Workspace/plugins.txt" 9f6aa8f0 Glue 9c9b2979 Tape; cp "$sd/Workspace/plugins.txt" "$WORK/b4.side"
grep -q '^6 CHIP' "$sd/Plugins/names.txt" && pass "1.3.0 remembers CHIP as number 6 in names.txt (the line 1.2.0 never wrote)" || fail "1.3.0 names.txt has 6 CHIP" "$(cat "$sd/Plugins/names.txt" | head -3)"
runh "$H12" "$sd" "$SYNTH; poke picker"
expect "1.2.0 shows the default engine list: synth-order.txt is not read (JU6 not hidden, CHIP not first)" "ACD,FAT,VAP,WTFM,PERC,CHIP,JU6" "$(list_of 1 | cut -d, -f1-7)"
expect "1.2.0 left synth-order.txt alone" "$(shasum "$WORK/order13.txt" | cut -c1-12)" "$(shasum "$sd/Plugins/synth-order.txt" | cut -c1-12)"
grep -q '^6 CHIP' "$sd/Plugins/names.txt" && pass "names.txt still holds 6 CHIP after 1.2.0 (it ignores the number)" || note "names.txt after 1.2.0: $(tr '\r\n' ' ' <"$sd/Plugins/names.txt" | cut -c1-60) (1.2.0 rewrote it without 6, 1.3.0 learns it again at boot)"
expect "the sidecar with a Glue slot (no glue-capable twin on this host) is not rewritten by the boot of 1.2.0" "$(shasum "$WORK/b4.side" | cut -c1-12)" "$(shasum "$sd/Workspace/plugins.txt" | cut -c1-12)"
note "the sidecar written back by a project save keeps a slot whose plugin is missing: plug_sidecar_test above (a missing plugin keeps its values), plug_host.cpp projectSave"
runh "$H13" "$sd" "$SYNTH; poke picker"
expect "1.3.0 on the same card afterwards: the order file still applies" "CHIP" "$(list_of 1 | cut -d, -f1)"

# ------------------------------------------------------------------------------------------------------------------ C
echo "### scenario C1: a half upgraded card on 1.3.0 (1.2.0 engines + chip.plg of 1.3.0 + PLK 1.0, Glue with the 1.2.0 inserts)"
sd="$WORK/c1"; mk12proj "$sd"; card "$sd" "$SYN12" "$FX12"
cp "$SYN13/chip.plg" "$SYN13/chip.plgi" "$sd/Plugins/Synth/"; cp "$FX13/glue.plg" "$sd/Plugins/Master/"
S2="$(sum "$sd"/Workspace/project.mt "$sd"/Workspace/?.patch)"
runh "$H13" "$sd" "$SYNTH; poke picker; poke rhs"
expect "everything is listed, CHIP included" "$((N12 + 1)) $((N12 + 1))" "$(syn_files "$sd") $(syn_listed "$sd")"
case "$(rhs_stored 1)" in Missing*) fail "the CHIP part is not Missing" "$(grep '^rhs' "$WORK/poke.out" | head -1)" ;; *) pass "the CHIP part is not Missing" ;; esac
expect "and nothing of the project or the patches was rewritten" "$S2" "$(sum "$sd"/Workspace/project.mt "$sd"/Workspace/?.patch)"
expect "the inserts, Glue included, are all listed" "listed $(( $(nfiles "$FX12") + 1 )), rejected 0" "$(scan_line "$sd")"
HAS_PHZ="$(ls "$SYN12"/phz.plg 2>/dev/null)"
if [ -n "$HAS_PHZ" ]; then
    echo "### scenario C2: two versions of one plugin on the card (PLK 1.0 and PLK 1.1, one uid)"
    sd="$WORK/c2"; card "$sd" "$SYN12" "$FX12"; cp "$SYN13/pluck.plg" "$sd/Plugins/Synth/pluck_new.plg"; prime "$H13" "$sd"
    "$H13" "$sd" "wait 200" >/dev/null 2>&1
    expect "one PLK entry, not two" "1" "$(grep -c ' PLK stored 18' "$sd/Plugins/synth.txt")"
    expect "and it is the newer file (1.1.0, pluck_new.plg)" "pluck_new.plg" "$(grep ' PLK stored 18' "$sd/Plugins/synth.txt" | cut -d' ' -f1)"
    sd="$WORK/c2o"; card "$sd" "$SYN12" "$FX12"; cp "$SYN13/pluck.plg" "$sd/Plugins/Synth/zz_new.plg"; cp "$SYN12/pluck.plg" "$sd/Plugins/Synth/zz_old.plg"; rm "$sd/Plugins/Synth/pluck.plg"
    "$H13" "$sd" "wait 200" >/dev/null 2>&1
    expect "the newer file wins whatever the order of the names" "zz_new.plg" "$(grep ' PLK stored 18' "$sd/Plugins/synth.txt" | cut -d' ' -f1)"
    sd="$WORK/c2b"; card "$sd" "$SYN12" "$FX12"; cp "$SYN13/pluck.plg" "$sd/Plugins/Synth/pluck_new.plg"; prime "$H12" "$sd"; "$H12" "$sd" "wait 200" >/dev/null 2>&1
    expect "the same on 1.2.0: one PLK entry, the newer file" "1 pluck_new.plg" "$(grep -c ' PLK stored 18' "$sd/Plugins/synth.txt") $(grep ' PLK stored 18' "$sd/Plugins/synth.txt" | cut -d' ' -f1)"
else
    echo "### scenario C2: SKIP (no PLK 1.0 and PHZ in the 1.2.0 set, the private engines repository is missing)"
fi
echo "### scenario C3: 1.3.0 inserts that do not fit beside each other (Glue is new, the 1.2.0 projects cannot have it)"
py pairs "$FX13" >"$WORK/pairs13.txt"
note "$(grep '^sizes' "$WORK/pairs13.txt" | cut -c1-200)"
expect "F3: every pair of the 1.3.0 inserts fits the shared 8 KB slot (else the second one is NoRoom, audio passes)" "nofit -" "$(grep '^nofit' "$WORK/pairs13.txt")"

# ------------------------------------------------------------------------------------------------------------------ F5
# A .plg never runs on the Mac, so an OLDER engine file cannot be run here. What decides F5 is the firmware's save path (OpenPatch::importData
# and exportData in open_cold.cpp), which is the same code for a built in and a plugin engine: it gets the parameter count from the engine
# definition. The smoke test (host_smoke_test.cpp, section "F5") links that real code with the definition of a real engine (N parameters,
# the "engine reports fewer params" stub) and saves a file with N+2 parameters and a macro target on the last one. Its F5 lines must all be OK.
echo "### F5: a patch with more parameters than the engine reports, saved by the firmware (host test of the real save path)"
SMOKE_ARCH="${SMOKE_ARCH:-arm64}" sh "$ROOT/synth-open/tools/run_smoke.sh" "ACD FAT VAP WTFM PERC" >"$WORK/smoke_f5.log" 2>&1
f5ok="$(grep -c '^  F5: .* OK$' "$WORK/smoke_f5.log" || true)"; f5bad="$(grep -c '^  F5: .* FAIL$' "$WORK/smoke_f5.log" || true)"
grep '^  F5:' "$WORK/smoke_f5.log" | sed 's/^ */      /'
f5got="ok=$f5ok fail=$f5bad"; { [ "$f5bad" -eq 0 ] && [ "$f5ok" -ge 7 ]; } && f5got="fine"
expect "F5: an older engine saving a patch with more parameters than it knows keeps the extra values and their macro targets byte for byte (bounded: 8 values, 8 targets, the rest logged)" "fine" "$f5got"
[ "$f5got" = fine ] || tail -5 "$WORK/smoke_f5.log" | sed 's/^/      smoke: /'

echo
echo "     the hosts: new = $(git -C "$ROOT" rev-parse --short HEAD), old = $FW12_REF ($(git -C "$ROOT" rev-parse --short "$FW12_REF"))"
[ "$knowns" -gt 0 ] && echo "known accepted findings: ${knowns} (F1)"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks checks)"; exit 1
