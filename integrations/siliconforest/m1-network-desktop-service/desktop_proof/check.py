#!/usr/bin/env python3
"""Actual C closed-proof composition over private synthetic filesystem records."""
import copy,hashlib,json,os,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
BASE=HERE.parent
ROOT=HERE.parents[3]
NONCE='0123456789abcdef'*3
GATES=['Admission','StatusNonblocking','SpawnReturned','PidOwned','AppReady','AppCleanup','StatusEof','AppReaped','TermAttempt','KillAttempt']
TIMES=[11,12,13,14,20,100,101,102,99,None]
FLAGS=['-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1']
SOURCES=[HERE/'desktop_proof.c',HERE/'manager_proof.c',BASE/'record_guard/read.c',BASE/'record_guard/json.c',BASE/'session/revisions/002/session_text.c',BASE.parent/'m1-network-egress-guest/codec/egress_validate.c',HERE/'test_driver.c']
PROFILE='desktop-native-resolver-two-http-v1'
def run(args,okay=True,env=None):
 p=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=60,env=env)
 if okay and p.returncode:raise RuntimeError(f'{args}: {p.returncode}\n{p.stdout}\n{p.stderr}')
 return p
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def dumps(v):return (json.dumps(v,separators=(',',':'))+'\n').encode()
def records():
 out={};snap='CHONKDESKTOP/1 initialized=1 input=both shell=shared native_runtime_qualification=not_inferred\nCHONKDESKTOP/1 elapsed_ms=200 cleanup=Complete deadline=false frame_error=None manager_failed=false\nCHONKLAUNCH/1 snapshot attempts=2 journal_error=None app_internal_gates=unknown_until_final_app_record\n';events=[]
 for slot in [1,2]:
  inst=f'{slot:04d}'+NONCE[:44];app=100+slot;shell=200+slot;client=300+slot;a=slot-1
  status=f'CHONKTERM/1 READY {inst} {app}\nCHONKTERM/1 CLEANUP {inst} {app} 1 9 1 1\n'
  created=10 if slot==1 else 30;times=TIMES if slot==1 else [31,32,33,34,40,100,101,102,99,None]
  cleanup='Some(Cleanup { complete: true, raw_wait_status: 9, term_sent: true, kill_sent: true })'
  native=f'SpawnResultV1 {{ version: 1, stage: 10, pid: {app}, primary_error: 0, spawn_error: 0, attr_destroy_error: 0, actions_destroy_error: 0, fd_close_error: 0, fd_close_failures: 0, requested_flags: 16398, observed_flags: 16398, child_created: 1, duplicate_count: 4, reserved: 0 }}'
  snap+=f'attempt={a} source=explicit_startup request=Some({slot}) instance=Some("{inst}") pid=Some({app}) created_ms={created} failure=None failure_ms=None status_bytes={len(status)} wait=Some(0) wait_error=None cleanup={cleanup} native_return=Some(0) native=Some({native})\n'
  snap+=f'attempt={a} exit_reason=query_failed errno=2\nattempt={a} cs_at_spawn=flags=0x20020001 cs_at_exit=query_failed errno=3\nattempt={a} kernel_messages=not_observed\n'
  for g,t in zip(GATES,times):snap+=f'attempt={a} gate={g} observed_ms='+('None' if t is None else f'Some({t})')+'\n'
  def event(time,line):events.append((time,len(events),line+'\n'))
  event(created,f'CHONKLAUNCH/1 attempt={a} ms={created} request=Some({slot}) instance=Some("{inst}") source=explicit_startup event=requested')
  for g,t in sorted(zip(GATES,times),key=lambda x:x[1] or 1000):
   if t is None:continue
   event(t,f'CHONKLAUNCH/1 attempt={a} ms={t} event={g}')
   if g=='SpawnReturned':event(t,f'CHONKLAUNCH/1 attempt={a} native_return=0 native={native}')
   if g=='AppReady':event(t,f'CHONKLAUNCH/1 attempt={a} status_bytes={len(status.splitlines(keepends=True)[0])} raw_app_wait=None wait_error=None cleanup=None')
   if g=='AppReaped':event(t,f'CHONKLAUNCH/1 attempt={a} status_bytes={len(status)} raw_app_wait=Some(0) wait_error=None cleanup={cleanup}')
  marker=f'DL30_SHELL_RETURN session={NONCE} instance={inst} shell_pid={shell} client_pid={client} status=0\n'
  rx=f'DL30_CLIENT_BEGIN session={NONCE} instance={inst} pid={client} shell_pid={shell}\r\nDL30_CLIENT_DONE session={NONCE} instance={inst} pid={client} status=0\r\n'.encode()+marker.replace('\n','\r\n').encode()+b'$ \x1b[?2004h';tx=b'normal typing'
  boundary={'pre_exec_fd_check_observed':True,'pre_exec_fd_scan_bound_exclusive':10240,'exec_status_pipe_eof_observed':True,'shell_exec_checked':True,'inventory_scope':'pre_exec_only_not_after_exec_inventory'}
  startup={'version':1,'profile':PROFILE,'session_nonce':NONCE,'instance':inst,'slot':slot,'shell_pid':shell,'client_pid':client,'client_record_complete':True,'client_success':True,'shell_return_observed':True,'client_exit_status':0,'client_reaped_by_shell':True,'fallback_prompt_observed':True,'foreground_pgid':shell,'foreground_matches_shell':True,'complete':True,'error':'none'}
  result={'schema':1,'instance':inst,'app_pid':app,'shell':'/bin/sh','term':'dumb','child_pid':shell,'child_exec_boundary':boundary,'session':{'configure_serial':1,'ready':True,'keymap':True,'pty_exec_checked':True,'exec_boundary':boundary,'ssd':True,'frames':100,'commits':101,'pty_rx_bytes':len(rx),'pty_tx_bytes':len(tx),'input_accepted_bytes':len(tx),'input_unsent_bytes':0,'unsupported_terminal_sequences':1,'stop_cause':'deadline','term_signal':False,'window_closed':False,'pty_eof':False},'cleanup':{'complete':True,'status':9,'term_sent':True,'kill_sent':True,'error':None},'errors':[],'startup':startup}
  request=f'GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: darwin-lab-egress/1\r\nAccept-Encoding: identity\r\nX-Darwin-Lab-Nonce: {NONCE}\r\nConnection: close\r\n\r\n'.encode()
  response=b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\n\r\nExample Domain'
  clientdoc={'version':1,'profile':PROFILE,'nonce':NONCE,'instance':inst,'slot':slot,'pid':client,'ppid':shell,'sid':shell,'pgid':shell,'foreground_pgid':shell,'status':'PASS','error':'none','error_errno':0,'gai_rc':0,'system_errno':0,'address_list_complete':True,'addresses':['93.184.216.34','93.184.216.34'],'selected':'93.184.216.34','local':{'address':'10.0.2.15','port':50000+slot},'peer':{'address':'93.184.216.34','port':80},'entry_fd_check_observed':True,'entry_fd_scan_bound_exclusive':10240,'tcp_maxseg_request':256,'tcp_maxseg_observed':256,'request_hex':request.hex(),'response_hex':response.hex(),'request_bytes':len(request),'response_bytes':len(response),'request_shutdown':True,'peer_eof':True,'socket_created':True,'socket_closed':True,'http':{'status':200,'header_bytes':response.index(b'\r\n\r\n')+4,'body_bytes':14,'framing':1,'framing_complete':True,'eof_observed':True,'example_content':True},'started_ms':1000,'elapsed_ms':3000}
  files={'result.json':dumps(result),'child-ready.json':dumps({'parent_pid':app,'child_pid':shell,'sid':shell,'pgid':shell,'session_and_ctty_checked_in_child':True,'exec_status_pipe_closed':True,'exec_boundary':boundary,'shell':'/bin/sh'}),'child.json':dumps({'parent_pid':app,'child_pid':shell,'setup_checked':False,'job_control':'disabled; foreground group must remain owned shell'}),'network-client.json':dumps(clientdoc),'network-client.pid':f'{client}\n'.encode(),'network-shell-return.txt':marker.encode(),'pty-rx.bin':rx,'pty-tx.bin':tx,'grid-final.txt':b'\n'.join([b' '*80 for _ in range(24)]),'application-stdout.log':b'','application-stderr.log':b''}
  out.update({inst+'/'+k:v for k,v in files.items()})
 out['session-result.txt']=snap.encode();out['manager-status.log']=('CHONKLAUNCH/1 scope=manager app_internal_gates=unknown_until_final_app_record\n'+''.join(e[2] for e in sorted(events))).encode();return out

def main():
 report={'scope':'Synthetic closed records through actual C adapter and real host filesystem; no target execution','cases':[],'mutants':[]}
 pins={
  'record_guard/source-validation-001.json':'2a8976145591d2b5bbbba0390a75e2dd275287cc6a01915eaea1b77175dd186b',
  'session/frontend-binding-002.json':'965aba83b00122b382065306ac8e3b6e98646988ae7bb7e96b77a0477959990f',
  'revisions/002/client-source-validation-001.json':'746c298a0213d49398dc4588af6c52656df51c7710245fffb168616c2d83a09f',
  'revisions/002/terminal-source-validation-002.json':'fa6f13a447c07846ee466f613eaa4f7b0ff2a751106286da7f989668b691a1a5',
 }
 for path,digest in pins.items():
  receipt=BASE/path
  if sha(receipt)!=digest:raise RuntimeError('Frozen dependency receipt changed: '+path)
  for name,digest in json.loads(receipt.read_text()).get('files_sha256',{}).items():
   if sha(receipt.parent/name)!=digest:raise RuntimeError('Frozen dependency source changed: '+name)
 report['frozen_receipts']=pins

 with tempfile.TemporaryDirectory(prefix='desktop-proof-') as tmp:
  temp=Path(tmp);exe=temp/'proof';root=temp/'root';root.mkdir(mode=0o755)
  current=root
  for part in ['private','var','run']:
   current=current/part;current.mkdir(mode=0o755)
  current=current/'darwin-desktop-network';current.mkdir(mode=0o700);session=current/('session-'+NONCE);session.mkdir(mode=0o700)
  for slot in [1,2]:(session/(f'{slot:04d}'+NONCE[:44])).mkdir(mode=0o700)
  run(['clang',*FLAGS,'-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DDNDS_PROOF_HOST_TEST','-DRG_TEST_ONLY',*SOURCES,'-lcrypto','-o',exe])
  env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
  original=records()
  def install(data):
   for k,v in data.items():
    p=session/k
    if p.exists() or p.is_symlink():p.unlink()
    p.write_bytes(v);p.chmod(0o600)
  install(original)
  def test(name,expected=0,program=exe):
   run([program,root,expected],env=env);report['cases'].append(name)
  test('complete-with-normal-PTY-typing',1)
  app='0001'+NONCE[:44]+'/'
  # Every admitted required JSON scalar is independently removed or wrong-typed.
  for file in ['result.json','child-ready.json','child.json','network-client.json']:
   doc=json.loads(original[app+file])
   def leaves(v,path=()):
    if isinstance(v,dict):
     for k,x in v.items():yield from leaves(x,path+(k,))
    else:yield path,v
   for path,value in leaves(doc):
    for mode in ['missing','wrong-type']:
     changed=copy.deepcopy(doc);node=changed
     for key in path[:-1]:node=node[key]
     if mode=='missing':del node[path[-1]]
     else:node[path[-1]]={'unexpected':1}
     (session/(app+file)).write_bytes(dumps(changed));test(file+'/'+'.'.join(path)+'/'+mode)
   (session/(app+file)).write_bytes(original[app+file])
  for file in list(original):
   p=session/file;raw=original[file];p.unlink();test('missing/'+file);p.write_bytes(raw);p.chmod(0o600)
   p.chmod(0o644);test('mode/'+file);p.chmod(0o600)
  modifications=[('network-client.pid',b'0301\n'),('network-shell-return.txt',original[app+'network-shell-return.txt'].replace(b'status=0',b'status=1')),('pty-rx.bin',b'no marker'),('pty-tx.bin',b'wrong-count'),('grid-final.txt',b''),('application-stdout.log',b'error'),('application-stderr.log',b'error')]
  for file,b in modifications:
   (session/(app+file)).write_bytes(b);test('raw/'+file);(session/(app+file)).write_bytes(original[app+file])
  for file in ['session-result.txt','manager-status.log']:
   raw=original[file]
   for pos in range(0,len(raw),83):
    (session/file).write_bytes(raw[:pos]);test(f'truncated/{file}/{pos}')
   (session/file).write_bytes(raw)
  semantic=[]
  def change(file,path,value):
   data=dict(original);doc=json.loads(data[app+file]);node=doc
   for key in path[:-1]:node=node[key]
   node[path[-1]]=value;data[app+file]=dumps(doc);return data
  for name,file,path,value in [
   ('client-nonzero','result.json',('startup','client_exit_status'),1),
   ('unreaped-client','result.json',('startup','client_reaped_by_shell'),False),
   ('no-prompt','result.json',('startup','fallback_prompt_observed'),False),
   ('wrong-foreground','result.json',('startup','foreground_pgid'),301),
   ('app-cleanup-failed','result.json',('cleanup','complete'),False),
   ('shell-wait-mismatch','result.json',('cleanup','status'),0),
   ('wrong-client-parent','network-client.json',('ppid',),101),
   ('missing-eof','network-client.json',('peer_eof',),False),
   ('incomplete-address-list','network-client.json',('address_list_complete',),False),
   ('wrong-first-selected','network-client.json',('selected',),'93.184.216.35'),
   ('private-api-address','network-client.json',('addresses',),['93.184.216.34','127.0.0.1']),
   ('noncanonical-address','network-client.json',('selected',),'093.184.216.34'),
   ('wrong-request','network-client.json',('request_hex',),'00'*186),
   ('wrong-response','network-client.json',('response_hex',),'00'*14),
   ('wrong-http-length','network-client.json',('http','body_bytes'),15),
   ('wrong-mss','network-client.json',('tcp_maxseg_observed',),257),
   ('elapsed-overflow','network-client.json',('started_ms',),18446744073709551615),
   ('client-deadline','network-client.json',('elapsed_ms',),60000),
   ('pid-overflow','result.json',('child_pid',),2147483648),
   ('entry-scope','child-ready.json',('exec_boundary','inventory_scope'),'after_exec'),
  ]:semantic.append((name,change(file,path,value)))
  for name,old,new in [
   ('manager-wrong-flags',b'observed_flags: 16398',b'observed_flags: 0'),
   ('manager-wrong-final-wait',b'raw_app_wait=Some(0)',b'raw_app_wait=Some(9)'),
   ('manager-noncanonical',b'attempt=0',b'attempt=00'),
   ('manager-wrong-gate-time',b'ms=20 event=AppReady',b'ms=21 event=AppReady'),
   ('manager-unknown-event',b'event=AppReady',b'event=FakeReady'),
  ]:
   data=dict(original);data['manager-status.log']=data['manager-status.log'].replace(old,new,1);semantic.append((name,data))
  for name,transform in [
   ('duplicate-return',lambda raw:raw.replace(b'$ \x1b[?2004h',original[app+'network-shell-return.txt']+b'$ \x1b[?2004h')),
   ('prompt-before-return',lambda raw:b'$ \x1b[?2004h'+raw.replace(b'$ \x1b[?2004h',b'')),
   ('forged-done',lambda raw:raw.replace(b'pid=301 status=0',b'pid=302 status=0')),
  ]:
   data=dict(original);data[app+'pty-rx.bin']=transform(data[app+'pty-rx.bin']);doc=json.loads(data[app+'result.json']);doc['session']['pty_rx_bytes']=len(data[app+'pty-rx.bin']);data[app+'result.json']=dumps(doc);semantic.append((name,data))
  for file in ['result.json','network-client.json']:
   data=dict(original);data[app+file]=data[app+file].replace(b'{',b'{"unknown":1,"unknown":2,',1);semantic.append(('duplicate-key/'+file,data))
  # A complete, source-consistent same tuple is still a forbidden second flow.
  data=dict(original);two='0002'+NONCE[:44]+'/network-client.json';doc=json.loads(data[two]);doc['local']['port']=50001;data[two]=dumps(doc);semantic.append(('same-full-tuple',data))
  data={k:(v.replace(b'201',b'101') if k.startswith(app) else v) for k,v in original.items()};semantic.append(('pid-role-alias',data))
  # Second startup is source-serialized after first AppReady; a self-consistent
  # early second request still must fail this independent chronology predicate.
  data=dict(original);snap=data['session-result.txt'].replace(b'created_ms=30',b'created_ms=10')
  for old,new in [(31,11),(32,12),(33,13),(34,14),(40,20)]:snap=snap.replace(f'attempt=1 gate={GATES[[31,32,33,34,40].index(old)]} observed_ms=Some({old})'.encode(),f'attempt=1 gate={GATES[[31,32,33,34,40].index(old)]} observed_ms=Some({new})'.encode())
  data['session-result.txt']=snap;lines=data['manager-status.log'].splitlines(keepends=True);head=lines.pop(0);changed=[]
  for line in lines:
   if b'attempt=1 ' in line:
    for old,new in [(30,10),(31,11),(32,12),(33,13),(34,14),(40,20)]:line=line.replace(f'ms={old} '.encode(),f'ms={new} '.encode())
   changed.append(line)
  # Preserve each per-attempt event order while merging by actual manager ms.
  timed=[];last=[0,0]
  for i,line in enumerate(changed):
   a=int(re.search(rb'attempt=([01])',line).group(1));m=re.search(rb' ms=([0-9]+)',line)
   if m:last[a]=int(m.group(1))
   timed.append((last[a],i,line))
  data['manager-status.log']=head+b''.join(x[2] for x in sorted(timed));semantic.append(('premature-second-startup',data))
  for name,data in semantic:install(data);test(name)
  data=dict(original);data['session-result.txt']=data['session-result.txt'].replace(b'elapsed_ms=200',b'elapsed_ms=50');install(data);test('distinct-clock-origins',1)
  data=dict(original);doc=json.loads(data[app+'network-client.json']);doc['future_extension']={'nested':['value',3]};data[app+'network-client.json']=dumps(doc);install(data);test('unique-unknown-keys',1)
  # Same local port to another global peer is a distinct full tuple.
  data=dict(original);doc=json.loads(data[two]);doc['local']['port']=50001
  doc['addresses']=['93.184.216.35'];doc['selected']='93.184.216.35';doc['peer']['address']='93.184.216.35';data[two]=dumps(doc);install(data);test('same-source-port-different-peer',1)
  install(original)
  victim=session/(app+'network-client.json');held=temp/'held';victim.rename(held);victim.symlink_to(held);test('symlink-file');victim.unlink();held.rename(victim)
  os.link(victim,held);test('hardlink-file');held.unlink()
  for level in [session,session/app.rstrip('/')]:
   level.chmod(0o755);test('private-directory-mode');level.chmod(0o700)
  for at in [1,24]:
   env['DNDS_TEST_RACE_AT']=str(at);env['DNDS_TEST_RACE_PATH']=str(session/'session-result.txt');test('read-race-'+str(at));env.pop('DNDS_TEST_RACE_AT');env.pop('DNDS_TEST_RACE_PATH');install(original)
  # Nonempty grid cap and parent encoded-observation cap independently fail.
  data=dict(original);data[app+'grid-final.txt']=b'x'*65537;install(data);test('raw-cap');install(original)
  mutations=[
   ('normal-return','desktop_proof.c','!eqn(d,"startup.client_exit_status",0)','0','client-nonzero'),
   ('client-parent','desktop_proof.c','!eqn(d,pids[i],(uint32_t)f->shell_pid[slot-1])','0','wrong-client-parent'),
   ('selected-address','desktop_proof.c','memcmp(ip,pa->peer,4)||!ipfield(d,"peer.address",ip)','0||!ipfield(d,"peer.address",ip)','wrong-first-selected'),
   ('public-address','desktop_proof.c','||!eg_ipv4_global(ip)','||0','private-api-address'),
   ('request-bytes','desktop_proof.c','!memcmp(request,expected,qn)','1','wrong-request'),
   ('HTTP-facts','desktop_proof.c','eqn(d,"http.body_bytes",http.body_bytes)','1','wrong-http-length'),
   ('normal-MSS','desktop_proof.c','!eqn(d,"tcp_maxseg_observed",256)','0','wrong-mss'),
   ('tuple-distinct','desktop_proof.c','if(apps[0].port==apps[1].port','if(0&&apps[0].port==apps[1].port','same-full-tuple'),
   ('serialized-startup','manager_proof.c','(a==1&&!(seen[0]&(1u<<4)))','0','premature-second-startup'),
   ('distinct-role-pids','desktop_proof.c','if(ids[i]==ids[j])','if(0&&ids[i]==ids[j])','pid-role-alias'),
   ('gate-time','manager_proof.c','||ms!=times[a][g]','||0','manager-wrong-gate-time'),
  ]
  cases=dict(semantic)
  for name,file,before,after,case in mutations:
   source=(HERE/file).read_text();assert source.count(before)==1,(name,source.count(before))
   changed=temp/file;changed.write_text(source.replace(before,after));mutant=temp/name
   paths=[changed if p.name==file else p for p in SOURCES]
   run(['clang',*FLAGS,'-Wno-unused-variable','-DDNDS_PROOF_HOST_TEST','-DRG_TEST_ONLY','-I',HERE,*paths,'-lcrypto','-o',mutant])
   install(original);run([mutant,root,1],env=env)
   install(cases[case]);p=run([mutant,root,0],okay=False,env=env)
   if p.returncode!=1 or 'expected=0' not in p.stderr:raise RuntimeError('Undetected mutant '+name+': '+p.stderr)
   report['mutants'].append({'name':name,'compiled':True,'detected_by':case})
  install(original)
  sdk='/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
  tool='/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin/arm64-apple-darwin20.4-'
  system=Path('/nix/store/9i1vaph8rfh102h1x4v7gj7fl7q4x7rm-puredarwin-libsystem-spawnattr-0.1/usr/lib/libSystem.B.dylib')
  imports=set();definitions=set();deps={};objects=[]
  for source in SOURCES[:-1]:
   obj=temp/(source.stem+'.o');dep=temp/(source.stem+'.d');flags=[*FLAGS,'-fno-stack-protector']
   if source.parent==HERE:flags+=['-Wframe-larger-than=2048']
   run([tool+'clang',*flags,'-MD','-MF',dep,'-c',source,'-o',obj],env=dict(os.environ,DARWIN_SDK_ROOT=sdk))
   header=run([tool+'otool','-hv',obj]).stdout;assert 'ARM64' in header and 'OBJECT' in header
   undefined=run([tool+'nm','-u',obj]).stdout.split();imports.update(undefined)
   for line in run([tool+'nm','-g',obj]).stdout.splitlines():
    words=line.split()
    if len(words)==3 and words[1]!='U':definitions.add(words[2])
   for p in dep.read_text().replace('\\\n',' ').split(':',1)[1].split():deps[str(Path(p).resolve())]=sha(p)
   objects.append({'source':str(source.relative_to(ROOT)),'sha256':sha(obj),'imports':undefined})
  exports=set()
  dyld=ROOT/'runs/m1-desktop-010/native-resolver-base-inventory-001/libdyld.dylib'
  if sha(dyld)!='428e224ec2621bf3a2a7859402603a9a0fd6abe8b3e26c38edeb869e0ce58a0d':raise RuntimeError('Retained runtime libdyld changed')
  for lib in [system,dyld]:
   exports.update(re.findall(r'\b(_[^\s]+)',run(['llvm-objdump','--macho','--exports-trie',lib]).stdout))
  unresolved=imports-definitions-exports
  if unresolved:raise RuntimeError('Unresolved SDK imports '+repr(unresolved))
  report['SDK']={'path':sdk,'compiler':tool+'clang','objects':objects,'dependencies':dict(sorted(deps.items())),'libSystem_sha256':sha(system),'runtime_libdyld_sha256':sha(dyld),'system_imports':sorted(imports-definitions),'all_resolved':True}
  for source in SOURCES[:2]:
   p=run(['clang','--analyze',*FLAGS,'-DDNDS_PROOF_HOST_TEST',source,'-o',temp/(source.stem+'.plist')])
   if p.stderr:raise RuntimeError(p.stderr)
  report['analyzer_diagnostics']=0
  report['fixture_sha256']={k:hashlib.sha256(v).hexdigest() for k,v in original.items()}
  report['case_count']=len(report['cases'])
  (HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
  print(f"{len(report['cases'])} closed-proof cases PASS")
if __name__=='__main__':main()
