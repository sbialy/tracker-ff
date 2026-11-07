#!/usr/bin/env python3
"""Compares two golden sets checkpoint by checkpoint (host tool, NOT firmware). The use: goldens/mac against goldens/device, the
difference between the Mac front end and the unit is itself a test. Texts are compared exactly, tiles are counted (the Mac
render is not pixel exact with the FT812, see tools/ui_shot/README), so a tile difference is a measure, text equality is the check.

  python3 -I golden_diff.py goldens/mac goldens/device [--md FILE]

Exit status 0 when every checkpoint present in both has the same texts."""
import json
import os
import sys


def load(d):
    out = {}
    for f in sorted(os.listdir(d)):
        if f.endswith('.json') and not f.startswith('_'):
            with open(os.path.join(d, f)) as fh:
                for k, v in json.load(fh)['checkpoints'].items():
                    out[(f[:-5], k)] = v
    return out


def main(argv):
    args = [a for a in argv if not a.startswith('--')]
    md = argv[argv.index('--md') + 1] if '--md' in argv else None
    if md:
        args.remove(md)
    if len(args) != 2:
        print(__doc__)
        return 2
    a, b = load(args[0]), load(args[1])
    rows, bad = [], 0
    for key in sorted(set(a) & set(b)):
        va, vb = a[key], b[key]
        ma, mb = set(va.get('mask', [])), set(vb.get('mask', []))
        va_vol, vb_vol = set(va.get('volatile', [])), set(vb.get('volatile', []))
        same_texts = len(va['texts']) == len(vb['texts']) and all(
            va['texts'][i] == vb['texts'][i] for i in range(len(va['texts'])) if i not in va_vol and i not in vb_vol)
        tiles = [i for i in range(48) if i not in ma and i not in mb and va['tiles'][i] != vb['tiles'][i]]
        if not same_texts:
            bad += 1
        rows.append((key, same_texts, len(tiles)))
    lines = ['| Scenario | Checkpoint | Texts | Tiles differing |', '| :--- | :--- | :--- | ---: |']
    for (scn, cp), ok, n in rows:
        lines.append('| %s | %s | %s | %d |' % (scn, cp, 'same' if ok else '**DIFFERENT**', n))
    only_a, only_b = sorted(set(a) - set(b)), sorted(set(b) - set(a))
    lines += ['', '%d checkpoints in both, %d with different texts, %d only in %s, %d only in %s' % (len(rows), bad, len(only_a), args[0], len(only_b), args[1])]
    text = '\n'.join(lines)
    print(text)
    if md:
        with open(md, 'w') as f:
            f.write(text + '\n')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
