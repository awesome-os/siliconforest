"""workstation: the presented-fps set: <n> runs of each case (anim, scroll, video) of my own Firefox with one prefix and
env, 3 runs per gui.lock hold (short holds), then a summary: per run the steady 10 s windows' shown/s (all windows
after the first, which holds the start-up), the minimum, and whether >= 59.5.
paceset.py <tag> <prefix|-> <n> <KEY=VAL,...> [cases]"""
import json, subprocess, sys
sys.path.insert(0, '/home/chrisk/src/darwin-lab/runs/sf-term12/tools')
import t12 as F
tag, prefix, n, env = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
cases = sys.argv[5].split(',') if len(sys.argv) > 5 else ['anim', 'scroll', 'video']
W = F.REPO / 'runs/sf-term12'
env = env + (',' if env else '') + 'MEASURE=31'
out = {}
for case in cases:
    out[case] = []
    k = 0
    while k < n:
        m = min(3, n - k); name = '%s-%s-%d' % (tag, case, k)
        r = subprocess.run(['python3', str(W / 'tools/runm1.py'), '4', 'presented-fps set %s %s runs %d-%d (prefix %s)' % (tag, case, k, k + m - 1, prefix),
                            'pace12.py %s %s %s %d %s' % (name, prefix, case, m, env)], capture_output=True, text=True)
        try:
            R = json.load(open(W / 'logs' / (name + '.json')))
        except Exception as e:
            print('no result', name, r.stdout[-500:], r.stderr[-500:]); k += m; continue
        for run in R['runs']:
            shown = [round(p['shown'], 1) for p in run.get('pacing', [])]
            steady = [s for s in shown[1:] if s > 0][:2] if len(shown) > 1 else []
            row = dict(name=name, i=run['i'], shown=shown, steady=steady, min=min(steady) if steady else None,
                       ok=bool(steady) and min(steady) >= 59.5, error=run.get('error'), frames=run.get('frames', [])[-1:] if run.get('frames') else None)
            out[case].append(row); print(case, row['shown'], 'min', row['min'], 'OK' if row['ok'] else 'LOW', row['error'] or '', flush=True)
        k += m
    json.dump(out, open(W / 'logs' / (tag + '-set.json'), 'w'), indent=1)
for case, rows in out.items():
    good = sum(r['ok'] for r in rows)
    print('%s: %d/%d runs >= 59.5 (minima %s)' % (case, good, len(rows), [r['min'] for r in rows]))
