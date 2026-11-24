"""Synthetic successful-serializer fixtures; does not regrade a saved boot."""
from pathlib import Path
import json, subprocess, tempfile

HERE=Path(__file__).resolve().parent
NONCE='0123456789abcdef'*3
GATES=('Admission','StatusNonblocking','SpawnReturned','PidOwned','AppReady',
       'AppCleanup','StatusEof','AppReaped','TermAttempt','KillAttempt')
def app(slot):
    pid=100+slot
    instance=f'{slot:04d}'+NONCE[:44]
    s=(f'attempt={slot-1} source=explicit_startup request=Some({slot}) instance=Some("{instance}") '
       f'pid=Some({pid}) created_ms=10 failure=None failure_ms=None status_bytes=150 '
       'wait=Some(0) wait_error=None cleanup=Some(Cleanup { complete: true, raw_wait_status: 9, '
       'term_sent: true, kill_sent: true }) native_return=Some(0) native=Some(SpawnResultV1 { '
       f'version: 1, stage: 10, pid: {pid}, primary_error: 0, spawn_error: 0, attr_destroy_error: 0, '
       'actions_destroy_error: 0, fd_close_error: 0, fd_close_failures: 0, requested_flags: 16398, '
       'observed_flags: 16398, child_created: 1, duplicate_count: 4, reserved: 0 })\n')
    for d in ('exit_reason=query_failed errno=2','cs_at_spawn=flags=0x20020001 cs_at_exit=query_failed errno=3','kernel_messages=not_observed'):
        s+=f'attempt={slot-1} {d}\n'
    for g,t in zip(GATES,(11,12,13,14,20,100,101,102,99,None)):
        s+=f'attempt={slot-1} gate={g} observed_ms='+('None' if t is None else f'Some({t})')+'\n'
    return s

BASE=('CHONKDESKTOP/1 initialized=1 input=both shell=shared native_runtime_qualification=not_inferred\n'
      'CHONKDESKTOP/1 elapsed_ms=200 cleanup=Complete deadline=false frame_error=None manager_failed=false\n'
      'CHONKLAUNCH/1 snapshot attempts=2 journal_error=None app_internal_gates=unknown_until_final_app_record\n'+app(1)+app(2))

def main():
    cases=[('normal',BASE,1),('different-clock-origins',BASE.replace('elapsed_ms=200','elapsed_ms=100'),1),('normal-finite-deadline',BASE.replace('deadline=false','deadline=true'),1),('no-shell-signals',BASE.replace('raw_wait_status: 9, term_sent: true, kill_sent: true','raw_wait_status: 0, term_sent: false, kill_sent: false'),1)]
    mutations={
      'not-initialized':('initialized=1','initialized=0'),
      'incomplete-manager':('cleanup=Complete','cleanup=Incomplete'),
      'bad-deadline-type':('deadline=false','deadline=1'),
      'frame-failure':('frame_error=None','frame_error=Some("failure")'),
      'manager-failure':('manager_failed=false','manager_failed=true'),
      'attempt-not-two':('snapshot attempts=2','snapshot attempts=1'),
      'journal-failure':('journal_error=None','journal_error=Some("failure")'),
      'interactive-request':('source=explicit_startup','source=menu'),
      'wrong-request':('request=Some(1)','request=Some(2)'),
      'wrong-instance':('0001'+NONCE[:44],'0003'+NONCE[:44]),
      'pid-zero':('pid=Some(101)','pid=Some(0)'),
      'pid-overflow':('pid=Some(101)','pid=Some(2147483648)'),
      'created-overflow':('created_ms=10','created_ms=18446744073709551616'),
      'created-leading-zero':('created_ms=10','created_ms=010'),
      'created-late':('created_ms=10','created_ms=99'),
      'attempt-failure':('failure=None','failure=Some("failed")'),
      'missing-status':('status_bytes=150','status_bytes=0'),
      'app-nonzero-exit':('wait=Some(0)','wait=Some(9)'),
      'app-wait-error':('wait_error=None','wait_error=Some(10)'),
      'app-incomplete':('complete: true','complete: false'),
      'shell-wait-overflow':('raw_wait_status: 9','raw_wait_status: 65536'),
      'kill-without-term':('term_sent: true','term_sent: false'),
      'shim-return':('native_return=Some(0)','native_return=Some(5)'),
      'shim-pid-mismatch':('stage: 10, pid: 101','stage: 10, pid: 103'),
      'shim-stage':('stage: 10','stage: 9'),
      'shim-close':('fd_close_failures: 0','fd_close_failures: 1'),
      'shim-flags':('observed_flags: 16398','observed_flags: 0'),
      'shim-child':('child_created: 1','child_created: 0'),
      'shim-dups':('duplicate_count: 4','duplicate_count: 3'),
      'shim-reserved':('reserved: 0','reserved: 1'),
      'missing-ready':('gate=AppReady observed_ms=Some(20)','gate=AppReady observed_ms=None'),
      'missing-cleanup':('gate=AppCleanup observed_ms=Some(100)','gate=AppCleanup observed_ms=None'),
      'missing-reap':('gate=AppReaped observed_ms=Some(102)','gate=AppReaped observed_ms=None'),
      'missing-eof':('gate=StatusEof observed_ms=Some(101)','gate=StatusEof observed_ms=None'),
      'manager-killed-app':('gate=KillAttempt observed_ms=None','gate=KillAttempt observed_ms=Some(101)'),
      'late-term':('gate=TermAttempt observed_ms=Some(99)','gate=TermAttempt observed_ms=Some(103)'),
      
      'elapsed-too-large':('elapsed_ms=200','elapsed_ms=180001'),
    }
    for name,(a,b) in mutations.items():
        assert a in BASE,name
        cases.append((name,BASE.replace(a,b,1),0))
    for n in range(0,len(BASE),37):cases.append((f'cut-{n}',BASE[:n],0))
    cases.append(('same-app-pid',BASE.replace('102','101'),0))
    cases += [('trailing',BASE+'unexpected\n',0),('NUL',BASE+'\0',0),
              ('duplicate-snapshot',BASE+BASE,0),('missing-last-LF',BASE[:-1],0)]
    with tempfile.TemporaryDirectory(prefix='desktop-session-text-') as tmp:
        root=Path(tmp)
        driver=root/'driver.c'
        rows=',\n'.join('{'+json.dumps(name)+','+json.dumps(s).replace('\\u0000','\\000')+','+str(len(s))+','+str(ok)+'}' for name,s,ok in cases)
        driver.write_text('''#ifdef NDEBUG
#error assertions required
#endif
#include "session_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct sample {const char *name,*text;size_t length;int expected;};
static const struct sample samples[]={'''+rows+'''};
int main(void) {
 for(size_t i=0;i<sizeof(samples)/sizeof(samples[0]);i++) {
  struct dnds_session_summary out,old;
  memset(&out,0xa5,sizeof(out));old=out;
  int got=dnds_session_text((const uint8_t *)samples[i].text,samples[i].length,"'''+NONCE+'''",&out);
  if(got!=samples[i].expected)fprintf(stderr,"case=%s got=%d expected=%d\\n",samples[i].name,got,samples[i].expected);
  assert(got==samples[i].expected);
  if(!got)assert(!memcmp(&out,&old,sizeof(out)));
  else assert(out.app[0].pid==101&&out.app[1].pid==102&&(out.elapsed_ms==200||out.elapsed_ms==100));
 }
 struct dnds_session_summary o;
 assert(!dnds_session_text(NULL,1,"'''+NONCE+'''",&o));
 assert(!dnds_session_text((const uint8_t *)samples[0].text,strlen(samples[0].text),"BAD",&o));
 puts("PASS");return 0;
}''')
        flags=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-Wno-overlength-strings','-O1','-fsanitize=address,undefined','-I'+str(HERE)]
        def build_run(src,name):
            target=root/name
            p=subprocess.run(flags+[str(src),str(driver),'-o',str(target)],capture_output=True,text=True,timeout=30)
            if p.returncode:raise RuntimeError(p.stderr)
            return subprocess.run([str(target)],capture_output=True,text=True,timeout=30)
        result=build_run(HERE/'session_text.c','base')
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        source=(HERE/'session_text.c').read_text()
        mutants={
          'ignore-elapsed-cap':('s->elapsed_ms>180000||','0||'),
          'ignore-same-pid':('s.app[0].pid==s.app[1].pid','0'),
          'ignore-app-kill':('||seen[9])','||0)'),
          'ignore-shell-kill-without-term':('(s->shell_kill&&!s->shell_term)||','0||'),

          'ignore-whole-tail':('||c.p!=c.end||','||0||'),
          'ignore-created-vs-manager-clock':('times[i]<s->created_ms','0'),
        }
        killed=[]
        for name,(a,b) in mutants.items():
            assert source.count(a)==1,(name,source.count(a))
            src=root/(name+'.c');src.write_text(source.replace(a,b))
            p=build_run(src,name)
            if p.returncode==0 or 'Assertion' not in p.stderr:raise RuntimeError('survived '+name+' '+p.stderr)
            killed.append(name)
        print(json.dumps({'status':'PASS','synthetic_cases':len(cases),'compiled_mutants_killed':killed}))

if __name__=='__main__':main()
