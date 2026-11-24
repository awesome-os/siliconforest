"""Compiled source mutants run real host tests; never modifies production files."""
from pathlib import Path
import json,os,resource,subprocess,tempfile
G=Path(__file__).resolve().parent
REPO=G.parents[3]
CASES=[
 ('probe_record.c','actual!=(uint32_t)pid','0','pid_binding'),
 ('probe_record.c','result.count==DLR_ADDRESS_CAP','0','address_cap'),
 ('probe_record.c','if(!text(&r,"]}\\n")||r.n)','if(!text(&r,"]}\\n"))','trailing_bytes'),
 ('probe_record.c','if((zero&&count)||d>high||n>(high-d)/10)','if(d>high||n>(high-d)/10)','noncanonical_address'),
 ('child_owner.c','if(errno==ECHILD)c->lost=1','if(errno==ECHILD)c->lost=0','lost_ownership'),
 ('child_owner.c','if(c->created)dlr_child_stop(c,now)','if(0)dlr_child_stop(c,now)','failed_spawn_still_owned'),
 ('child_owner.c','c->last-c->stop_at>=2000','c->last-c->stop_at>=2001','kill_deadline'),
 ('child_owner.c','c->spawn.child_created&&c->spawn.pid>=2','c->spawn.pid>=2','spawn_without_created'),
 ('child_owner.c','bad(c,EFBIG)','bad(c,EIO)','output_cap_classification'),
 ('endpoint_owner.c','i->inode==(uint64_t)s->st_ino&&','', 'inode_ownership'),
 ('endpoint_owner.c','||!absent(PIDFILE)||','||', 'preexisting_pid_refused'),
 ('endpoint_owner.c','if(!pid_contents(pid,&p))','if(0)', 'pid_identity'),
 ('endpoint_owner.c','||!absent(DDNS)','', 'ddns_config_refused'),
 ('endpoint_owner.c','&&!(st->st_mode&0022)','', 'config_permissions'),
]
def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
results=[]
with tempfile.TemporaryDirectory(prefix='dlr-mutants-') as d:
 root=Path(d)
 for index,(filename,before,after,label) in enumerate(CASES):
  text=(G/filename).read_text();assert text.count(before)==1,(label,text.count(before));source=root/f'{label}.c';source.write_text(text.replace(before,after));binary=root/label
  group='probe_record' if filename.startswith('probe') else 'child_owner' if filename.startswith('child') else 'endpoint_owner'
  command=['clang','-std=c11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-Wpedantic','-Wno-unused-variable','-Wno-unused-but-set-variable','-O1','-fsanitize=address,undefined','-I'+str(G),'-I'+str(REPO/'integrations/puredarwin/chonkstep-darwin/darwin-spawn')]
  if group=='child_owner':command+=['-DDLR_CHILD_TEST']
  endpoint=f'/tmp/dlr-mutant-{os.getpid()}-{index}'
  if group=='endpoint_owner':command+=['-DDLR_ROOT='+json.dumps(endpoint)]
  command +=[str(source),str(G/f'test_{group}.c'),'-o',str(binary)]
  build=subprocess.run(command,capture_output=True);assert build.returncode==0,(label,build.stderr.decode())
  run=subprocess.run((['sudo','-n'] if group=='endpoint_owner' else [])+[str(binary)],capture_output=True,timeout=8,preexec_fn=no_core)
  assert run.returncode!=0,(label,'survived');results.append({'name':label,'compiled':True,'detected':True,'returncode':run.returncode})
  # Root fixtures created by the test are exclusively ours, with private paths.
  if group=='endpoint_owner' and Path(endpoint).exists():subprocess.run(['sudo','-n','rm','-rf','--',endpoint],check=True)
print(json.dumps({'status':'PASS','mutants':results},indent=2))
