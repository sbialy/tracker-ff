"""Statistics and the side by side report of ab_run.py. Pure functions, no device access. Host tool, NOT firmware.

A measurement is one CPU reading of the sweep:
    {'flavour': 'A', 'phase': 0, 'repeat': 1, 'engine': 'ACD', 'voices': 4, 'avg_us': 812, 'max_us': 1500}
A suite run of one phase:
    {'flavour': 'A', 'phase': 0, 'results': [{'name': 'pages.walk', 'status': 'PASS', 'error': None, 'failed': ['text of a failed check']}]}
"""
import statistics

PASS_LIKE = ('PASS', 'RECORDED')
FAIL_LIKE = ('FAIL', 'ERROR')


def stats(values):
    if not values:
        return None
    return {'n': len(values), 'min': min(values), 'avg': round(statistics.fmean(values), 1), 'max': max(values)}


def cpu_rows(measurements, flavours=('A', 'B')):
    """One row per (engine, voices) in the order the sweep met them. avg_us is the median of 3 readings of one measurement (Ctx.cpu_measure),
    so min/avg/max are over the repeats (and over the phases of that flavour in an ABAB run). max_us is the worst block seen at all.
    delta = B - A of the averages, negative means B is cheaper. A delta is 'clear' when it is larger than the wider of the two spreads,
    otherwise 'within noise'. pair_deltas: per pair k of an ABAB run the average of B minus the average of A of that pair (drift cancels)."""
    order, by = [], {}
    for m in measurements:
        key = (m['engine'], m['voices'])
        if key not in by:
            order.append(key)
            by[key] = {f: [] for f in flavours}
        by[key][m['flavour']].append(m)
    rows = []
    for key in order:
        engine, voices = key
        row = {'engine': engine, 'voices': voices}
        for f in flavours:
            ms = by[key][f]
            s = stats([m['avg_us'] for m in ms])
            if s:
                s['spread'] = s['max'] - s['min']
                s['worst_block_us'] = max(m['max_us'] for m in ms)
            row[f] = s
        a, b = row[flavours[0]], row[flavours[1]]
        if a and b:
            row['delta_us'] = round(b['avg'] - a['avg'], 1)
            row['delta_pct'] = round(100.0 * (b['avg'] - a['avg']) / a['avg'], 1) if a['avg'] else None
            noise = max(a['spread'], b['spread'])
            if abs(row['delta_us']) <= noise:
                row['verdict'] = 'within noise' if row['delta_us'] != 0 else 'same'
            else:
                row['verdict'] = '%s cheaper' % (flavours[1] if row['delta_us'] < 0 else flavours[0])
            row['pair_deltas'] = pair_deltas(by[key], flavours)
        else:
            row.update(delta_us=None, delta_pct=None, verdict='n/a', pair_deltas=[])
        rows.append(row)
    return rows


def pair_deltas(per_flavour, flavours):
    """{'A': [measurements], 'B': [...]} -> [B_avg - A_avg for every pair index that has both]"""
    avg = {}
    for f in flavours:
        for m in per_flavour[f]:
            if 'pair' in m:
                avg.setdefault((f, m['pair']), []).append(m['avg_us'])
    out = []
    pairs = sorted({p for (_, p) in avg})
    for p in pairs:
        if (flavours[0], p) in avg and (flavours[1], p) in avg:
            out.append(round(statistics.fmean(avg[(flavours[1], p)]) - statistics.fmean(avg[(flavours[0], p)]), 1))
    return out


def totals(rows, voices, flavours=('A', 'B')):
    """Sum of the average CPU over every engine at one voice count, for the headline."""
    sel = [r for r in rows if r['voices'] == voices and r[flavours[0]] and r[flavours[1]]]
    if not sel:
        return None
    a = round(sum(r[flavours[0]]['avg'] for r in sel), 1)
    b = round(sum(r[flavours[1]]['avg'] for r in sel), 1)
    return {'voices': voices, 'engines': len(sel), flavours[0]: a, flavours[1]: b, 'delta_us': round(b - a, 1),
            'delta_pct': round(100.0 * (b - a) / a, 1) if a else None}


def scenario_rows(suites, flavours=('A', 'B')):
    """Per scenario the statuses on each flavour (one per suite run) and a verdict:
    DIFFERENT when one flavour passes it (every run) and the other fails it, flaky when a flavour gives both, same otherwise."""
    names, st, info = [], {}, {}
    for s in suites:
        for r in s['results']:
            if r['name'] not in st:
                names.append(r['name'])
                st[r['name']] = {f: [] for f in flavours}
                info[r['name']] = {f: [] for f in flavours}
            st[r['name']][s['flavour']].append(r['status'])
            if r['status'] in FAIL_LIKE:
                info[r['name']][s['flavour']].append(r.get('error') or ' / '.join(r.get('failed') or []) or r['status'])
    rows = []
    for n in names:
        a, b = st[n][flavours[0]], st[n][flavours[1]]

        def kind(lst):
            if not lst:
                return 'none'
            ok, bad = any(x in PASS_LIKE for x in lst), any(x in FAIL_LIKE for x in lst)
            return 'mixed' if ok and bad else 'fail' if bad else 'pass' if ok else 'neutral'
        ka, kb = kind(a), kind(b)
        if {ka, kb} == {'pass', 'fail'}:
            verdict = 'DIFFERENT'
        elif 'mixed' in (ka, kb):
            verdict = 'flaky'
        elif ka == 'none' or kb == 'none':
            verdict = 'one flavour only'
        else:
            verdict = 'same'
        rows.append({'name': n, flavours[0]: a, flavours[1]: b, 'verdict': verdict,
                     'why': {f: info[n][f][:1] for f in flavours if info[n][f]}})
    return rows


def _esc(s):
    return str(s).replace('|', '\\|').replace('\n', ' ')


def _fmt(s):
    if not s:
        return '-'
    return '%s / %s / %s' % (s['min'], s['avg'], s['max'])


def render(rep):
    """The markdown of a report dict (see ab_run.py: meta, flavours, phases, cpu, totals, scenarios, crashes, restore)."""
    fl = rep['flavours']
    ka, kb = rep['keys']
    L = ['# A/B run: %s against %s' % (fl[ka]['label'], fl[kb]['label']), '',
         '- result: **%s**' % rep['result'],
         '- target: %s, started %s, %s s' % (rep['meta']['target'], rep['meta']['started'], rep['meta']['seconds']),
         '- sequence: %s (%d pair%s), sweep %d repeat%s per phase, suite %s' % (
             ' '.join(rep['meta']['sequence']), rep['meta']['pairs'], '' if rep['meta']['pairs'] == 1 else 's',
             rep['meta']['sweep_repeats'], '' if rep['meta']['sweep_repeats'] == 1 else 's', rep['meta']['suite']),
         '- command: `%s`' % _esc(rep['meta']['command'])]
    if rep.get('abort'):
        L += ['', '## ABORTED', '', '**%s**' % rep['abort']['reason'], '',
              'Nothing was measured after this point. Phase: %s.' % rep['abort'].get('phase', '-')]
    L += ['', '## Flavours', '', '| | label | .ptf | build id | bytes | crc32 | plugin files |', '| :--- | :--- | :--- | :--- | ---: | :--- | ---: |']
    for k in (ka, kb):
        f = fl[k]
        L.append('| %s | %s | `%s` | %s | %d | %08x | %d |' % (k, f['label'], f['ptf_name'], f['build'], f['ptf_bytes'], f['ptf_crc'], f['plugin_files']))
    d = rep['plugin_diff']
    L += ['', 'Plugin sets: %d files differ between the two, %d are identical (%s).' % (len(d['differ']), len(d['same']),
          ', '.join(d['differ'][:20]) + (' ...' if len(d['differ']) > 20 else '') if d['differ'] else 'nothing differs, the A/B isolates the firmware only'), '']

    L += ['## Phases', '', '| # | flavour | pair | update | plugin swap | verified | suite | sweep | markers |', '| ---: | :--- | ---: | :--- | :--- | :--- | :--- | :--- | :--- |']
    for p in rep['phases']:
        L.append('| %d | %s | %d | %s | %s | %s | %s | %s | %s |' % (
            p['index'], p['flavour'], p['pair'], _esc(p.get('update', '-')), _esc(p.get('swap', '-')), _esc(p.get('verified', '-')),
            _esc(p.get('suite', '-')), _esc(p.get('sweep', '-')), _esc(p.get('markers', '-'))))

    L += ['', '## CPU per engine', '',
          'Audio block CPU in microseconds (the block budget is 2902 us). The average of one measurement is the median of 3 readings, min / avg / max are over the',
          'repeats%s. Delta is %s minus %s: negative means %s is cheaper. A delta larger than the wider spread is clear, otherwise it is within noise.' % (
              ' and over the phases of the flavour', kb, ka, kb), '',
          '| engine | voices | %s min / avg / max | %s min / avg / max | delta us | delta %% | spread %s | spread %s | worst block %s | worst block %s | verdict |' % (ka, kb, ka, kb, ka, kb),
          '| :--- | ---: | :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- |']
    for r in rep['cpu']:
        a, b = r[ka], r[kb]
        L.append('| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |' % (
            r['engine'], r['voices'], _fmt(a), _fmt(b), r['delta_us'] if r['delta_us'] is not None else '-',
            r['delta_pct'] if r['delta_pct'] is not None else '-', a['spread'] if a else '-', b['spread'] if b else '-',
            a['worst_block_us'] if a else '-', b['worst_block_us'] if b else '-', r['verdict']))
    if not rep['cpu']:
        L.append('| (no CPU measurements) | | | | | | | | | | |')
    if rep['totals']:
        L += ['', '**Sum over the engines**: ' + ' / '.join('%d voice%s: %s %s us, %s %s us, delta %s us (%s %%)' % (
            t['voices'], '' if t['voices'] == 1 else 's', ka, t[ka], kb, t[kb], t['delta_us'], t['delta_pct']) for t in rep['totals'])]
    pd = [r for r in rep['cpu'] if r.get('pair_deltas') and len(r['pair_deltas']) > 1]
    if pd:
        L += ['', 'Pair deltas (%s minus %s per ABAB pair, drift cancels): ' % (kb, ka) + ' / '.join(
            '%s v%s %s' % (r['engine'], r['voices'], r['pair_deltas']) for r in pd[:40])]
    if rep.get('cpu_missing'):
        L += ['', 'Not measured: ' + ' / '.join(_esc(x) for x in rep['cpu_missing'][:20])]

    diff = [r for r in rep['scenarios'] if r['verdict'] in ('DIFFERENT', 'flaky', 'one flavour only')]
    L += ['', '## Scenarios', '']
    if not rep['scenarios']:
        L.append('The suite did not run.')
    else:
        same = sum(1 for r in rep['scenarios'] if r['verdict'] == 'same')
        L.append('%d scenarios, %d give the same result on both flavours, %d differ or are flaky.' % (len(rep['scenarios']), same, len(diff)))
        if diff:
            L += ['', '| scenario | %s | %s | verdict | first failure |' % (ka, kb), '| :--- | :--- | :--- | :--- | :--- |']
            for r in diff:
                why = ' / '.join('%s: %s' % (f, _esc(v[0])[:160]) for f, v in r['why'].items())
                L.append('| %s | %s | %s | **%s** | %s |' % (r['name'], ' '.join(r[ka]) or '-', ' '.join(r[kb]) or '-', r['verdict'], why or '-'))
        L += ['', '<details><summary>every scenario</summary>', '', '| scenario | %s | %s | verdict |' % (ka, kb), '| :--- | :--- | :--- | :--- |']
        for r in rep['scenarios']:
            L.append('| %s | %s | %s | %s |' % (r['name'], ' '.join(r[ka]) or '-', ' '.join(r[kb]) or '-', r['verdict']))
        L += ['', '</details>']

    L += ['', '## Crash markers', '']
    if not rep['crashes']:
        L.append('No fault record, no stale loading marker and no safe mode on either flavour, at any check.')
    else:
        L += ['| phase | flavour | when | marker |', '| ---: | :--- | :--- | :--- |']
        for c in rep['crashes']:
            L.append('| %s | %s | %s | %s |' % (c['phase'], c['flavour'], _esc(c['when']), _esc(c['text'])))
    if rep.get('restore'):
        L += ['', '## Restore', '', rep['restore']]
    if rep.get('notes'):
        L += ['', '## Notes', ''] + ['- ' + _esc(n) for n in rep['notes']]
    return '\n'.join(L) + '\n'
