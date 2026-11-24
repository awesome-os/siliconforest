from pathlib import Path
import json, subprocess, tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SPAWN = ROOT / 'integrations/puredarwin/chonkstep-darwin/darwin-spawn'
def run(args):
    return subprocess.run(args, capture_output=True, text=True, timeout=30)

def main():
    with tempfile.TemporaryDirectory(prefix='desktop-owned-child-') as tmp:
        temp = Path(tmp)
        base = ['clang', '-std=c11', '-D_GNU_SOURCE', '-Wall', '-Wextra',
                '-Werror', '-Wpedantic', '-O1', '-DDNDS_CHILD_TEST',
                '-I'+str(HERE), '-I'+str(SPAWN)]
        source = (HERE/'owned_child.c').read_text()
        def build(text, name, sanitize=True):
            src, exe = temp/(name+'.c'), temp/name
            src.write_text(text)
            flags = ['-fsanitize=address,undefined'] if sanitize else []
            p = run(base+flags+[str(src), str(HERE/'test_owned_child.c'), '-o', str(exe)])
            if p.returncode:
                raise RuntimeError(p.stderr)
            return exe
        exe = build(source, 'baseline')
        p = run([str(exe)])
        if p.returncode:
            raise RuntimeError(p.stdout+p.stderr)
        mutants = {
            'forget-created-on-local-error': ('if(c->spawn.child_created&&c->spawn.pid>=2)', 'if(!rc&&c->spawn.child_created&&c->spawn.pid>=2)'),
            'lost-owner-may-signal': ('if(errno==ECHILD)c->lost=1;', 'if(errno==ECHILD)c->lost=0;'),
            'frontend-short-kill': ('kind==DNDS_FRONTEND?15000:2000', 'kind==DNDS_FRONTEND?2000:2000'),
            'kill-deadline-reversed': ('c->last-c->stop_at>=c->kill_after_ms', 'c->last-c->stop_at<=c->kill_after_ms'),
            'omit-pipe-cloexec': ('flags|FD_CLOEXEC', 'flags'),
            'ignore-close-failures': ('c->spawn.fd_close_failures||', '0||'),
            'ignore-attribute-destroy': ('c->spawn.attr_destroy_error||', '0||'),
            'ignore-reserved': ('c->spawn.reserved)', '0)'),
            'discard-output-overflow': ('bad(c,EFBIG);retire(c,fd);', 'retire(c,fd);'),
            'same-owner-can-start-again': ('c->initialized||!now', '0||!now'),
        }
        killed = []
        for name, (before, after) in mutants.items():
            if source.count(before)!=1:
                raise RuntimeError('nonunique mutation '+name)
            exe = build(source.replace(before, after), name)
            p = run([str(exe)])
            if p.returncode == 0 or 'Assertion' not in p.stderr:
                raise RuntimeError('mutation did not fail assertion: '+name+' '+p.stderr)
            killed.append(name)
        analyzer = run(['clang', '--analyze', '-std=c11', '-D_GNU_SOURCE',
                        '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-I'+str(HERE), '-I'+str(SPAWN), str(HERE/'owned_child.c'),
                        '-o', str(temp/'analysis.plist')])
        if analyzer.returncode or analyzer.stderr:
            raise RuntimeError(analyzer.stderr)
        print(json.dumps({'status':'PASS','baseline': '14 real-pipe lifecycle/fault scenarios',
                          'assertion_killed_mutants':killed,'static_analyzer':'CLEAN'},indent=2))

if __name__ == '__main__':
    main()
