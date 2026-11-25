#!/usr/bin/env python3
"""Ratchet on our feature conditionals in stock files (move 4 of ref/arch-longterm-proposal.md). Host tool, NOT firmware.

  python3 -I ff_ifdef_ratchet.py [--update] [--list]

Counts the preprocessor conditional lines (#if, #ifdef, #ifndef, #elif) that name one of our flags (FEATURE_* other than the stock FEATURE_SYNTH,
FW_*, UI_SHOT_HOST) in the stock files: the files under core/ and platforms/ that the upstream merge base already had (the base and the
count are in ff_ifdef_baseline.txt). Our own new files are not counted, a feature in its own file is the goal. The count may only go down:
a stock file gets a hook line (core/include/ff_hooks.h), not a new #ifdef block. Exit 0 at or under the baseline, 1 over it.
--update writes the current count (after a conversion lowered it). --list prints the per file counts.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
BASELINE = os.path.join(HERE, 'ff_ifdef_baseline.txt')
COND = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif)\b(.*)$')
OURS = re.compile(r'\b(FEATURE_(?!SYNTH\b)[A-Z0-9_]+|FW_[A-Z0-9_]+|UI_SHOT_HOST)\b')
EXT = ('.c', '.cc', '.cpp', '.h', '.hpp', '.inc')


def main():
    upd = '--update' in sys.argv
    with open(BASELINE) as f:
        fields = f.read().split()
    base, limit = fields[0], int(fields[1])
    names = subprocess.run(['git', '-C', ROOT, 'ls-tree', '-r', '--name-only', base, '--', 'core', 'platforms'],
                           capture_output=True, text=True, check=True).stdout.split()
    per = {}
    for n in names:
        p = os.path.join(ROOT, n)
        if not n.endswith(EXT) or not os.path.isfile(p):
            continue
        with open(p, encoding='latin-1') as fh:
            c = sum(1 for ln in fh if (m := COND.match(ln)) and OURS.search(m.group(2)))
        if c:
            per[n] = c
    total = sum(per.values())
    if '--list' in sys.argv:
        for n, c in sorted(per.items(), key=lambda kv: -kv[1]):
            print('%5d %s' % (c, n))
    if upd:
        with open(BASELINE, 'w') as f:
            f.write('%s %d\n' % (base, total))
        print('ff_ifdef_ratchet: baseline %d -> %d (%d stock files)' % (limit, total, len(per)))
        return 0
    if total > limit:
        print('ff_ifdef_ratchet: FAIL %d conditional lines of our flags in %d stock files, baseline %d: use a hook (core/include/ff_hooks.h)'
              % (total, len(per), limit))
        return 1
    print('ff_ifdef_ratchet: PASS %d conditional lines of our flags in %d stock files (baseline %d%s)'
          % (total, len(per), limit, ', run --update to lock the gain in' if total < limit else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
