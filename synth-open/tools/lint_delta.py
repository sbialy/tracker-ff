#!/usr/bin/env python3
"""Lint delta gate: warnings of `make lint-original` (and optionally cppcheck) on lines ADDED since the merge-base with origin/main.

`make lint-original` compiles the file manager and the plugin host with the builtins on (-fno-builtin hides the bounds and format checks in
the release build) and prints warnings, it never fails, and 90 percent of what it prints is upstream code. This keeps only the hits on
lines this branch added or changed, in the classes that matched the device crashes, and fails on them:

  -Warray-bounds*  -Wnull-dereference  every *overflow class (-Wstringop-overflow, -Wformat-overflow, -Woverflow)
  -Wconversion  -Wfloat-conversion   only in the file manager (core/FileManager, core/source/File*) and the plugin host (synth-open/plug_*)

-Wformat-truncation is dropped on purpose (GCC reports "region of size 1" after `n += snprintf(t + n, ...)`: noise). A reviewed false
positive goes into lint_delta_allow.txt as  path <TAB> -Wclass <TAB> the source line text  (line numbers move, the text does not).

With --cppcheck the changed files also go through cppcheck, kept to a short list of check ids (below). cppcheck skips files with
`unknownMacro` unless -D'__NOINIT(x)=' is given, that define is always passed and an unknownMacro / syntaxError on a changed file fails
the run, so a skipped file cannot pass silently.

  lint_delta.py --root REPO --base origin/main --allow FILE LINT_LOG [--cppcheck]
"""
import argparse
import os
import re
import subprocess
import sys

WARN = re.compile(r'^(\S[^:]*):(\d+):(\d+): warning: (.*) \[(-W[A-Za-z0-9=_+-]+)\]$')
ANSI = re.compile(r'\x1b\[[0-9;]*m')
HUNK = re.compile(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@')

ALWAYS = ('-Warray-bounds', '-Wnull-dereference', 'overflow')
CONV = ('-Wconversion', '-Wfloat-conversion')
CONV_SCOPE = ('core/FileManager/', 'core/source/File', 'synth-open/plug_')

CPPCHECK_IDS = {'nullPointer', 'nullPointerRedundantCheck', 'nullPointerArithmetic', 'arrayIndexOutOfBounds', 'arrayIndexOutOfBoundsCond',
                'negativeIndex', 'bufferAccessOutOfBounds', 'uninitvar', 'incorrectLogicOperator', 'memleak', 'doubleFree',
                'danglingLifetime', 'returnDanglingLifetime', 'invalidPrintfArgType_s', 'invalidPrintfArgType_n', 'invalidPrintfArgType_p',
                'invalidPrintfArgType_uint', 'invalidPrintfArgType_sint', 'invalidPrintfArgType_float', 'wrongPrintfScanfArgNum'}
CPPCHECK_BAD = {'unknownMacro', 'syntaxError'}
CPPCHECK_DIRS = ('core/', 'synth-open/')
CPPCHECK_SKIP = ('core/include/', 'core/libs/FT812/', 'core/libs/fonts/', 'synth-open/tools/', 'synth-open/plug/')
# core/AudioEngine is left out on purpose: with it on the include path cppcheck 2.7 (ubuntu 22.04) reports a syntaxError at the enum class in
# plug_host.h when it analyses plug_ui.cpp (found by bisecting the include dirs in CI), 2.22 does not. The headers there stay unresolved,
# which cppcheck tolerates.
CPPCHECK_INC = ['core/include', 'core/config', 'core/source', 'core/synth', 'core/libs', 'core/libs/Audio', 'core/libs/ElapsedMillis',
                'core/libs/SerialFlash', 'core/libs/SPI', 'core/libs/Audio/utility', 'core/libs/FT812', 'core/libs/fonts',
                'core/libs/NewDisplay', 'core/interface', 'core/effector', 'core/FileManager', 'core/Exporter',
                'core/AudioEngine/ConstantValues', 'core/AudioEngine/MasterEnhancer', 'util/include', 'util/3rdparty', 'synth/include',
                'synth-open', 'synth-open/ot', 'synth-open/plug']


def git(root, *args):
    r = subprocess.run(['git', '-C', root, '-c', 'core.quotepath=off'] + list(args), capture_output=True, text=True, errors='replace')
    if r.returncode != 0:
        sys.exit('lint_delta: git %s failed: %s' % (' '.join(args), r.stderr.strip()))
    return r.stdout


def added_lines(root, base):
    """{repo relative path: set of added line numbers} between the merge-base of BASE and the working tree."""
    mb = git(root, 'merge-base', base, 'HEAD').strip()
    out, cur = {}, None
    for line in git(root, 'diff', '-U0', '--no-color', '--no-ext-diff', '--diff-filter=AMR', mb).splitlines():
        if line.startswith('+++ '):
            cur = line[6:] if line.startswith('+++ b/') else None
            if cur is not None:
                out.setdefault(cur, set())
        else:
            m = HUNK.match(line)
            if m and cur is not None:
                start, n = int(m.group(1)), int(m.group(2) if m.group(2) is not None else 1)
                out[cur].update(range(start, start + n))
    for f in git(root, 'ls-files', '--others', '--exclude-standard').splitlines():
        try:
            with open(os.path.join(root, f), errors='replace') as fh:
                out[f] = set(range(1, sum(1 for _ in fh) + 1))
        except OSError:
            pass
    return mb, out


def norm(path, root, cwd_rel):
    if os.path.isabs(path):
        p = os.path.relpath(os.path.normpath(path), root)
    else:
        p = os.path.normpath(os.path.join(cwd_rel, path))
    return p.replace(os.sep, '/')


def failing(path, cls):
    if any(a in cls for a in ALWAYS):
        return True
    return cls in CONV and path.startswith(CONV_SCOPE)


def source_line(root, path, n):
    try:
        with open(os.path.join(root, path), errors='replace') as f:
            for i, line in enumerate(f, 1):
                if i == n:
                    return line.strip()
    except OSError:
        pass
    return ''


def read_allow(path):
    out = set()
    if path and os.path.exists(path):
        with open(path) as f:
            for line in f:
                line = line.rstrip('\n')
                if line and not line.startswith('#'):
                    p = line.split('\t', 2)
                    if len(p) == 3:
                        out.add((p[0], p[1], p[2].strip()))
    return out


def run_cppcheck(root, files, added):
    """[(path, line, id, message)] cppcheck hits on added lines, in the id allow list. Fails the run on unknownMacro/syntaxError."""
    cc = subprocess.run(['cppcheck', '--version'], capture_output=True, text=True)
    if cc.returncode != 0:
        print('lint delta: cppcheck is not installed, the cppcheck part is skipped')
        return [], []
    cmd = ['cppcheck', '--quiet', '-j', '4', '--enable=warning', '--inline-suppr', '--std=c++20', '--language=c++', '-D__NOINIT(x)=',
           '-DFEATURE_SYNTH', '-DFEATURE_SYNTH_OPEN', '-DFEATURE_PLUG_HOST', '-DFEATURE_SAT_SOFTKNEE', '-DFW_TRIM', '-DKINETISK', '-D__MK66FX1M0__',
           '--template={file}:{line}:{id}:{message}'] + ['-I' + os.path.join(root, i) for i in CPPCHECK_INC] + files
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=root, errors='replace')
    hits, bad = [], []
    for line in r.stderr.splitlines():
        p = line.split(':', 3)
        if len(p) != 4 or not p[1].isdigit():
            continue
        f = norm(p[0], root, '.')
        if p[2] in CPPCHECK_BAD:
            bad.append((f, int(p[1]), p[2], p[3]))
        elif p[2] in CPPCHECK_IDS and int(p[1]) in added.get(f, ()):
            hits.append((f, int(p[1]), p[2], p[3]))
    return hits, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', required=True)
    ap.add_argument('--base', default='origin/main')
    ap.add_argument('--allow')
    ap.add_argument('--cppcheck', action='store_true')
    ap.add_argument('--print-allow', action='store_true', help='print allow list entries for the current hits and exit 0')
    ap.add_argument('--lint-cwd', default='core', help='directory the lint compile ran in (relative warning paths are relative to it)')
    ap.add_argument('log', help='output of make lint-original')
    a = ap.parse_args()
    root = os.path.realpath(a.root)

    mb, added = added_lines(root, a.base)
    allow = read_allow(a.allow)
    seen, hits, allowed, total = set(), [], 0, 0
    with open(a.log, errors='replace') as f:
        for line in f:
            m = WARN.match(ANSI.sub('', line.rstrip('\n')))
            if not m:
                continue
            total += 1
            path, n, cls, msg = norm(m.group(1), root, a.lint_cwd), int(m.group(2)), m.group(5), m.group(4)
            if n not in added.get(path, ()) or not failing(path, cls):
                continue
            key = (path, n, cls, msg)
            if key in seen:
                continue
            seen.add(key)
            text = source_line(root, path, n)
            if (path, cls, text) in allow:
                allowed += 1
            else:
                hits.append((path, n, cls, msg, text))

    cc_hits, cc_bad = [], []
    if a.cppcheck:
        files = sorted(f for f, ls in added.items() if ls and f.endswith(('.cpp', '.cc', '.h', '.hpp')) and f.startswith(CPPCHECK_DIRS)
                       and not f.startswith(CPPCHECK_SKIP) and os.path.exists(os.path.join(root, f)))
        cc_hits, cc_bad = run_cppcheck(root, files, added)
        for f, n, i, msg in cc_hits[:]:
            text = source_line(root, f, n)
            if (f, 'cppcheck:' + i, text) in allow:
                cc_hits.remove((f, n, i, msg))
                allowed += 1

    if a.print_allow:
        for path, n, cls, msg, text in sorted(hits):
            print('%s\t%s\t%s' % (path, cls, text))
        for f, n, i, msg in sorted(cc_hits):
            print('%s\tcppcheck:%s\t%s' % (f, i, source_line(root, f, n)))
        return 0

    print('lint delta: base %s (merge-base %s), %d lint warnings, %d files with added lines, %d reviewed hits in the allow list'
          % (a.base, mb[:9], total, sum(1 for v in added.values() if v), allowed))
    rc = 0
    for path, n, cls, msg, text in sorted(hits):
        print('FAIL %s:%d: %s [%s]\n       %s' % (path, n, msg, cls, text))
        rc = 1
    for f, n, i, msg in sorted(cc_hits):
        print('FAIL %s:%d: cppcheck %s: %s' % (f, n, i, msg))
        rc = 1
    for f, n, i, msg in sorted(cc_bad):
        print('FAIL %s:%d: cppcheck could not analyse the file (%s: %s), add the missing define to CPPCHECK flags' % (f, n, i, msg))
        rc = 1
    if rc:
        print('lint delta: FAILED. Fix the line, or if it is a reviewed false positive add it to the allow list'
              ' (lint_delta.py --print-allow prints the entries).')
    else:
        print('lint delta: OK')
    return rc


if __name__ == '__main__':
    sys.exit(main())
