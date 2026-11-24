from pathlib import Path
import json,os,subprocess,tempfile
R=Path(__file__).resolve().parent;G=R/'guest';repo=R.parents[2]
def run(argv):
 a=subprocess.run(argv,capture_output=True,text=True,timeout=90)
 if a.returncode:raise RuntimeError(str(argv)+'\n'+a.stdout+a.stderr)
 return a.stdout.strip()
results={}
with tempfile.TemporaryDirectory(prefix='dlr-check-') as d:
 p=Path(d);common=['clang','-std=c11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-fsanitize=address,undefined','-I'+str(G),'-I'+str(repo/'integrations/puredarwin/chonkstep-darwin/darwin-spawn')]
 for group in ['probe_record','child_owner','endpoint_owner']:
  extra=[]
  if group=='child_owner':extra=['-DDLR_CHILD_TEST']
  if group=='endpoint_owner':extra=['-DDLR_ROOT='+json.dumps('/tmp/dlr-check-endpoint-'+str(os.getpid()))]
  target=p/group
  run(common+extra+[str(G/(group+'.c')),str(G/('test_'+group+'.c')),'-o',str(target)])
  results[group]=run((['sudo','-n'] if group=='endpoint_owner' else [])+[str(target)])
 results['service_transitions']=run(['python3',str(G/'test_service.py')])
 results['compiled_mutations']=json.loads(run(['python3',str(G/'test_mutations.py')]))
 for group in ['probe_record','child_owner','endpoint_owner']:
  results[group+'_analyzer']=run(['clang','--analyze','-std=c11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-Wpedantic','-I'+str(G),'-I'+str(repo/'integrations/puredarwin/chonkstep-darwin/darwin-spawn'),str(G/(group+'.c')),'-o',str(p/(group+'.plist'))]) or 'CLEAN'
print(json.dumps({'status':'PASS','results':results},indent=2))
