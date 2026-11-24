"""workstation: M1-side TERM12 commands in ONE gui.lock hold, then fetch their log directories.
runm1.py <minutes> <hold-title> '<tool.py> <args...>' ...   (each tool's first argument is its run name = log dir)"""
import shlex, subprocess, sys, time
sys.path.insert(0, '/home/chrisk/src/darwin-lab/runs/sf-term12/tools')
import t12 as F
minutes, title = float(sys.argv[1]), sys.argv[2]; specs = [shlex.split(a) for a in sys.argv[3:]]
W = F.REPO / 'runs/sf-term12'
for attempt in range(6):
    try:
        with F.hold(minutes, title):
            for sp in specs:
                args = ' '.join(shlex.quote(x) for x in sp[1:])
                r = F.sh('/opt/homebrew/bin/python3 /private/var/db/sft12/tools/%s %s' % (sp[0], args), timeout=1200)
                print(sp[1], r.stdout[-2500:], r.stderr[-1500:], flush=True)
        break
    except TimeoutError:
        F.log('gui.lock wait timed out (600 s), queueing again')
for sp in specs:
    d = W / 'logs' / sp[1]; d.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(F.SSH + ['cd /private/var/db/sft12/logs && tar -cf - $(ls -d %s %s.json 2>/dev/null) && rm -rf %s %s.json' % (sp[1], sp[1], sp[1], sp[1])],
                       capture_output=True, timeout=600)
    subprocess.run(['tar', '-C', str(W / 'logs'), '-xf', '-'], input=r.stdout)
print('RUNM1-END')
