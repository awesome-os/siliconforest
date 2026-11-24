from pathlib import Path
import subprocess,tempfile,json
S=Path(__file__).resolve().parent
text=(S/'desktop_network.c').read_text();old=(S.parents[1]/'desktop_network.c').read_text()
a=text.index('/* Failure evidence only,');b=text.index('static int service_start(',a)
body=text[a:b]
assert text[:a]+text[b:]==old.replace(' if(s->tty_saved&&tcsetattr(0,TCSANOW,&s->terminal)<0)',' if(s->error){failure_child(s,&s->frontend);failure_child(s,&s->daemon);}\n if(s->tty_saved&&tcsetattr(0,TCSANOW,&s->terminal)<0)')
source=r'''
#ifdef NDEBUG
#error assertion tests required
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "owned_child.h"
struct state {int flags_saved;char nonce[49];};
'''+body+r'''
int main(void){
 int saved=dup(1);assert(saved>=0);int p[2];assert(!pipe(p));assert(dup2(p[1],1)==1);assert(!close(p[1]));
 int f=fcntl(1,F_GETFL);assert(f>=0);struct state s={.flags_saved=1};strcpy(s.nonce,"0123456789abcdef0123456789abcdef0123456789abcdef");
 struct dnds_child c={.kind=DNDS_FRONTEND,.pid=41,.created=1,.reaped=0,.term_sent=1,.stop_requested=1};
 failure_child(&s,&c);assert(fcntl(1,F_GETFL)==f);assert(fcntl(p[0],F_SETFL,O_NONBLOCK)==0);char b[1024];assert(read(p[0],b,sizeof(b))==-1&&errno==EAGAIN);
 assert(!fcntl(1,F_SETFL,f|O_NONBLOCK));failure_child(&s,&c);ssize_t n=read(p[0],b,sizeof(b)-1);assert(n>0);b[n]=0;assert(strstr(b,"kind=2 pid=41 created=1 reaped=0 lost=0 error=0 wait_status=0 output_eof=0 stderr_eof=0 term_sent=1 kill_sent=0 stop_requested=1\n"));
 int flags=fcntl(1,F_GETFL);assert(flags==(f|O_NONBLOCK));c.created=0;failure_child(&s,&c);assert(read(p[0],b,sizeof(b))==-1&&errno==EAGAIN);c.created=1;s.flags_saved=0;failure_child(&s,&c);assert(read(p[0],b,sizeof(b))==-1&&errno==EAGAIN);s.flags_saved=1;
 char fill[4096];memset(fill,'x',sizeof(fill));while(write(1,fill,sizeof(fill))>0){};assert(errno==EAGAIN);failure_child(&s,&c);assert(fcntl(1,F_GETFL)==flags&&!c.reaped&&!c.kill_sent&&c.term_sent);
 assert(dup2(saved,1)==1);close(saved);close(p[0]);puts("5 failure-only evidence cases passed");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='dnds-failure-')as tmp:
 p=Path(tmp);(p/'test.c').write_text(source)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-fsanitize=address,undefined','-I'+str(S.parents[1]),'-I'+str(S.parents[3]/'chonkstep-darwin/darwin-spawn'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 r=subprocess.run([str(p/'test')],capture_output=True,text=True,timeout=10);assert r.returncode==0,r.stderr
print(json.dumps({'status':'PASS','real_pipe_cases':5,'existing_source_unchanged_except_failure_observation':True,'no_budget_or_PASS_change':True}))
