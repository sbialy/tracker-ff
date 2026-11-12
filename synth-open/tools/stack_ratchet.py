#!/usr/bin/env python3
"""Stack ratchet: the -fstack-usage .su files of a finished build against a committed baseline.

The audio ISR and the main loop share one stack with no guard page, so a big frame or a VLA is the crash class behind the shot, Model-roll
and NewList faults (ref/crash-class-audit.md). The build already warns (CRASH_WARN = -Wstack-usage=512 -Wvla), but nobody reads 50 warnings.
This turns them into a gate. It fails on:

  - a NEW frame >= LIMIT (512) bytes that is not in the baseline
  - a NEW function with a dynamic frame (VLA or alloca, qualifier dynamic*) that is not in the baseline
  - a baseline frame that GREW (bytes up, or static -> dynamic)
  - with --log (a build log with the -Wvla warnings): a VLA (file, variable) that is not in the baseline or one more of the same name
  - the hard cap on the audio chain (frames in AUDIO_PATHS): no frame above AUDIO_FRAME_CAP, no more than AUDIO_MAX_ENTRIES entries
    at all, in the build and in the baseline file. The cap lives in this script, not in the baseline, so raising it is a visible diff
    here and not a one line edit of the data file.

Shrunk or removed frames pass and are reported, run with --update to lock the gain in. A new entry only gets in by an edited baseline in
the same commit (--update writes it). The baseline is per compiler major version: the frame sizes of GCC 10.3 and 12.3 differ, so
stack_baseline_gcc12.txt and stack_baseline_gcc10.txt both exist, and per flavour (the AUTOF image, see README).

  stack_ratchet.py --root REPO --baseline FILE --su-dir core/build-X-trim --su-dir platforms/mk66/build-trim [--log build.log] [--update]
"""
import argparse
import collections
import os
import re
import sys

LIMIT = 512
# hard cap for the audio ISR chain: the engine render frames, the synth shim and the mixer (7 entries and 1768 B at most today)
AUDIO_PATHS = ('synth-open/ot/audio/', 'synth-open/open_synth.cpp', 'synth-open/engine_', 'synth-open/plug_insert.cpp',
               'synth-open/plug_synth.cpp', 'core/libs/Audio/', 'core/AudioEngine/')
AUDIO_FRAME_CAP = 1792
AUDIO_MAX_ENTRIES = 7

SU_LINE = re.compile(r'^(.+?):(\d+):(\d+):(.*)$')
VLA_LINE = re.compile(r"^(\S+?):(\d+):\d+: warning: variable length array '([^']+)' is used \[-Wvla\]")
ANSI = re.compile(r'\x1b\[[0-9;]*m')


def is_audio(path):
    return any(path.startswith(p) for p in AUDIO_PATHS)


def norm(path, cwd_rel, root):
    """Repo relative path of a path as the compiler printed it (relative to the compile directory, or absolute)."""
    if os.path.isabs(path):
        p = os.path.relpath(os.path.normpath(path), root)
    else:
        p = os.path.normpath(os.path.join(cwd_rel, path))
    return p.replace(os.sep, '/')


def compile_dir(su_dir_rel):
    """core/build-X-trim -> core, platforms/mk66/build-trim -> platforms/mk66 (the make runs in the dir above build*)."""
    parts = su_dir_rel.replace(os.sep, '/').split('/')
    for i, c in enumerate(parts):
        if c.startswith('build'):
            return '/'.join(parts[:i])
    return '/'.join(parts)


def read_su(root, su_dirs):
    """{(path, signature): (bytes, qualifier)} for every function, the max over duplicates (inline functions repeat per object)."""
    out = {}
    for d in su_dirs:
        absd = os.path.join(root, d)
        if not os.path.isdir(absd):
            sys.exit('stack_ratchet: no such build dir %s (build first)' % d)
        cwd_rel = compile_dir(os.path.relpath(absd, root))
        n = 0
        for dp, _, fns in os.walk(absd):
            for fn in fns:
                if not fn.endswith('.su'):
                    continue
                with open(os.path.join(dp, fn), errors='replace') as f:
                    for line in f:
                        parts = line.rstrip('\n').split('\t')
                        if len(parts) != 3:
                            continue
                        m = SU_LINE.match(parts[0])
                        if not m:
                            continue
                        try:
                            nbytes = int(parts[1])
                        except ValueError:
                            continue
                        key = (norm(m.group(1), cwd_rel, root), m.group(4))
                        old = out.get(key)
                        if old is None or nbytes > old[0] or (nbytes == old[0] and parts[2] != 'static'):
                            out[key] = (nbytes, parts[2])
                        n += 1
        if n == 0:
            sys.exit('stack_ratchet: no .su lines under %s (was the build made with -fstack-usage?)' % d)
    return out


def read_vlas(root, log):
    """{(path, variable): count} of the -Wvla warnings in a build log. A warning is repeated by every object that includes the file,
    so a site (path, line, variable) counts once. Line numbers are not kept, two arrays of the same name in one file count 2."""
    sites = set()
    with open(log, errors='replace') as f:
        for line in f:
            m = VLA_LINE.match(ANSI.sub('', line))
            if m:
                sites.add((norm(m.group(1), 'core', root), int(m.group(2)), m.group(3)))
    return collections.Counter((p, v) for p, _, v in sites)


def tracked(frames):
    return {k: v for k, v in frames.items() if v[0] >= LIMIT or v[1] != 'static'}


def read_baseline(path):
    frames, vlas = {}, collections.Counter()
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            p = line.split('\t')
            if p[0] == 'frame' and len(p) == 5:
                frames[(p[3], p[4])] = (int(p[1]), p[2])
            elif p[0] == 'vla' and len(p) == 4:
                vlas[(p[1], p[2])] = int(p[3])
            else:
                sys.exit('stack_ratchet: %s:%d: cannot parse %r' % (path, n, line))
    return frames, vlas


def write_baseline(path, frames, vlas, comment):
    with open(path, 'w', newline='\n') as f:
        f.write(comment)
        for (p, sig), (b, q) in sorted(frames.items(), key=lambda kv: (-kv[1][0], kv[0])):
            f.write('frame\t%d\t%s\t%s\t%s\n' % (b, q, p, sig))
        for (p, v), n in sorted(vlas.items()):
            f.write('vla\t%s\t%s\t%d\n' % (p, v, n))


def audio_problems(frames, what):
    probs = []
    aud = {k: v for k, v in frames.items() if is_audio(k[0]) and v[0] >= LIMIT}
    for (p, sig), (b, q) in sorted(aud.items()):
        if b > AUDIO_FRAME_CAP:
            probs.append('%s: audio chain frame %d B is over the hard cap %d B: %s %s' % (what, b, AUDIO_FRAME_CAP, p, sig))
    if len(aud) > AUDIO_MAX_ENTRIES:
        probs.append('%s: %d audio chain frames of %d B or more, the hard cap allows %d entries (the script constant AUDIO_MAX_ENTRIES)'
                     % (what, len(aud), LIMIT, AUDIO_MAX_ENTRIES))
    return probs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', required=True)
    ap.add_argument('--baseline', required=True)
    ap.add_argument('--su-dir', action='append', required=True, help='build dir with .su files, relative to --root')
    ap.add_argument('--log', help='build log with the -Wvla warnings (optional, enables the VLA variable check)')
    ap.add_argument('--update', action='store_true', help='rewrite the baseline from this build (the hard cap still applies)')
    ap.add_argument('--tag', default='', help='text for the baseline header (flavour, compiler)')
    a = ap.parse_args()
    root = os.path.realpath(a.root)

    cur = tracked(read_su(root, a.su_dir))
    cur_vla = read_vlas(root, a.log) if a.log else None

    probs = audio_problems(cur, 'build')

    if a.update:
        old_vla = read_baseline(a.baseline)[1] if os.path.exists(a.baseline) else collections.Counter()
        vlas = cur_vla if cur_vla is not None else old_vla
        probs += audio_problems(cur, 'new baseline')
        if probs:
            print('\n'.join('FAIL ' + p for p in probs))
            return 1
        comment = ('# Stack ratchet baseline, written by stack_ratchet.py --update. Frames of %d B or more, and every function with a dynamic\n'
                   '# frame (VLA, alloca), in the %s. Columns: frame, bytes, qualifier, file, signature. vla: file, variable, count (from -Wvla).\n'
                   '# Lower a number or drop a line any time. Raising one or adding one is a review decision: say why in the commit.\n'
                   % (LIMIT, a.tag or 'AUTOF image'))
        write_baseline(a.baseline, cur, vlas, comment)
        print('baseline %s written: %d frames, %d VLAs' % (a.baseline, len(cur), sum(vlas.values())))
        return 0

    if not os.path.exists(a.baseline):
        sys.exit('stack_ratchet: no baseline %s for this compiler, build once with the compiler you want to gate and run\n'
                 '  make stack-ratchet-update (and commit the file)' % a.baseline)
    base, base_vla = read_baseline(a.baseline)
    probs += audio_problems(base, 'baseline')
    shrunk = []
    for k, (b, q) in sorted(cur.items()):
        p, sig = k
        old = base.get(k)
        if old is None:
            kind = 'new dynamic frame (VLA or alloca)' if q != 'static' and b < LIMIT else 'new frame of %d B (limit %d)' % (b, LIMIT)
            probs.append('%s: %s %s [%s]' % (kind, p, sig, q))
        elif b > old[0] or (q != 'static' and old[1] == 'static') or (q == 'dynamic' and old[1] != 'dynamic'):
            probs.append('grown frame %d -> %d B (%s -> %s): %s %s' % (old[0], b, old[1], q, p, sig))
        elif b < old[0]:
            shrunk.append((old[0], b, p, sig))
    gone = [k for k in base if k not in cur]
    if cur_vla is not None:
        for (p, v), n in sorted(cur_vla.items()):
            if n > base_vla.get((p, v), 0):
                probs.append("new VLA '%s' in %s (%d now, %d in the baseline)" % (v, p, n, base_vla.get((p, v), 0)))
        gone_vla = sorted(k for k, n in base_vla.items() if cur_vla.get(k, 0) < n)
    else:
        gone_vla = []

    print('stack ratchet: %d frames tracked (>= %d B or dynamic), %d in the baseline, %s' % (
        len(cur), LIMIT, len(base),
        ('%d VLA sites checked, %d in the baseline' % (sum(cur_vla.values()), sum(base_vla.values()))) if cur_vla is not None
        else 'VLA variables not checked (no --log), new VLA functions are'))
    for ob, nb, p, sig in shrunk:
        print('  shrunk %d -> %d B: %s %s' % (ob, nb, p, sig))
    for p, sig in gone:
        print('  gone from the build: %s %s' % (p, sig))
    if gone_vla:
        print('  %d baseline VLA site(s) not in the log (an incremental build only logs the files it compiled)' % len(gone_vla))
    if shrunk or gone:
        print('  (run `make stack-ratchet-update` to lock the gain in)')
    if probs:
        for p in probs:
            print('FAIL ' + p)
        print('stack ratchet: FAILED, %d problem(s). Fix the frame (move the scratch to a static or heap buffer), or if it is intended edit %s'
              ' in the same commit and say why.' % (len(probs), a.baseline))
        return 1
    print('stack ratchet: OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
