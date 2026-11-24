from pathlib import Path
import json,subprocess,tempfile
R=Path(__file__).resolve().parent;G=R/'guest'
def run(argv):
 p=subprocess.run(argv,capture_output=True,text=True,timeout=90)
 if p.returncode:raise RuntimeError(str(argv)+'\n'+p.stdout+p.stderr)
 return p.stdout.strip()
with tempfile.TemporaryDirectory(prefix='dlr-drain-check-') as d:
 p=Path(d)
 run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-fsanitize=address,undefined',str(G/'close_guard.c'),str(G/'test_close_guard.c'),'-o',str(p/'test')])
 result={'guard':run([str(p/'test')]),'actual_parent_branches':run(['python3',str(G/'test_close_integration.py')]),'mutants':json.loads(run(['python3',str(G/'test_mutations.py')])),'analyzer':run(['clang','--analyze','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic',str(G/'close_guard.c'),'-o',str(p/'analysis.plist')]) or 'CLEAN'}
print(json.dumps({'status':'PASS','results':result},indent=2))
