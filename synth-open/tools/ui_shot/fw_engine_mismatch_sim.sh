#!/bin/bash
# fw_engine_mismatch_sim.sh [PNG_DIR]
#
# Firmware and engine plugins from different releases, on the Mac host simulator. A pair simulator, generic over the pair: the two releases are
# the tree this script sits in (the "new" firmware, its ui_shot host the emulator bridge build, its version from version.mk) and OLD_REF (any older
# release of this repository, the "old" firmware: built from a git archive into build/fw-eng-mismatch with the one extra "poke picker" action
# of fw_mismatch_probe12.py, its version from the version.mk of that ref). The "old" plugins are the release files the old ref's
# engines/external.lock.json pins, rebuilt from the module repository tags of that lock and checked against its sha256, so they are the shipped
# bytes. The "new" plugins are the set of PLG_DIR. Nothing is hard coded to a release pair: what the old release can do is READ from its tree
# and its lock (does the old lock list chip.plg: CHIP is a plugin there, does the old tree read the macro depth field 100, does it read
# synth-order.txt, has it the shared insert slot, which plugin files differ between the two locks) and the expectations follow from that.
#
#   A  new firmware + old plugins           (engines and /Plugins/Master inserts, a project of the old release)
#   B  old firmware + new plugins           (a plugin the old firmware has built in, a project the new one wrote, .patch field 100)
#   C  a card with plugins of both releases (half upgraded, two versions of one plugin, the other order of installing)
#
# What the host does and does not prove: the scan of /Plugins/Synth and /Plugins/Master, the header and CRC checks, the engine list, the
# Missing state, the insert slots, the project and patch files and the codecs are the real firmware code of each tree. The new host is the
# BRIDGE=1 build (bridge_env.sh, emu parity T10): every synth and insert .plg a scenario installs runs its real bytes under Unicorn through the
# image's loader, start() and ABI checks. The old host is a plain build of the old tree (a release before the bridge): there a .plg never runs,
# its Tape, Lo-Fi and Dirt slots and its CHIP are the native twins of that tree. A runtime ABI mismatch on the old side (a plugin that calls a
# service the host does not have) is proven by comparing the ABI structs and the service table of both trees. Nothing touches a device. Every
# scenario has its own card folder.
#
# The release gate (synth-open/tools/release_gate.sh) runs this script against the previous release tag and fails the release on any RED
# check whose F<n> tag is not in synth-open/tools/ui_shot/mismatch_known_findings.txt. Name a new finding check "F<n>: ..." so it can be listed.
#
# The ABI checks (scenario 0, A3) are additive-only rules plus a pin: ui_shot/abi_contract.txt is the ABI of the tree under test (1.4: host level 2, ABI minor 4), see release_gate.sh.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)" (exit 1). A RED check is a finding about the release pair, see
# the project notes for what each one means. A KNOWN line is a finding the project accepted (F1: a Scale patch with a macro amount plays as
# Add on a firmware that does not read field 100, 1.2.x, and B5: the published 1.3.0 traps a Missing part, fixed in 1.3.1): it is listed and never makes
# the result RED. B5 is KNOWN only on an old side without the fix, with the 1.3.1 fix it is a real check that PASSes.
#
#   synth-open/tools/ui_shot/fw_engine_mismatch_sim.sh [PNG_DIR]
#   PLG_DIR        the new plugin set, flat folder of .plg and .plgi (default build/release-1.3.0-rc/modules)
#   OLD_REF        git ref of the old firmware in this repository (FW12_REF is the old name of the variable). Default: the latest published release
#                  tag tracker-ff-v<A.B.C> by version order (what the gate does), else release-1.2.0-pub6, else release-1.2.0
#   TOOLCHAIN      arm-none-eabi- prefix of Arm GNU 12.3.rel1 (default agt/12.3.rel1/bin/arm-none-eabi-), builds the old plugins
#   PLGO_DIR       a flat folder of the old .plg and .plgi instead of building them (still checked against the old lock). PLG12_DIR is the old name
#   ENGINES_OS_REPO EFFECTS_OS_REPO ENGINES_REPO   module repositories (default ../poly-engines-os, poly-effects-os, poly-engines), the
#                  private one is optional: without it PHZ and PLK of the old set are not on the old card and their checks say SKIP
#   NCPU           make -j for the hosts (default: the cores of the machine)
#   UNICORN_DIR BRIDGE_AXF   the new host's bridge build (bridge_env.sh: default the pinned wheel unpacked into build/unicorn and this tree's
#                  release axf, made when missing)
#   REBUILD=1      throw the cache of build/fw-eng-mismatch away first. The plugin set cache and the old host are keyed on the old ref's commit,
#                  so a run against another ref rebuilds them by itself
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
NCPU="${NCPU:-$(sh "$HERE/../jobs.sh" n)}"

PLGN="${PLG_DIR:-$ROOT/build/release-1.3.0-rc/modules}"
[ -f "$PLGN/ju6.plg" ] || { echo "fw_engine_mismatch_sim.sh: no new plugins (set PLG_DIR to a folder with ju6.plg ...)" >&2; exit 2; }
OLD_REF="${OLD_REF:-${FW12_REF:-}}"
if [ -z "$OLD_REF" ]; then
    OLD_REF="$(git -C "$ROOT" tag --list 'tracker-ff-v[0-9]*' --sort=-v:refname | grep -E '^tracker-ff-v[0-9]+\.[0-9]+\.[0-9]+$' | head -1)"
    [ -n "$OLD_REF" ] || for r in release-1.2.0-pub6 release-1.2.0; do git -C "$ROOT" rev-parse --verify -q "$r^{commit}" >/dev/null && { OLD_REF="$r"; break; }; done
fi
[ -n "$OLD_REF" ] || { echo "fw_engine_mismatch_sim.sh: no old ref (set OLD_REF)" >&2; exit 2; }
OLD_COMMIT="$(git -C "$ROOT" rev-parse --verify -q "$OLD_REF^{commit}")" || { echo "fw_engine_mismatch_sim.sh: OLD_REF $OLD_REF is not a commit here" >&2; exit 2; }
PLGO_DIR="${PLGO_DIR:-${PLG12_DIR:-}}"
ver_of() {   # ver_of < version.mk -> the A.B.C of SYNTH_OPEN_VER
    sed -n 's/^SYNTH_OPEN_VER[[:space:]]*:\{0,1\}=[[:space:]]*//p' | head -1 | sed 's/-.*//'
}
NV="$(ver_of <"$ROOT/version.mk")"                                   # the version of the new tree, the stamp its firmware writes into a project
OV="$(git -C "$ROOT" show "$OLD_COMMIT:version.mk" 2>/dev/null | ver_of)"
[ -n "$NV" ] && [ -n "$OV" ] || { echo "fw_engine_mismatch_sim.sh: no SYNTH_OPEN_VER in version.mk of the new tree ($NV) or of $OLD_REF ($OV)" >&2; exit 2; }
[ "$NV" != "$OV" ] || echo "     note: the old ref and the new tree both say $NV"
TC="${TOOLCHAIN:-$ROOT/agt/12.3.rel1/bin/arm-none-eabi-}"
EOS="${ENGINES_OS_REPO:-$ROOT/../poly-engines-os}"; FOS="${EFFECTS_OS_REPO:-$ROOT/../poly-effects-os}"; PEN="${ENGINES_REPO:-$ROOT/../poly-engines}"
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
        # a struct may GROW at its tail (the new fields are read only when struct_size covers them, PLUG_HAS): the old body must be an exact prefix of the new one
        n2 = s2.get(k, '')
        print('same' if s1[k] == n2 else ('grown' if n2.startswith(s1[k] + ' ') else 'DIFF'), 'struct', k)
    for k in sorted(e1):
        print('same' if e1[k] == e2.get(k) else 'DIFF', 'enum', k)
    for k in sorted(set(e2) - set(e1)):
        print('added', 'enum', k, e2[k])
    for k in sorted(set(s2) - set(s1)):
        print('added', 'struct', k)
elif cmd == 'contract':                # contract H -> the ABI contract of a header: every PLUG_ define, the sha1 of every struct body (field order and types, comments stripped), every enum member
    d, st, en = abi_facts(sys.argv[2])
    for k in sorted(d):
        print('define', k, d[k])
    for k in sorted(st):
        print('struct', k, hashlib.sha1(st[k].encode()).hexdigest()[:12])
    for k in sorted(en):
        print('enum', k, en[k])
elif cmd == 'pairs':                   # pairs ROOM DIR... -> the insert pairs (slot 1, slot 2) that do not fit the shared slot of ROOM bytes, plug_index.h insertPlace
    sizes = {}
    for d in sys.argv[3:]:
        for f in sorted(os.listdir(d)):
            if f.endswith('.plg'):
                h = hdr(os.path.join(d, f))
                sizes[f[:-4]] = h['image_size'] + h['bss_size']
    room = int(sys.argv[2])
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
echo "### setup: the two hosts and the old plugin set ($OV = $OLD_REF, new = $NV)"
LOCKO="$WORK/lockold.json"; LOCKN="$WORK/locknew.json"
git -C "$ROOT" show "$OLD_COMMIT:engines/external.lock.json" >"$LOCKO" || { echo "no external.lock.json in $OLD_REF" >&2; exit 2; }
cp "$ROOT/engines/external.lock.json" "$LOCKN"
# what the old release is, from its lock and its tree (every expectation below that differs between release pairs uses one of these)
OLD_CHIP_PLG=0; grep -q '"chip.plg"' "$LOCKO" && OLD_CHIP_PLG=1      # CHIP is a plugin (chip.plg) in the old release, else it is built into the old firmware (stored number 6)
NEW_CHIP_PLG=0; grep -q '"chip.plg"' "$LOCKN" && NEW_CHIP_PLG=1
OLD_F100=0; git -C "$ROOT" show "$OLD_COMMIT:synth-open/open_patchdata.h" 2>/dev/null | grep -q 'kMacroModeField' && OLD_F100=1     # the old codec reads the macro depth mode, patch field 100
OLD_ORDER=0; git -C "$ROOT" cat-file -e "$OLD_COMMIT:synth-open/synth_order.h" 2>/dev/null && OLD_ORDER=1                         # the old firmware reads /Plugins/synth-order.txt
OLD_SHARED=0; git -C "$ROOT" show "$OLD_COMMIT:synth-open/plug_index.h" 2>/dev/null | grep -q 'insertPlace' && OLD_SHARED=1      # both inserts share the one 8 KB slot (insert 2 ran from the flash before)
[ "$NEW_CHIP_PLG" = 1 ] || { echo "fw_engine_mismatch_sim.sh: the lock of the new tree has no chip.plg: the CHIP scenarios need it" >&2; exit 2; }
echo "     old release facts: CHIP plugin $OLD_CHIP_PLG, reads field 100 $OLD_F100, reads synth-order.txt $OLD_ORDER, shared insert slot $OLD_SHARED"
echo "     new firmware: $NV $(git -C "$ROOT" describe --always --dirty 2>/dev/null || echo '?') ($(git -C "$ROOT" rev-parse --abbrev-ref HEAD))   old firmware: $OV $OLD_REF $(git -C "$ROOT" rev-parse --short "$OLD_COMMIT")"

# new host: the emulator bridge build, the plugins run their real bytes. The insert sources of the effects-os checkout at the pinned tag are
# still fetched: TWINS says whether the old host can have its insert twins (A4 runs on both hosts)
. "$HERE/bridge_env.sh"
bridge_env "$ROOT" || exit 2
BRIDGE=1   # wsum and seeded_init: the new host attaches every engine of the card
BNEW="$ROOT/build/ui-shot-bridge"
echo "     new host: the emulator bridge build (BRIDGE=1), the plugins run their real bytes"
HNEW="${UI_SHOT_BIN:-$BNEW/ui_shot}"   # UI_SHOT_BIN: a prebuilt BRIDGE=1 binary of this tree, no make (the old hosts still build in the cache)
mkdir -p "$BNEW/external"
TWINS=1
python3 -I "$ROOT/engines/external.py" source effects-os "$BNEW/external" plug/fx/ot_tape_plug.c plug/fx/ot_lofi_plug.c plug/fx/ot_bbd_plug.c plug/fx/ot_phaser_plug.c plug/fx_dirt_plug.cpp >"$WORK/twin13.log" 2>&1 || { TWINS=0; echo "     no effects-os twin sources for the new host: the insert checks are skipped"; }
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$NCPU" "${BRIDGE_ARGS[@]}" OUT="$BNEW" >"$BNEW.make.log" 2>&1 || { tail -30 "$BNEW.make.log"; exit 2; }

# old plugins: rebuilt from the tags of the old lock, then checked against the sha256 of that lock
SETO="$CACHE/seto"
SETO_KEY="$OLD_COMMIT ${PLGO_DIR:-built-from-lock}"      # the cache is the set of THIS old commit: a run against another ref (or another PLGO_DIR) rebuilds it
build_module() {   # build_module NAME REPO LOCKSET -> $CACHE/src-NAME-TAG/out (a git archive of the pinned tag)
    local name="$1" repo="$2" set="$3" tag dir
    tag="$(py tag "$LOCKO" "$set")"
    dir="$CACHE/src-$name-$tag"
    if [ ! -f "$dir/out/SHA256SUMS" ]; then
        [ -d "$repo/.git" ] || [ -f "$repo/.git" ] || return 1
        rm -rf "$dir"; mkdir -p "$dir"
        git -C "$repo" archive "$tag" | tar -x -C "$dir" || return 1
        make -C "$dir" TOOLCHAIN="$TC" OUT="$dir/out" PY=python3 >"$dir/make.log" 2>&1 || { tail -20 "$dir/make.log"; return 1; }
    fi
    echo "$dir"
}
if [ "$(cat "$SETO/.ok" 2>/dev/null)" != "$SETO_KEY" ]; then
    rm -rf "$SETO"; mkdir -p "$SETO/synth" "$SETO/master"
    if [ -n "${PLGO_DIR:-}" ]; then
        for f in "$PLGO_DIR"/*.plg "$PLGO_DIR"/*.plgi; do [ -f "$f" ] && cp "$f" "$SETO/_flat_$(basename "$f")"; done
    else
        [ -f "${TC}gcc" ] || { echo "fw_engine_mismatch_sim.sh: no ${TC}gcc (set TOOLCHAIN, or PLGO_DIR with the old plugins)" >&2; exit 2; }
        d="$(build_module engines-os "$EOS" engines-os)" || { echo "the old engines-os build failed" >&2; exit 2; }
        for f in "$d"/out/*.plg "$d"/out/*.plgi; do cp "$f" "$SETO/_flat_$(basename "$f")"; done
        d="$(build_module effects-os "$FOS" effects-os)" || { echo "the old effects-os build failed" >&2; exit 2; }
        for f in "$d"/out/*.plg; do cp "$f" "$SETO/_flat_$(basename "$f")"; done
        if d="$(build_module engines "$PEN" engines)"; then
            for f in phz pluck; do cp "$d/out/$f.plg" "$d/out/$f.plgi" "$SETO/" 2>/dev/null; for g in "$SETO/$f".plg*; do mv "$g" "$SETO/_flat_$(basename "$g")"; done; done
        else echo "     SKIP: no private engines repository, PHZ and PLK of the old release are not on the old card"; fi
    fi
    # Console and Tube (private effects repo) are not built here: the new files stand in for the old ones when they did not change, the lock check below proves it
    for f in console tube; do cp "$PLGN/$f.plg" "$SETO/_flat_$f.plg"; done
    for f in "$SETO"/_flat_*; do
        b="$(basename "$f")"; b="${b#_flat_}"; n="${b%.*}"
        case " $MASTER_NAMES " in *" $n "*) mv "$f" "$SETO/master/$b" ;; *) mv "$f" "$SETO/synth/$b" ;; esac
    done
    echo "$SETO_KEY" >"$SETO/.ok"
fi
SYNO="$SETO/synth"; FXO="$SETO/master"
# the new set split the same way
SETN="$CACHE/setn"; rm -rf "$SETN"; mkdir -p "$SETN/synth" "$SETN/master"
for f in "$PLGN"/*.plg "$PLGN"/*.plgi; do
    [ -f "$f" ] || continue
    b="$(basename "$f")"; n="${b%.*}"
    case "$n" in dirt|bbd|tape|lofi|phaser|console|tube|glue) [ "${b##*.}" = plg ] && cp "$f" "$SETN/master/" ;; *) cp "$f" "$SETN/synth/" ;; esac
done
SYNN="$SETN/synth"; FXN="$SETN/master"

# old host: git archive of the old commit, the probe (only for a tree without the poke actions), insert twins and the CHIP twin of the old lock's tags
BOLD="$CACHE/fwold"; HOLD="$BOLD/build/ui-shot/ui_shot"
PROBE_SUM="$(shasum "$HERE/fw_mismatch_probe12.py" | cut -c1-12) $OLD_COMMIT"
[ "$(cat "$BOLD/.probe" 2>/dev/null)" = "$PROBE_SUM" ] || rm -rf "$BOLD"
if [ ! -x "$HOLD" ]; then
    rm -rf "$BOLD"; mkdir -p "$BOLD"
    git -C "$ROOT" archive "$OLD_COMMIT" | tar -x -C "$BOLD" || { echo "git archive $OLD_REF failed" >&2; exit 2; }
    python3 -I "$HERE/fw_mismatch_probe12.py" "$BOLD" || exit 2
    mkdir -p "$BOLD/build/ui-shot/external"
    if d="$(build_module effects-os "$FOS" effects-os)"; then
        for f in ot_tape_plug.c ot_lofi_plug.c ot_bbd_plug.c ot_phaser_plug.c; do cp "$d/src/synth-open/plug/fx/$f" "$BOLD/build/ui-shot/external/"; done
        cp "$d/src/synth-open/plug/fx_dirt_plug.cpp" "$BOLD/build/ui-shot/external/"
    fi
    # an old tree whose host has the CHIP twin (CHIP is a plugin there) gets its own engine_chip.cpp, the one of the engines-os tag its lock pins
    OLD_CHIP_ARG=""
    if grep -q CHIP_PLUGIN_SRC "$BOLD/synth-open/tools/ui_shot/Makefile" && [ "$OLD_CHIP_PLG" = 1 ]; then
        d="$(build_module engines-os "$EOS" engines-os)" && [ -f "$d/src/synth-open/engine_chip.cpp" ] && OLD_CHIP_ARG="CHIP_PLUGIN_SRC=$d/src/synth-open/engine_chip.cpp"
        [ -n "$OLD_CHIP_ARG" ] || { echo "no engine_chip.cpp of the old engines-os tag for the old host" >&2; exit 2; }
    fi
    make -s -C "$BOLD/synth-open/tools/ui_shot" -j"$NCPU" OUT="$BOLD/build/ui-shot" $OLD_CHIP_ARG >"$BOLD/make.log" 2>&1 || { tail -30 "$BOLD/make.log"; exit 2; }
    echo "$PROBE_SUM" >"$BOLD/.probe"
fi
[ -x "$HOLD" ] || { echo "no old host binary $HOLD" >&2; exit 2; }

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
# the Workspace sum of A2 and C1. mkoldproj seeds parts 2 and 3 as JU6 and PLK in project.mt only, their 1.patch and 2.patch stay the patches the
# old host wrote for the part's first engine (ACD 1, VAP 4). The plain host never attaches JU6 or PLK, so nothing touches them. With BRIDGE=1 both
# engines really start and the firmware replaces a patch of another engine with the engine's Init patch (native firmware code, the unit does the
# same), so the sum leaves those two out and seeded_init checks they now carry the part's engine (JU6 0x81, PLK 0x8A). Not a 1.4.0 finding: a
# fixture of mismatched files that only an attaching engine sees. 0.patch is the same fixture once chip.plg is on the card: the part is seeded on CHIP in
# project.mt only and 0.patch stays the patch of the part's first engine (the default engine of part 1, WTFM: the 1.4.0 host does not write the CHIP patch at an engine change,
# checked on tracker-ff-v1.4.0), so the checks after chip.plg is on the card (wsum_att) leave it out too and seeded_init chip checks it got CHIP (8). The 1.4.0
# firmware does the same on its own project (gate on pristine 1.4.0 vs tag 1.4.0), not a 1.4.1 finding. While CHIP is Missing nothing may touch 0.patch (wsum)
wsum() { if [ "${BRIDGE:-}" = 1 ]; then sum "$1"/Workspace/project.mt "$1"/Workspace/0.patch; else sum "$1"/Workspace/project.mt "$1"/Workspace/?.patch; fi; }
patch_engine() { python3 -I -c 'import sys
b = open(sys.argv[1], "rb").read(); v = s = 0; i = 1
while b[0] == 8:
    v |= (b[i] & 127) << s; s += 7; i += 1
    if b[i - 1] < 128: break
print(v)' "$1" 2>/dev/null; }
wsum_att() { if [ "${BRIDGE:-}" = 1 ]; then sum "$1"/Workspace/project.mt; else wsum "$1"; fi; }   # chip.plg on the card: the CHIP part attaches and gets its Init patch too
seeded_init() {   # seeded_init DIR [chip]: chip = chip.plg is on the card, so part 1 (0.patch) got the CHIP Init patch (8) as well
    if [ "${BRIDGE:-}" = 1 ]; then
        if [ "${2:-}" = chip ]; then expect "BRIDGE: the seeded CHIP, JU6 and PLK parts (WTFM, ACD and VAP patch files under them) got their engine's Init patch at start" "8 129 138" "$(patch_engine "$1"/Workspace/0.patch) $(patch_engine "$1"/Workspace/1.patch) $(patch_engine "$1"/Workspace/2.patch)"
        else expect "BRIDGE: the seeded JU6 and PLK parts (ACD and VAP patch files under them) got their engine's Init patch at start" "129 138" "$(patch_engine "$1"/Workspace/1.patch) $(patch_engine "$1"/Workspace/2.patch)"; fi
    fi
    return 0
}
nfiles() { ls "$1"/*.plg 2>/dev/null | wc -l | tr -d ' '; }
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 10"; }
# state of insert slot N from the last "poke inserts" line (running or bypassed: the plugin is loaded, the project's mixer stage is not Inserts here)
st_of() { grep '^inserts' "$WORK/poke.out" | tail -1 | tr ' ' '\n' | grep "^$1=" | sed 's/^[0-9]=\([a-z]*\):.*/\1/'; }
loaded() { case "$1" in running|bypassed) echo loaded ;; *) echo "$1" ;; esac; }
mkoldproj() {   # mkoldproj DIR: a Workspace the old firmware wrote, part 1 on CHIP (built in, or chip.plg of the old card, with its CHIP patch), parts 2 and 3 stored as JU6 and PLK
    local sd="$1"; rm -rf "$sd"; mkdir -p "$sd"
    [ "$OLD_CHIP_PLG" = 1 ] && card "$sd" "$SYNO" "$FXO"          # CHIP is only in the list with its chip.plg on the card
    prime "$HOLD" "$sd"
    "$HOLD" "$sd" "$SYNTH; tap Down 2; wait $([ "$OLD_CHIP_PLG" = 1 ] && echo 2500 || echo 600); tap Pattern; wait 600" >/dev/null 2>&1
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
echo "### scenario 0: what differs between the two releases, $OV and $NV (ABI, plugin files)"
py abi "$BOLD/synth-open/plug/plug_abi.h" "$ROOT/synth-open/plug/plug_abi.h" >"$WORK/abi.txt"
cat "$WORK/abi.txt" | grep -v '^same' | sed 's/^/        /'
# PLUG_ABI_MINOR counts additive changes (plug_abi.h: new tail fields, optional entry points, host services): a release raises it when it adds something (1.3.0 raised it to 2 for
# PLUG_EXT_CHORD, 1.4.0 to 4). PLUG_HOST_LEVEL is the level of the host service table: a release may RAISE it (ABI 1.4: 1 -> 2, host->query answers PLUG_EXT_FILES),
# never lower it, because a plugin of the old release with min_host <= the old level has to stay loadable. Major and magic must stay. A struct may grow at its tail
# (plug_ext_synth got service and view, read only when struct_size covers them), a field may not move. The "old against new" checks below accept exactly that,
# and abi_contract.txt pins the NEW contract field for field, so an additive change nobody meant (host level 4, a wrong field offset) is still RED.
expect "ABI: major and magic are the same constants" "2" "$(grep '^same PLUG_' "$WORK/abi.txt" | grep -cE '^same PLUG_(MAGIC|ABI_MAJOR) ')"
expect "ABI: the minor version did not go down (additive changes only)" "ok" "$(awk '$2=="PLUG_ABI_MINOR"{print ($4+0 >= $3+0) ? "ok" : "lower " $3 " -> " $4}' "$WORK/abi.txt")"
expect "ABI: the host level did not go down (a plugin of $OV with min_host <= the old level stays loadable)" "ok" "$(awk '$2=="PLUG_HOST_LEVEL"{print ($4+0 >= $3+0) ? "ok" : "lower " $3 " -> " $4}' "$WORK/abi.txt")"
OLD_LVL="$(sed -n 's/^#define PLUG_HOST_LEVEL[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$BOLD/synth-open/plug/plug_abi.h" | head -1)"
NEW_LVL="$(sed -n 's/^#define PLUG_HOST_LEVEL[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$ROOT/synth-open/plug/plug_abi.h" | head -1)"
note "host level $OLD_LVL -> $NEW_LVL, grown structs (new fields only at the tail): $(grep '^grown struct' "$WORK/abi.txt" | sed 's/^grown struct //' | tr '\n' ' ')"
expect "ABI: every struct that crosses the boundary (header, manifest, param, desc, synth extension) keeps every field of $OV where it was (a struct may only grow at its tail)" "0" "$(grep -c '^DIFF struct' "$WORK/abi.txt")"
py contract "$ROOT/synth-open/plug/plug_abi.h" >"$WORK/contract_new.txt"
sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$HERE/abi_contract.txt" >"$WORK/contract_pin.txt"
expect "ABI: plug_abi.h of this tree is the pinned contract abi_contract.txt (every define, struct layout and enum value)" "" "$(diff "$WORK/contract_pin.txt" "$WORK/contract_new.txt" | grep '^[<>]' | head -6 | tr '\n' '|')"
expect "ABI: no enum value of $OV changed (kinds, cpus, extension ids, param flags are only added)" "0" "$(grep -c '^DIFF enum' "$WORK/abi.txt")"
# The Tracker+ and Mini take the codec rate for the sample-rate slot (#ifndef PLATFORM_IMX ... #else imx::kSampleRate ... #endif). The MK66 branch is what
# this firmware ships, so the table is compared with the IMX branch taken out, the same functions in the same order
host_table() {   # host_table FILE -> the s_host initialiser on the MK66 branch, blanks removed
    sed -n '/^const plug_host s_host = {/,/^};/p' "$1" | tr -d '\r' | awk '/^#ifndef PLATFORM_IMX/{next} /^#else/{skip=1;next} /^#endif/{skip=0;next} !skip' | tr -d ' \n'
}
HTOLD="$(host_table "$BOLD/synth-open/plug_host.cpp")"
HTNEW="$(host_table "$ROOT/synth-open/plug_host.cpp")"
[ -n "$HTNEW" ] && expect "the host service table the plugin gets (plug_host.cpp s_host) is the same list of functions" "$HTOLD" "$HTNEW" || fail "found the service table"
expect "the synth extension layout constant (kLayout, plug_synth.cpp) is the same" "$(sed -n 's/.*constexpr uint32_t kLayout *= *\([^;]*\);.*/\1/p' "$BOLD/synth-open/plug_synth.cpp" | head -1)" "$(sed -n 's/.*constexpr uint32_t kLayout *= *\([^;]*\);.*/\1/p' "$ROOT/synth-open/plug_synth.cpp" | head -1)"
py verify "$LOCKO" "$SYNO" "$FXO" >"$WORK/vold.txt"
expect "the $OV plugin files on the old card are the shipped ones (sha256 of the $OV lock)" "0 0" "$(grep -c '^bad' "$WORK/vold.txt") $(grep -c '^none' "$WORK/vold.txt")"
note "checked $(grep '^ok' "$WORK/vold.txt")"
py verify "$LOCKN" "$SYNN" "$FXN" >"$WORK/vnew.txt"
expect "the $NV plugin files are the ones the $NV lock pins" "0" "$(grep -c '^bad' "$WORK/vnew.txt")"
py delta "$LOCKO" "$LOCKN" >"$WORK/delta.txt"
echo "     files that differ between the two locks:"; sed 's/^/        /' "$WORK/delta.txt"
# the plugin files that are in both sets and not in the delta of the two locks are byte for byte the same (the cache and the lock agree), what the
# delta lists is what the pair changes: a file that is new to the pair, one that changed (its pair scenario is C2), one that is gone
unknown=""
for f in "$SYNO"/*.plg "$SYNO"/*.plgi "$FXO"/*.plg; do
    b="$(basename "$f")"; case "$f" in "$FXO"/*) g="$FXN/$b" ;; *) g="$SYNN/$b" ;; esac
    [ -f "$g" ] || continue
    grep -q "^[a-z]* $b\$" "$WORK/delta.txt" && continue
    cmp -s "$f" "$g" || unknown="$unknown $b"
done
expect "the $(( $(nfiles "$SYNO") + $(nfiles "$FXO") )) plugin files of $OV that the lock delta does not list are byte for byte the $NV files" "" "$unknown"
DELTA_CHANGED="$(sed -n 's/^changed \(.*\)\.plg$/\1/p' "$WORK/delta.txt" | grep -v -x -E 'bbd|console|dirt|glue|lofi|phaser|tape|tube' | head -1)"   # a synth engine that has two versions: the C2 pair
[ -s "$WORK/delta.txt" ] && note "the pair changes $(wc -l <"$WORK/delta.txt" | tr -d ' ') plugin file(s), listed above" || note "no plugin file differs between the two locks (the pair is a firmware change only)"
for f in "$SYNO/ju6.plg" "$SYNN/chip.plg" "$SYNN/pluck.plg"; do
    python3 -I - "$f" <<'PY'
import struct, sys
b = open(sys.argv[1], 'rb').read()
print('        %-9s abi %d.%d min_host %d version %06x' % (sys.argv[1].split('/')[-1], b[6], b[7], struct.unpack_from('<I', b, 20)[0], struct.unpack_from('<I', b, 16)[0]))
PY
done

# ------------------------------------------------------------------------------------------------------------------ A
echo "### scenario A1: $NV firmware, card with the $OV plugins ($( [ "$OLD_CHIP_PLG" = 1 ] && echo "chip.plg included" || echo "no chip.plg" ))"
NOLD="$(nfiles "$SYNO")"
sd="$WORK/a1"; card "$sd" "$SYNO" "$FXO"; prime "$HNEW" "$sd"
runh "$HNEW" "$sd" "$SYNTH; $(shot a1-card-old); poke picker"
expect "every $OV engine file is in the library" "$NOLD $NOLD" "$(syn_files "$sd") $(syn_listed "$sd")"
expect "no engine file has an error" "0" "$(grep ' stored ' "$sd/Plugins/synth.txt" | grep -vc ' err 0')"
expect "the master insert scan lists every $OV insert, none rejected" "listed $(nfiles "$FXO"), rejected 0" "$(scan_line "$sd")"
expect "the Model list is the 5 built-in engines and the $NOLD plugin engines" "$((5 + NOLD))" "$(list_of 1 | tr ',' '\n' | wc -l | tr -d ' ')"
if [ "$OLD_CHIP_PLG" = 0 ]; then
    case ",$(list_of 1)," in *,CHIP,*) fail "CHIP is not in the list without chip.plg" "$(list_of 1)" ;; *) pass "CHIP is not in the list without chip.plg (it is a plugin since it left the firmware)" ;; esac
else
    expect "CHIP is in the list once, from its chip.plg" "1" "$(list_of 1 | tr ',' '\n' | grep -c '^CHIP$')"
fi
LIST_A1="$(list_of 1)"

echo "### scenario A2: a $OV project (parts on CHIP, JU6, PLK) on $NV with the $OV card$([ "$OLD_CHIP_PLG" = 1 ] && echo ", chip.plg taken off it (CHIP is Missing)")"
sd="$WORK/a2"; mkoldproj "$sd"; card "$sd" "$SYNO" "$FXO"
[ "$OLD_CHIP_PLG" = 1 ] && rm -f "$sd/Plugins/Synth/chip.plg" "$sd/Plugins/Synth/chip.plgi"
expect "the project was written by the $OV firmware (header stamp)" "$OV" "$(py stamp "$sd/Workspace/project.mt")"
S0="$(wsum "$sd")"; S0P="$(wsum_att "$sd")"
runh "$HNEW" "$sd" "$SYNTH; $(shot a2-chip-missing); poke picker; poke rhs; instr 65; tap Pattern; wait 300; tap Params; wait 1500; poke picker; poke rhs; instr 66; tap Pattern; wait 300; tap Params; wait 1500; poke picker; poke rhs"
expect "part 1 keeps its stored number 6" "6" "$(part_line 1 0 | sed 's/.* stored \([0-9]*\) engine.*/\1/')"
expect "part 1 (CHIP) is Missing: CHIP, silent (R7: a flash alone breaks CHIP parts)" "Missing: CHIP" "$(rhs_stored 1)"
expect "part 2 (JU6, ju6.plg on the card) is in the Model list at its own row" "JU6" "$(list_of 2 | tr ',' '\n' | sed -n "$(( $(cursor_of 2) + 1 ))p")"
expect "part 3 (PLK, pluck.plg 1.0 on the card) is in the Model list at its own row" "PLK" "$(list_of 3 | tr ',' '\n' | sed -n "$(( $(cursor_of 3) + 1 ))p")"
expect "boot and the Synth screens did not rewrite the project or the patches" "$S0" "$(wsum "$sd")"
seeded_init "$sd"
# R1 and R2 (hide-and-legacy-engines.md): the Missing CHIP part highlights no row of the Model list. A change of the list (encoder, arrow keys,
# F1) moves it onto a row like on any part and the engine of that row is picked only after the rest of the list (kModelRestMs, 1 s), so the
# part is never trapped on the Missing engine (chip-missing-fix) and a stray click does not change it at once. Every key runs on a copy of the
# card, the card of the 1.2.0 project itself stays byte identical for the checks after this loop.
bar_of() { grep '^listbar' "$WORK/poke.out" | sed -n "${1}p" | sed 's/^listbar \([0-9]*\) pos.*/\1/'; }
popup_of() { grep '^popup' "$WORK/poke.out" | sed -n "${1}p"; }
runh "$HNEW" "$sd" "$SYNTH; poke picker"
expect "R2: the Missing CHIP part highlights no row of the Model list" "0" "$(bar_of 1)"
for keys in "enc -1" "enc 1" "tap Up" "tap Down" "tap F1"; do
    rm -rf "$sd.k"; cp -R "$sd" "$sd.k"
    runh "$HNEW" "$sd.k" "$SYNTH; poke popup; $keys; wait 300; poke picker; poke popup; wait 2500; poke picker; poke popup"
    expect "R1: $keys on the Missing CHIP part moves the list onto a row and does not change the engine before the rest (stored 6)" "1 6 popup none" "$(bar_of 1) $(part_line 1 0 | sed 's/.* stored \([0-9]*\) engine.*/\1/') $(popup_of 2)"
    expect "R1: $keys: after the rest the part is on the engine of that row (not stored 6, Init patch loaded)" "1 popup 'Init patch loaded'" "$([ "$(part_line 2 0 | sed 's/.* stored \([0-9]*\) engine.*/\1/')" != 6 ] && echo 1 || echo 0) $(popup_of 3)"
done
rm -rf "$sd.k"
# gate rule (a refusal needs its escape): Edit Patch (F8) and Load Patch (F7) of a Missing part are refused with the not installed popup. The way out is the Model
# list, the R1 loop above (a step moves the list onto a row, the engine of that row is picked after the rest)
runh "$HNEW" "$sd" "$SYNTH; tap F8; wait 400; poke popup; wait 2500; tap F7; wait 400; poke popup"
expect "R6: Edit Patch and Load Patch of the Missing CHIP part are refused with the not installed popup (the escape is the R1 loop above)" "popup 'CHIP not installed' popup 'CHIP not installed'" "$(popup_of 1) $(popup_of 2)"
# the user installs the new chip.plg: the part is back
cp "$SYNN/chip.plg" "$SYNN/chip.plgi" "$sd/Plugins/Synth/"
runh "$HNEW" "$sd" "$SYNTH; poke picker; poke rhs"
case "$(rhs_stored 1)" in Missing*) fail "with chip.plg added the CHIP part is no longer Missing" "$(grep '^rhs' "$WORK/poke.out" | head -1)" ;; *) pass "with chip.plg added the CHIP part is no longer Missing (engine $(grep '^rhs' "$WORK/poke.out" | head -1 | sed 's/^rhs engine\[\([^]]*\)\].*/\1/'))" ;; esac
S0A="$(wsum_att "$sd")"; expect "and the project is still the one of $OV" "$S0P" "$S0A"
seeded_init "$sd" chip

echo "### scenario A3: the header gate of the loader, $OV plugin files with one header field changed (new host and old host)"
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
min_host_level:min_host:LEVEL:1
min_host_over:min_host:LEVEL+1:0
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
    if [ "$hostname" = new ]; then host="$HNEW"; else host="$HOLD"; fi
    bad=0; got_all=""
    for row in $GATE_ROWS; do
        IFS=: read -r name field val want <<<"$row"
        # min_host is judged against the host level of the firmware being asked: its own level is accepted, one above is refused (ABI 1.4: 1 -> 2 on the new side)
        lvl="$OLD_LVL"; [ "$hostname" = new ] && lvl="$NEW_LVL"
        case "$val" in LEVEL) val="$lvl" ;; LEVEL+1) val=$((lvl + 1)) ;; esac
        got="$(gate "$host" "${hostname}_$name" "$SYNO/ju6.plg" "$field" "$val" "$want")"
        got_all="$got_all $name=$got"
        [ "$got" = "$want" ] || { bad=$((bad+1)); note "$hostname host: $name ($field=$val) expected listed=$want got $got"; }
    done
    expect "$hostname firmware: a $OV plugin file is accepted, any header outside the rules is refused (21 header variants, min_host judged against that firmware's own host level)" "0" "$bad"
    [ "$hostname" = new ] && GATE_NEW="$got_all" || GATE_OLD="$got_all"
done
# every variant but min_host (which each firmware answers by its own host level, checked above) is answered the same by both. With equal host levels min_host is too.
strip_mh() { printf '%s\n' "$1" | tr ' ' '\n' | grep -vE '^min_host_(level|over)=' | tr '\n' ' '; }
if [ "$OLD_LVL" = "$NEW_LVL" ]; then expect "both firmwares answer every header variant the same" "$GATE_OLD" "$GATE_NEW"
else expect "both firmwares answer every header variant the same (min_host aside: host level $OLD_LVL on the old side, $NEW_LVL on the new one, each checked above)" "$(strip_mh "$GATE_OLD")" "$(strip_mh "$GATE_NEW")"; fi
note "abi_minor (0 and 9), plugin version and the CRC are not checked by the scan (the CRC is checked at install: the insert test below, device for engines)"

echo "### scenario A4: the $OV master inserts on $NV (Tape and Lo-Fi in the slots, a corrupt Tape, Glue missing)"
if [ "$TWINS" = 1 ]; then
    TAPE=9c9b2979; LOFI=e5b380f1; GLUE=9f6aa8f0
    for hostname in new old; do
        if [ "$hostname" = new ]; then host="$HNEW"; else host="$HOLD"; fi
        sd="$WORK/a4_$hostname"; card "$sd" "$SYNO" "$FXO"; prime "$host" "$sd"
        sidecar "$sd/Workspace/plugins.txt" $TAPE Tape $LOFI Lo-Fi; cp "$sd/Workspace/plugins.txt" "$WORK/a4.side"
        runh "$host" "$sd" "wait 6000; poke inserts"
        expect "$hostname firmware: Tape in insert 1 and Lo-Fi in insert 2 load" "loaded loaded" "$(loaded "$(st_of 1)") $(loaded "$(st_of 2)")"
        expect "$hostname firmware: the sidecar is untouched by the load" "$(shasum "$WORK/a4.side" | cut -c1-12)" "$(shasum "$sd/Workspace/plugins.txt" | cut -c1-12)"
        # a corrupt Tape (one image byte flipped, header and CRC field as shipped)
        py mut "$FXO/tape.plg" "$sd/Plugins/Master/tape.plg" payload 300
        runh "$host" "$sd" "wait 6000; poke inserts"
        expect "$hostname firmware: a corrupt Tape is Failed with the CRC error (6), nothing runs from it" "failed 6" "$(st_of 1) $(ins_err "$sd" 1)"
        expect "$hostname firmware: and the other insert still loads" "loaded" "$(loaded "$(st_of 2)")"
        # a project with Glue in insert 2 and no glue.plg on the card: Missing, the values stay in the file
        sd="$WORK/a4m_$hostname"; card "$sd" "$SYNO" "$FXO"; prime "$host" "$sd"
        sidecar "$sd/Workspace/plugins.txt" $TAPE Tape $GLUE Glue; cp "$sd/Workspace/plugins.txt" "$WORK/a4m.side"
        runh "$host" "$sd" "wait 12000; poke inserts"
        expect "$hostname firmware: Glue in the project, no glue.plg: the slot is Missing, the other insert loads" "missing loaded" "$(st_of 2) $(loaded "$(st_of 1)")"
        expect "$hostname firmware: the sidecar with the Glue values is untouched" "$(shasum "$WORK/a4m.side" | cut -c1-12)" "$(shasum "$sd/Workspace/plugins.txt" | cut -c1-12)"
    done
else
    echo "     SKIP: no insert twin sources (effects-os checkout at the pinned tag)"
fi
# the shared insert slot of the tree under test (plug_index.h kInsertRoom), the one both releases' inserts run in after the update
SLOT_ROOM="$(sed -n 's/^constexpr uint32_t kInsertRoom = \([0-9]*\);.*/\1/p' "$ROOT/synth-open/plug_index.h")"
[ -n "$SLOT_ROOM" ] || { echo "fw_engine_mismatch_sim.sh: no kInsertRoom in $ROOT/synth-open/plug_index.h" >&2; exit 2; }
py pairs "$SLOT_ROOM" "$FXO" >"$WORK/pairsold.txt"
note "$(grep '^sizes' "$WORK/pairsold.txt" | cut -c1-200)"
if [ "$OLD_SHARED" = 1 ]; then
    # both releases have the one shared insert slot: a pair that does not fit is not something the update does to a project, it never fitted ($OV and $NV, the finding is F3 below)
    pass "the inserts of $OV share the one insert slot like the new ones: a pair that does not fit never did (F3 below, $(grep '^nofit' "$WORK/pairsold.txt" | sed 's/^nofit //' | cut -c1-80))"
else
    expect "every pair of the $OV inserts fits the one insert slot of $NV (insert 2 ran from the flash in $OV)" "nofit -" "$(grep '^nofit' "$WORK/pairsold.txt")"
fi

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
echo "### scenario B1: $OV firmware, card with the whole $NV set$([ "$OLD_CHIP_PLG" = 0 ] && echo " (chip.plg duplicates the built-in CHIP)")"
NNEW="$(nfiles "$SYNN")"
sd="$WORK/b1"; card "$sd" "$SYNN" "$FXN"; prime "$HOLD" "$sd"
runh "$HOLD" "$sd" "$SYNTH; poke picker"
DUP=$((1 - OLD_CHIP_PLG))      # the old firmware has CHIP built in: chip.plg is a duplicate of its number 6 and is refused (1), else it is the engine (0)
expect "no crash, the old firmware lists every file of the $NV set it can" "$NNEW $((NNEW - DUP))" "$(syn_files "$sd") $(syn_listed "$sd")"
if [ "$DUP" = 1 ]; then
    case "$(syn_engine "$sd" chip.plg)" in "chip.plg CHIP stored 6 not listed err 6") pass "chip.plg is refused as a duplicate of the built-in number 6 (err 6, not listed)" ;; *) fail "chip.plg on $OV" "$(syn_engine "$sd" chip.plg)" ;; esac
    expect "CHIP is in the Model list exactly once (the built-in one)" "1" "$(list_of 1 | tr ',' '\n' | grep -c '^CHIP$')"
else
    case "$(syn_engine "$sd" chip.plg)" in "chip.plg CHIP stored 6 "*" err 0"*) pass "chip.plg is the CHIP engine of $OV (err 0, listed)" ;; *) fail "chip.plg on $OV" "$(syn_engine "$sd" chip.plg)" ;; esac
    expect "CHIP is in the Model list exactly once (chip.plg)" "1" "$(list_of 1 | tr ',' '\n' | grep -c '^CHIP$')"
fi
expect "the other engines of $NV are listed, PLK and PHZ included" "$((5 + NNEW))" "$(list_of 1 | tr ',' '\n' | wc -l | tr -d ' ')"
expect "the $NV inserts, Glue included, pass the old scan" "listed $(nfiles "$FXN"), rejected 0" "$(scan_line "$sd")"
expect "no engine line has another error than the CHIP duplicate" "$DUP" "$(grep ' stored ' "$sd/Plugins/synth.txt" | grep -vc ' err 0')"
note "synth.txt is the only place that says chip.plg was ignored, nothing on the screen"

echo "### scenario B2: a project that $NV wrote (parts on CHIP, ACD, VAP) opened by $OV, then $NV again"
sd="$WORK/b2"; card "$sd" "$SYNN" "$FXN"; prime "$HNEW" "$sd"
"$HNEW" "$sd" "$SYNTH; tap Down 2; wait 2500; tap Pattern; wait 600" >/dev/null 2>&1     # the pick of CHIP (a plugin on the new firmware) writes its patch
py seed "$sd/Workspace/project.mt" 6 0 2
S1="$(sum "$sd"/Workspace/project.mt "$sd"/Workspace/?.patch)"
expect "the project of the new firmware says $NV in its header" "$NV" "$(py stamp "$sd/Workspace/project.mt")"
runh "$HOLD" "$sd" "$SYNTH; poke picker; instr 65; tap Pattern; wait 300; tap Params; wait 1500; poke picker; instr 66; tap Pattern; wait 300; tap Params; wait 1500; poke picker"
expect "$OV reads the project: part 1 is on CHIP$([ "$OLD_CHIP_PLG" = 0 ] && echo " (the built-in one)") (stored 6)" "part 0 stored 6 engine CHIP" "$(part_line 1 0)"
expect "the CHIP row is the part's row in the list (it is not Missing, cursor on CHIP)" "CHIP" "$(list_of 1 | tr ',' '\n' | sed -n "$(( $(cursor_of 1) + 1 ))p")"
expect "parts 2 and 3 (ACD, VAP) are the same engines" "ACD VAP" "$(part_engine 2 1) $(part_engine 3 2)"
expect "opening it and showing the Synth screens rewrote neither the project nor the patches" "$S1" "$(sum "$sd"/Workspace/project.mt "$sd"/Workspace/?.patch)"
runh "$HNEW" "$sd" "$SYNTH; poke picker"
expect "and $NV reads it again as before" "part 0 stored 6 engine CHIP" "$(part_line 1 0)"
# the two firmwares write the same project structure: a default project of each differs in the version stamp and the CRC only
sdo="$WORK/b2dold"; sdn="$WORK/b2dnew"; rm -rf "$sdo" "$sdn"; mkdir -p "$sdo" "$sdn"; prime "$HOLD" "$sdo"; prime "$HNEW" "$sdn"
py projdiff "$sdo/Workspace/project.mt" "$sdn/Workspace/project.mt" >"$WORK/pd.txt"
# the stamp bytes (4 major, 5 minor, 6 patch of the version) that differ between the two versions, and the CRC, are all that may differ
IFS=. read -r ov1 ov2 ov3 <<<"$OV"; IFS=. read -r nv1 nv2 nv3 <<<"$NV"
STAMPDIFF=""; [ "$ov1" = "$nv1" ] || STAMPDIFF="${STAMPDIFF}4,"; [ "$ov2" = "$nv2" ] || STAMPDIFF="${STAMPDIFF}5,"; [ "$ov3" = "$nv3" ] || STAMPDIFF="${STAMPDIFF}6,"
PSZ="$(wc -c <"$sdn/Workspace/project.mt" | tr -d ' ')"
expect "a default project of $OV and of $NV: same size, only the firmware stamp (bytes ${STAMPDIFF%,}) and the CRC (last 4 bytes) differ" "size $PSZ $PSZ diff ${STAMPDIFF}$((PSZ - 4)),$((PSZ - 3)),$((PSZ - 2)),$((PSZ - 1))" "$(cat "$WORK/pd.txt")"
expect "the Synth stored numbers of a default project sit at the same offsets and hold the same engines" "$(py stored "$sdn/Workspace/project.mt")" "$(py stored "$sdo/Workspace/project.mt")"
# The loader is the same code in both firmwares apart from the project.mtff stamp hook (b2ccf89): plug::projectStamp(crc, size) (ffHookProjectStamp
# since the project store core/FileManager/projectSidecar.cpp, no #ifdef) is a void call that only records the CRC and size of the project.mt just read or written, it decides nothing. So the two files are
# compared with exactly that hook (the include and the one-statement blocks) and the [[maybe_unused]] marks of the warning cleanup taken out. Anything
# else that differs (a version compare, a refusal, a changed read) still turns the check RED, and so does a projectStamp call used as a value.
loader_core() {   # loader_core FILE -> the file without the stamp hook, CRs, [[maybe_unused]] marks and the upstream bounds fixes (a missing old file stays empty = different)
    python3 -I - "$1" <<'PY'
import re, sys
t = open(sys.argv[1], "rb").read().decode("utf-8", "replace").replace("\r", "")
t = re.sub(r"#ifdef FEATURE_PLUG_HOST\n(?:#include \"plug_host\.h\"\n|[ \t]*plug::projectStamp\([^;\n]*\);[^\n]*\n)#endif\n", "", t)
# the same hook as an ff_hooks.h call (the project store, 1.4.1 arch): the include and the one-statement line
t = re.sub(r"#include \"ff_hooks\.h\"\n|[ \t]*ffHookProjectStamp\([^;\n]*\);[^\n]*\n", "", t)
t = t.replace("[[maybe_unused]] ", "")
# The FW_LTO=ui stack fix (6c19d61f): noinline on the two functions with a 500 B or larger frame, plus its comment. Codegen only, taken out as exact literals.
t = t.replace("// noinline on the functions with a 500 B or larger frame: with FW_LTO=ui the optimiser inlined them into callers that then kept the frame\n"
              "// while they called the next big one (ProjectSave::saveProjectInit 296 B -> 568 B, main loop +312 B at the stack-margin gate)\n", "")
t = t.replace("__attribute__((noinline)) ", "")
# The upstream bounds fixes that the ambv merge (2a2a32d0) brought in: 5e30ec63 (stop creating the project folder when mkdir fails) and e9e57dba
# (bound two path concatenations). Each post-fix hunk is mapped back, as an exact literal, to the text it replaced, so a release with or without the
# fixes compares the same. Only these literals are accepted: any other change in the same functions still differs. An old side that has the fixes
# already (a later release) maps the same way and an old side without them has nothing to map.
FIXES = [
    # e9e57dba, getCurrentProjectPathWithName: the separator and the name are bounded
    ("    if (pathLen > 0 && pathLen < maxLen - 1)\n", "    if (pathLen > 0)\n"),
    ("    // strncat takes the limit as an unsigned number, so a negative one is no limit at all.\n"
     "    if (nameMaxLen > 0)\n"
     "    {\n"
     "        strncat(path, currentProjectName, nameMaxLen);\n"
     "    }\n",
     "    strncat(path, currentProjectName, nameMaxLen);\n"),
    # a67a1506, writeProjectFileToFileStruct: the project path is copied with the size of its field (a path of 222 characters or more wrote past the
    # structure, a shorter path is stored as before). Save path only, a project of the old release loads as before.
    ("    // The path of the open project can be longer than its field in the project file.\n"
     "    strncpy(pFile->projectDataAndHeader.project.projectPath,\n"
     "            currentProjectPatch,\n"
     "            PATCH_SIZE - PROJECT_NAME_SIZE - 1);\n",
     "    strcpy(pFile->projectDataAndHeader.project.projectPath, currentProjectPatch);\n"),
    # ff63fecf, loadProjectFileFormFileStruct: structure version 0 is refused like a wrong type or size (the old code called the entry in front of the
    # function table of translateProject(), a hard fault). Versions 1 and up, which every release writes, never reach the refusal. The other hunk of
    # ff63fecf (the version 15 name copy in mtFileManagerOldVersionRecovery.cpp) is not in this file.
    ("    // Structure versions start at 1. translateProject() calls one function per version\n"
     "    // from a table whose first entry belongs to version 1.\n"
     "    if (pFile->projectDataAndHeader.projectHeader.fileStructureVersion[0] == 0)\n"
     "        return false;\n\n", ""),
    # 5e30ec63, createCurrentProjectFolderTree: reports failure and ends when mkdir fails
    # the IMX port (imx-merge141): the sample pool size is the platform's own constant, SAMPLE_MEMORY_SIZE on the MK66 (same value there, mtStructs.h)
    ("    source->max_memory = SAMPLE_POOL_BYTES;\n", "    source->max_memory = SAMPLE_MEMORY_SIZE;\n"),
    ("bool cFileManager::createCurrentProjectFolderTree()\n", "void cFileManager::createCurrentProjectFolderTree()\n"),
    ("    // A pass creates the next missing folder of the path, so one pass per character is\n"
     "    // always enough. The limit ends the loop for a path that never comes to exist even\n"
     "    // though no mkdir fails.\n"
     "    int passesLeft = strlen(fullProjectPath);\n"
     "    bool mkdirFailed = false;\n\n", ""),
    ("        if (fileSystem->exists(tempPath))\n        {\n            return true;\n        }\n\n"
     "        if (mkdirFailed || passesLeft-- <= 0)\n        {\n            return false;\n        }\n",
     "        if (fileSystem->exists(tempPath))\n        {\n            break;\n        }\n"),
    ("                    mkdirFailed |= !fileSystem->mkdir(0, tempPath);\n", "                    fileSystem->mkdir(0, tempPath);\n"),
]
for post, pre in FIXES:
    t = t.replace(post, pre)
sys.stdout.write(t)
PY
}
LOADER_OLD="$BOLD/core/FileManager/fileManagerProject.cpp"; LOADER_NEW="$ROOT/core/FileManager/fileManagerProject.cpp"
expect "the project file loader (fileManagerProject.cpp) is the same code in both firmwares apart from the project.mtff stamp hook: no gate on the firmware stamp" "same" "$([ -f "$LOADER_OLD" ] && [ "$(loader_core "$LOADER_OLD" | shasum | cut -c1-40)" = "$(loader_core "$LOADER_NEW" | shasum | cut -c1-40)" ] && echo same || echo different)"
# the hook is plug::projectStamp (up to 1.4.1) or ffHookProjectStamp of core/include/ff_hooks.h (the project store): void either way
STAMP_DECL="$(( $(grep -c '^void projectStamp(uint32_t crc, uint32_t size);' "$ROOT/synth-open/plug_host.h") + $(cat "$ROOT/core/include/ff_hooks.h" 2>/dev/null | grep -c '^FF_HOOK_INLINE void ffHookProjectStamp(uint32_t crc, uint32_t size)$') ))"
expect "the stamp hook of $NV only records (the stamp hook is void, it is a statement in the loader, never a condition)" "1 2 0" "$STAMP_DECL $(grep -Ec '^[[:space:]]*(plug::projectStamp|ffHookProjectStamp)\(' "$LOADER_NEW") $(grep -E '(if|while|return|=|&&|\|\|)[^;]*(projectStamp|ProjectStamp)' "$LOADER_NEW" | grep -vc '^[[:space:]]*//')"

echo "### scenario B3: a patch that $NV wrote, through the patch codec of $OV (the load and the save path)"
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
if build_rt "$BOLD" "$WORK/rtold" && build_rt "$ROOT" "$WORK/rtnew"; then
    sd="$WORK/b3"; rm -rf "$sd"; mkdir -p "$sd"; prime "$HNEW" "$sd"
    P="$sd/Workspace/1.patch"
    expect "a new patch of $NV is written with field 100 (macro depth mode Scale is the default of a new patch): bytes a0 06 01 at the end" "a00601" "$(tail -c 3 "$P" | xxd -p)"
    # the codec contract of both releases is macro_mode_compat_check.sh: a 1.2 patch loads identically on 1.3, a 1.3 patch round-trips through the 1.2 codec
    if mmc="$("$ROOT/synth-open/tools/macro_mode_compat_check.sh" 2>&1)"; then
        pass "the macro depth mode codec contract holds in both directions (macro_mode_compat_check.sh, $(printf '%s\n' "$mmc" | grep -c '^PASS') checks)"
    else
        fail "macro_mode_compat_check.sh" "$(printf '%s\n' "$mmc" | grep -A3 '^FAIL' | head -12 | sed '2,$s/^/      /')"      # the nested lines are indented: a line of the sub script that starts with FAIL would be counted by the gate
    fi
    # wave file references (patch field 101, a wave parameter by file name): a patch without one is written and read as before, one with it round-trips through the old codec
    if wrc="$(OLD_REF="$OLD_COMMIT" "$ROOT/synth-open/tools/wave_ref_compat_check.sh" 2>&1)"; then
        pass "the wave file reference codec contract holds against $OV (wave_ref_compat_check.sh, $(printf '%s\n' "$wrc" | grep -c '^PASS') checks)"
    else
        fail "wave_ref_compat_check.sh" "$(printf '%s\n' "$wrc" | grep -A3 '^FAIL' | head -12 | sed '2,$s/^/      /')"
    fi
    "$WORK/rtold" "$P" "$WORK/rtold.out" >/dev/null && "$WORK/rtnew" "$P" "$WORK/rtnew.out" >/dev/null
    expect "$OV loads that patch and saves it back byte for byte (the field is $([ "$OLD_F100" = 1 ] && echo read || echo "kept as an unknown field"))" "$(shasum "$P" | cut -c1-12)" "$(shasum "$WORK/rtold.out" | cut -c1-12)"
    expect "$NV round trips it too" "$(shasum "$P" | cut -c1-12)" "$(shasum "$WORK/rtnew.out" | cut -c1-12)"
    for f in "$ROOT"/synth-open/tools/fixtures/ot-test-07/*.patch; do
        "$WORK/rtold" "$f" "$WORK/o12.patch" >/dev/null; "$WORK/rtnew" "$f" "$WORK/o13.patch" >/dev/null
        cmp -s "$f" "$WORK/o12.patch" && cmp -s "$f" "$WORK/o13.patch" || fail "OpenTracker fixture $(basename "$f") round trip" "$(sum "$f" "$WORK/o12.patch" "$WORK/o13.patch")"
    done
    pass "the 4 OpenTracker fixture patches round trip byte for byte in both codecs"
    # a patch of 1.3.0 that 1.2.0 would take for Add: field 100 is not read by 1.2.0 (macro depth follows the Add rule, the sound differs)
    # F1 is a known accepted finding (user decision 2026-11-12): the new codec always writes field 100 for Scale, a firmware without it (1.2.x) plays Scale as Add, data kept
    if [ "$(grep -c 'kMacroModeField' "$BOLD/synth-open/open_patchdata.h")" = 0 ]; then
        known "F1: $OV does not read the macro depth mode (field 100): a $NV Scale patch with a macro amount plays as Add on $OV (accepted, the data is kept)"
    else
        pass "F1: $OV reads field 100, so Scale patches sound the same on both releases"
    fi
    # the unknown fields the old codec keeps are limited to kRawMax 768 bytes (1.2.x): the 3 bytes of field 100 are 3 more. A codec that reads field 100 has no such limit here
    mkdir -p "$WORK/edge"
    head -c $(( $(wc -c <"$P") - 3 )) "$P" >"$WORK/base_add.patch"
    "$WORK/rtold" "$WORK/base_add.patch" "$WORK/x.patch" >"$WORK/raw.txt"; RAW="$(sed 's/.*raw //' "$WORK/raw.txt")"
    py edgepatch "$WORK/base_add.patch" "$WORK/edge" "$RAW" >/dev/null
    r12=0; r13=0
    "$WORK/rtold" "$WORK/edge/edge_over_scale.patch" "$WORK/x.patch" >/dev/null 2>&1 && r12=1
    "$WORK/rtnew" "$WORK/edge/edge_over_scale.patch" "$WORK/x.patch" >/dev/null 2>&1 && r13=1
    "$WORK/rtold" "$WORK/edge/edge_fits_scale.patch" "$WORK/x.patch" >/dev/null 2>&1 && f12=1 || f12=0
    expect "F2 (edge): $OV decodes a $NV patch whose unknown-field block is over 765 bytes (kRawMax 768 less the 3 of field 100; real patches: 8 to 52 bytes)" "1 1 1" "$r12 $r13 $f12"
else
    fail "build the patch codec test (open_patchio.cpp) for both trees" "$(head -5 "$WORK/rt.log")"
fi

echo "### scenario B4: names, synth order file and sidecar written by $NV, on $OV"
sd="$WORK/b4"; card "$sd" "$SYNN" "$FXN"; prime "$HNEW" "$sd"
runh "$HNEW" "$sd" "$SYNTH; poke picker"          # new firmware boot: writes names.txt and synth.txt
printf 'CHIP\r\n-JU6\r\nACD\r\n' >"$sd/Plugins/synth-order.txt"
cp "$sd/Plugins/names.txt" "$WORK/namesnew.txt"; cp "$sd/Plugins/synth-order.txt" "$WORK/ordernew.txt"
sidecar "$sd/Workspace/plugins.txt" 9f6aa8f0 Glue 9c9b2979 Tape; cp "$sd/Workspace/plugins.txt" "$WORK/b4.side"
grep -q '^6 CHIP' "$sd/Plugins/names.txt" && pass "$NV remembers CHIP as number 6 in names.txt (the line a firmware with CHIP built in never wrote)" || fail "$NV names.txt has 6 CHIP" "$(cat "$sd/Plugins/names.txt" | head -3)"
runh "$HNEW" "$sd" "$SYNTH; poke picker"; LIST_B4_NEW="$(list_of 1)"      # the order file applied by the new firmware (CHIP first, JU6 hidden)
runh "$HOLD" "$sd" "$SYNTH; poke picker"
if [ "$OLD_ORDER" = 1 ]; then
    expect "$OV reads synth-order.txt like $NV: the same Model list on the same card" "$LIST_B4_NEW" "$(list_of 1)"
else
    expect "$OV shows the default engine list: synth-order.txt is not read (JU6 not hidden, CHIP not first)" "ACD,FAT,VAP,WTFM,PERC,CHIP,JU6" "$(list_of 1 | cut -d, -f1-7)"
fi
expect "$OV left synth-order.txt alone" "$(shasum "$WORK/ordernew.txt" | cut -c1-12)" "$(shasum "$sd/Plugins/synth-order.txt" | cut -c1-12)"
grep -q '^6 CHIP' "$sd/Plugins/names.txt" && pass "names.txt still holds 6 CHIP after $OV" || note "names.txt after $OV: $(tr '\r\n' ' ' <"$sd/Plugins/names.txt" | cut -c1-60) ($OV rewrote it without 6, $NV learns it again at boot)"
expect "the sidecar with a Glue slot (no glue-capable twin on this host) is not rewritten by the boot of $OV" "$(shasum "$WORK/b4.side" | cut -c1-12)" "$(shasum "$sd/Workspace/plugins.txt" | cut -c1-12)"
note "the sidecar written back by a project save keeps a slot whose plugin is missing: plug_sidecar_test above (a missing plugin keeps its values), plug_host.cpp projectSave"
runh "$HNEW" "$sd" "$SYNTH; poke picker"
expect "$NV on the same card afterwards: the order file still applies" "CHIP" "$(list_of 1 | cut -d, -f1)"

B5_FIX=a1852a75158a415f4015cf8d84c2f4b9389c2e89   # fix(synth): let a part on a missing engine switch to an installed one (in tracker-ff-v1.3.1, not in v1.3.0)
echo "### scenario B5: a part stored on an engine whose .plg is not on the card (Missing) can be switched away, on $OV and on $NV (the 1.3.0 trap: the Model list refused every change of a Missing part)"
# gate rule: every state the UI refuses to work in has a way out that is checked. The Missing part (ju6.plg taken off the $OV card, part 2 stored on JU6) plays nothing and
# refuses Params pages and the patch editor: a step on the Model list has to move it onto an engine that is installed. A RED here is a finding about the release pair.
for who in OLD NEW; do
    if [ "$who" = OLD ]; then H="$HOLD"; V="$OV"; else H="$HNEW"; V="$NV"; fi
    sd="$WORK/b5$who"; mkoldproj "$sd"; card "$sd" "$SYNO" "$FXO"
    rm -f "$sd/Plugins/Synth/ju6.plg" "$sd/Plugins/Synth/ju6.plgi"
    runh "$H" "$sd" "instr 65; tap Params; wait 1500; poke picker; tap Down; wait 300; poke picker; wait 2500; poke picker"
    st() { part_line "$1" 1 | sed 's/.* stored \([0-9]*\) engine.*/\1/'; }
    expect "$V: part 2 is stored on JU6 (7) and its engine is not installed (the card has no ju6.plg)" "7" "$(st 1)"
    expect "$V: a step on the Model list does not change the engine before the rest (stored 7)" "7" "$(st 2)"
    if [ "$(st 3)" != 7 ]; then
        pass "$V ESCAPE: after the rest the Missing part is on an installed engine (stored $(st 3), $(part_engine 3 1))"
    elif [ "$who" = OLD ] && ! git -C "$ROOT" merge-base --is-ancestor "$B5_FIX" "$OLD_COMMIT" 2>/dev/null; then
        # B5 is a known accepted finding like F1: the published 1.3.0 really traps a Missing part (the Model list refuses every change, ref/chip-missing/REPORT.md).
        # The fix (a1852a75, 5af74401) shipped in 1.3.1. It is a KNOWN line only while the old side lacks that fix and the trap shows. An old side with the fix
        # (1.3.1 and later) has to PASS, and the NEW side is never excused.
        known "B5: $V does not let the Model list move a Missing part away (the 1.3.0 trap, fixed in 1.3.1 by $B5_FIX: stored $(st 3) after the rest)"
    else
        fail "$V ESCAPE: the Model list moves the Missing part away (the 1.3.0 trap)" "$(part_line 3 1)"
    fi
done

# ------------------------------------------------------------------------------------------------------------------ C
echo "### scenario C1: a half upgraded card on $NV ($OV engines + chip.plg of $NV + the newer versions of the changed plugins, Glue with the $OV inserts)"
sd="$WORK/c1"; mkoldproj "$sd"; card "$sd" "$SYNO" "$FXO"
cp "$SYNN/chip.plg" "$SYNN/chip.plgi" "$sd/Plugins/Synth/"; cp "$FXN/glue.plg" "$sd/Plugins/Master/"
S2="$(wsum_att "$sd")"
runh "$HNEW" "$sd" "$SYNTH; poke picker; poke rhs"
C1N="$(nfiles "$sd/Plugins/Synth")"; C1F="$(nfiles "$sd/Plugins/Master")"      # the files on the card: the old ones, plus chip.plg and glue.plg when the old set had neither
expect "everything is listed, CHIP included" "$C1N $C1N" "$(syn_files "$sd") $(syn_listed "$sd")"
case "$(rhs_stored 1)" in Missing*) fail "the CHIP part is not Missing" "$(grep '^rhs' "$WORK/poke.out" | head -1)" ;; *) pass "the CHIP part is not Missing" ;; esac
expect "and nothing of the project or the patches was rewritten" "$S2" "$(wsum_att "$sd")"
seeded_init "$sd" chip
expect "the inserts, Glue included, are all listed" "listed $C1F, rejected 0" "$(scan_line "$sd")"
# C2: two versions of one plugin. The real pair is a synth engine that differs between the two locks (PLK 1.0 and 1.1 for 1.2.0 and 1.3.0). Between two
# locks that pin the same files the older one is the old file with an older version in its header (the version is the only thing the pick looks at)
C2P=""; C2NEW=""
if [ -n "$DELTA_CHANGED" ] && [ -f "$SYNO/$DELTA_CHANGED.plg" ] && [ -f "$SYNN/$DELTA_CHANGED.plg" ]; then
    C2P="$DELTA_CHANGED"; C2NEW="$SYNN/$C2P.plg"; C2HOW="the $OV and $NV files of $C2P.plg"
elif [ -f "$SYNO/ju6.plg" ]; then
    C2P=ju6; C2NEW="$WORK/c2_newer.plg"; py mut "$SYNO/ju6.plg" "$C2NEW" version 0x090000; C2HOW="ju6.plg and a copy with a newer version in its header (no synth engine differs between the two locks)"
fi
if [ -n "$C2P" ]; then
    echo "### scenario C2: two versions of one plugin on the card ($C2HOW, one uid)"
    sd="$WORK/c2k"; card "$sd" "$SYNO" "$FXO"; prime "$HNEW" "$sd"; "$HNEW" "$sd" "wait 200" >/dev/null 2>&1
    C2KEY="$(syn_engine "$sd" "$C2P.plg" | awk '{print $2 " stored " $4}')"      # e.g. "PLK stored 18": the engine of the file, as synth.txt names it
    [ -n "$C2KEY" ] || C2KEY="no-engine-line-for-$C2P"
    sd="$WORK/c2"; card "$sd" "$SYNO" "$FXO"; cp "$C2NEW" "$sd/Plugins/Synth/${C2P}_new.plg"; prime "$HNEW" "$sd"
    "$HNEW" "$sd" "wait 200" >/dev/null 2>&1
    expect "one $C2KEY entry, not two" "1" "$(grep -c " $C2KEY" "$sd/Plugins/synth.txt")"
    expect "and it is the newer file (${C2P}_new.plg)" "${C2P}_new.plg" "$(grep " $C2KEY" "$sd/Plugins/synth.txt" | cut -d' ' -f1)"
    sd="$WORK/c2o"; card "$sd" "$SYNO" "$FXO"; cp "$C2NEW" "$sd/Plugins/Synth/zz_new.plg"; cp "$SYNO/$C2P.plg" "$sd/Plugins/Synth/zz_old.plg"; rm "$sd/Plugins/Synth/$C2P.plg"
    "$HNEW" "$sd" "wait 200" >/dev/null 2>&1
    expect "the newer file wins whatever the order of the names" "zz_new.plg" "$(grep " $C2KEY" "$sd/Plugins/synth.txt" | cut -d' ' -f1)"
    sd="$WORK/c2b"; card "$sd" "$SYNO" "$FXO"; cp "$C2NEW" "$sd/Plugins/Synth/${C2P}_new.plg"; prime "$HOLD" "$sd"; "$HOLD" "$sd" "wait 200" >/dev/null 2>&1
    expect "the same on $OV: one $C2KEY entry, the newer file" "1 ${C2P}_new.plg" "$(grep -c " $C2KEY" "$sd/Plugins/synth.txt") $(grep " $C2KEY" "$sd/Plugins/synth.txt" | cut -d' ' -f1)"
else
    echo "### scenario C2: SKIP (no engine that has two versions: none differs between the locks and the old set has no ju6.plg)"
fi
echo "### scenario C3: $NV inserts that do not fit beside each other (a project of an older release names the ones it has, F3 lists the pair)"
py pairs "$SLOT_ROOM" "$FXN" >"$WORK/pairsnew.txt"
note "$(grep '^sizes' "$WORK/pairsnew.txt" | cut -c1-200)"
expect "F3: every pair of the $NV inserts fits the shared insert slot of $SLOT_ROOM B (else the second one is NoRoom, audio passes)" "nofit -" "$(grep '^nofit' "$WORK/pairsnew.txt")"

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
echo "     the hosts: new = $NV $(git -C "$ROOT" rev-parse --short HEAD), old = $OV $OLD_REF ($(git -C "$ROOT" rev-parse --short "$OLD_COMMIT"))"
[ "$knowns" -gt 0 ] && echo "known accepted findings: ${knowns} (F1, B5 when the old side is the published 1.3.0)"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks checks)"; exit 1
