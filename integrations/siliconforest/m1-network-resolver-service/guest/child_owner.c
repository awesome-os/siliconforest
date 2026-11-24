#include "child_owner.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef DLR_CHILD_TEST
#include "child_test_api.h"
#endif
static void bad(struct dlr_child *c,int error) {if(!c->error)c->error=error?error:EIO;}
static void retire(struct dlr_child *c,int *fd) {if(*fd>=0){int n=*fd;*fd=-1;if(close(n))bad(c,errno);}}
static int pipe_owned(struct dlr_child *c,int f[2]) {
 if(pipe(f))return -1;
 for(unsigned i=0;i<2;i++) {
  int flags=fcntl(f[i],F_GETFD);
  if(flags<0||fcntl(f[i],F_SETFD,flags|FD_CLOEXEC)<0)goto fail;
 }
 int flags=fcntl(f[0],F_GETFL);
 if(flags<0||fcntl(f[0],F_SETFL,flags|O_NONBLOCK)<0)goto fail;
 return 0;
fail:
 bad(c,errno);retire(c,&f[0]);retire(c,&f[1]);return -1;
}
int dlr_child_start(struct dlr_child *c,enum dlr_child_kind kind,const char nonce[49],uint64_t now) {
 if(!c||c->initialized||!now||now>UINT64_MAX-75000||
    (kind!=DLR_DAEMON&&kind!=DLR_PROBE))return -1;
 if(!nonce||strlen(nonce)!=48)return -1;
 for(unsigned i=0;i<48;i++)if(!((nonce[i]>='0'&&nonce[i]<='9')||(nonce[i]>='a'&&nonce[i]<='f')))return -1;
 c->initialized=1;c->kind=kind;c->output_fd=c->error_fd=-1;c->started=c->last=now;
 int out[2]={-1,-1},err[2]={-1,-1},null=-1;
 if(pipe_owned(c,out)||pipe_owned(c,err)){bad(c,errno);goto finish;}
 null=open("/dev/null",O_RDWR|O_CLOEXEC);if(null<0){bad(c,errno);goto finish;}
 const char *daemon_argv[]={"/usr/sbin/mdnsd","-debug",NULL};
 const char *probe_argv[]={"/usr/bin/darwin-resolver-probe","--nonce",nonce,"--name","example.com",NULL};
 const char *env[]={"PATH=/usr/bin:/bin:/usr/sbin:/sbin","HOME=/","LC_ALL=C",NULL};
 struct chonk_spawn_request_v1 request;memset(&request,0,sizeof(request));
 request.version=1;request.argc=kind==DLR_DAEMON?2:5;request.envc=3;
 request.argv=kind==DLR_DAEMON?daemon_argv:probe_argv;request.executable=request.argv[0];request.envp=env;
 request.source_fds[0]=null;request.source_fds[1]=out[1];request.source_fds[2]=err[1];request.source_fds[3]=null;
 int rc=chonk_darwin_spawn_v1(&request,&c->spawn);
 /* Child creation transfers ownership even when shim local cleanup fails. */
 if(c->spawn.child_created&&c->spawn.pid>=2){c->created=1;c->pid=c->spawn.pid;}
 if(rc||!c->created||c->spawn.version!=1||c->spawn.stage!=10||c->spawn.requested_flags!=0x400e||c->spawn.observed_flags!=0x400e||c->spawn.duplicate_count!=4)bad(c,rc?rc:EIO);
finish:
 retire(c,&out[1]);retire(c,&err[1]);retire(c,&null);
 if(c->created){c->output_fd=out[0];c->error_fd=err[0];out[0]=err[0]=-1;}
 retire(c,&out[0]);retire(c,&err[0]);
 if(c->error){if(c->created)dlr_child_stop(c,now);return -1;}
 return 0;
}
void dlr_child_stop(struct dlr_child *c,uint64_t now) {
 if(!c||!c->initialized||!c->created||c->reaped||c->lost||c->stop_requested)return;
 c->stop_requested=1;c->stop_at=now;
}
static void drain(struct dlr_child *c,int *fd,uint8_t *buffer,size_t *used,size_t cap,int *eof) {
 if(*fd<0)return;
 size_t budget=8192;
 while(budget) {
  uint8_t discard[1];size_t space=cap-*used;
  size_t want=space;if(want>budget)want=budget;
  ssize_t n=read(*fd,space?buffer+*used:discard,space?want:1);
  if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return;
  if(n<0){bad(c,errno);retire(c,fd);return;}
  if(!n){*eof=1;retire(c,fd);return;}
  if(!space){bad(c,EFBIG);retire(c,fd);return;}
  *used+=(size_t)n;budget-=(size_t)n;
 }
}
void dlr_child_tick(struct dlr_child *c,uint64_t now) {
 if(!c||!c->initialized||!c->created)return;
 if(!now||now<c->last){bad(c,EINVAL);dlr_child_stop(c,c->last);}else c->last=now;
 drain(c,&c->output_fd,c->output,&c->output_used,sizeof(c->output),&c->output_eof);
 drain(c,&c->error_fd,c->errors,&c->error_used,sizeof(c->errors),&c->error_eof);
 if(!c->reaped&&!c->lost) {
  int status=0;pid_t pid=waitpid(c->pid,&status,WNOHANG);
  if(pid==c->pid){c->wait_status=status;c->reaped=1;}
  else if(pid<0&&errno!=EINTR){bad(c,errno);if(errno==ECHILD)c->lost=1;}
  else if(pid>0){bad(c,EIO);c->lost=1;}
 }
 if(c->error)dlr_child_stop(c,c->last);
 if(c->stop_requested&&!c->reaped&&!c->lost) {
  int signal=0;if(!c->term_sent){signal=SIGTERM;c->term_sent=1;}
  else if(!c->kill_sent&&c->last>=c->stop_at&&c->last-c->stop_at>=2000){signal=SIGKILL;c->kill_sent=1;}
  if(signal&&kill(c->pid,signal)<0)bad(c,errno);
 }
}
int dlr_child_done(const struct dlr_child *c) {
 return c&&c->initialized&&c->created&&c->reaped&&!c->lost&&c->output_fd<0&&c->error_fd<0;
}
