from pathlib import Path
import json,resource,subprocess,tempfile
G=Path(__file__).resolve().parent
cases=[('if(peer&&!output&&!input)','if(!output&&!input)','actual_peer_CLOSE'),('if(peer&&!output&&!input)','if(peer&&!input)','pending_final_ACK'),('if(line||bits||!end)','if(bits||!end)','partial_line'),('if(line||bits||!end)','if(line||!end)','partial_bits'),('now>=d->deadline','now>d->deadline','strict_deadline'),('now<d->last','0','monotonic_clock'),('input>16','input>17','queue_cap'),('d->completed=now','d->completed=0','actual_completion_clock'),('(budget!=15000&&budget!=40000)','0','fixed_budgets')]
def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
result=[]
with tempfile.TemporaryDirectory(prefix='dlr-close-mutants-') as d:
 p=Path(d)
 for before,after,label in cases:
  s=(G/'close_guard.c').read_text();assert s.count(before)==1;mutant=p/(label+'.c');mutant.write_text(s.replace(before,after));out=p/label
  a=subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-fsanitize=address,undefined','-I'+str(G),str(mutant),str(G/'test_close_guard.c'),'-o',str(out)],capture_output=True);assert not a.returncode,a.stderr.decode()
  a=subprocess.run([str(out)],capture_output=True,timeout=5,preexec_fn=no_core);assert a.returncode!=0,label;result.append({'name':label,'compiled':True,'detected':True,'returncode':a.returncode})
 # The actual closing-branch harness must detect stale loop time after lengthy observations.
 clone=p/'integration';clone.mkdir()
 for name in ['test_close_integration.py','close_guard.c','close_guard.h']:(clone/name).write_bytes((G/name).read_bytes())
 s=(G/'net_resolver.c').read_text();assert 'dlr_drain_poll(&s->drain,close_now,' in s;(clone/'net_resolver.c').write_text(s.replace('dlr_drain_poll(&s->drain,close_now,','dlr_drain_poll(&s->drain,now,'))
 a=subprocess.run(['python3',str(clone/'test_close_integration.py')],capture_output=True,timeout=8,preexec_fn=no_core);assert a.returncode!=0 and b'Assertion' in a.stderr;result.append({'name':'stale_parent_loop_time','compiled':True,'detected':True,'returncode':a.returncode})
print(json.dumps({'status':'PASS','mutants':result},indent=2))
