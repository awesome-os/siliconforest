/* Original adapter derived from the qualified resolver child_owner. */
#include "owned_child.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef DNDS_CHILD_TEST
#include "child_test_api.h"
#endif
static void bad(struct dnds_child *c,int e) { if(!c->error)c->error=e?e:EIO; }
static void retire(struct dnds_child *c,int *fd) {
 if(*fd>=0){int n=*fd;*fd=-1;if(close(n))bad(c,errno);}
}
static int pipes(struct dnds_child *c,int f[2]) {
 if(pipe(f))return -1;
 for(unsigned i=0;i<2;i++) {
  int flags=fcntl(f[i],F_GETFD);
  if(flags<0||fcntl(f[i],F_SETFD,flags|FD_CLOEXEC)<0)goto fail;
 }
 int flags=fcntl(f[0],F_GETFL);
 if(flags<0||fcntl(f[0],F_SETFL,flags|O_NONBLOCK)<0)goto fail;
 return 0;
fail: bad(c,errno);retire(c,&f[0]);retire(c,&f[1]);return -1;
}
int dnds_child_start(struct dnds_child *c,enum dnds_child_kind kind,const char nonce[49],uint64_t now) {
 if(!c||c->initialized||!now||now>UINT64_MAX-180000||
    (kind!=DNDS_MDNS&&kind!=DNDS_FRONTEND)||!nonce||strlen(nonce)!=48)return -1;
 for(unsigned i=0;i<48;i++)if(!((nonce[i]>='0'&&nonce[i]<='9')||(nonce[i]>='a'&&nonce[i]<='f')))return -1;
 c->initialized=1;c->kind=kind;c->output_fd=c->error_fd=-1;c->started=c->last=now;
 /* Normal frontend shutdown is allowed its source-bound ten-second manager
  * cleanup loop plus scheduling margin. This is not an app orphan guarantee. */
 c->kill_after_ms=kind==DNDS_FRONTEND?15000:2000;
 int out[2]={-1,-1},err[2]={-1,-1},null=-1;
 if(pipes(c,out)||pipes(c,err)){bad(c,errno);goto finish;}
 null=open("/dev/null",O_RDWR|O_CLOEXEC);
 if(null<0){bad(c,errno);goto finish;}
 char records[160];
 int n=snprintf(records,sizeof(records),"/private/var/run/darwin-desktop-network/session-%s",nonce);
 if(n<0||(size_t)n>=sizeof(records)){bad(c,EINVAL);goto finish;}
 const char *mdns[]={"/usr/sbin/mdnsd","-debug",NULL};
 const char *frontend[]={"/usr/bin/chonkstep-darwin-desktop","--run",
  "--socket","/private/var/run/darwin-desktop-network/wayland-0",
  "--records",records,"--session",nonce,
  "--terminal","/usr/bin/chonk-desktop-network-terminal","--seconds","90",
  "--font-regular","/usr/share/darwin-desktop/DejaVuSans.ttf",
  "--font-bold","/usr/share/darwin-desktop/DejaVuSans-Bold.ttf",
  "--input","pdhid","--startup-terminals","2",NULL};
 const char *env[]={"PATH=/usr/bin:/bin:/usr/sbin:/sbin","HOME=/","LC_ALL=C",
  "XDG_RUNTIME_DIR=/private/var/run/darwin-desktop-network","WAYLAND_DISPLAY=wayland-0",
  "XKB_CONFIG_ROOT=/usr/share/X11/xkb","TMPDIR=/private/tmp",NULL};
 struct chonk_spawn_request_v1 q;memset(&q,0,sizeof(q));
 q.version=1;q.argv=kind==DNDS_MDNS?mdns:frontend;q.argc=kind==DNDS_MDNS?2:20;
 q.envc=kind==DNDS_MDNS?3:7;q.envp=env;q.executable=q.argv[0];
 /* Set the terminator for the daemon's deliberately shorter environment. */
 const char *mdns_env[]={env[0],env[1],env[2],NULL};
 if(kind==DNDS_MDNS)q.envp=mdns_env;
 q.source_fds[0]=null;q.source_fds[1]=out[1];q.source_fds[2]=err[1];q.source_fds[3]=null;
 int rc=chonk_darwin_spawn_v1(&q,&c->spawn);
 if(c->spawn.child_created&&c->spawn.pid>=2){c->created=1;c->pid=c->spawn.pid;}
 if(rc||!c->created||c->spawn.version!=1||c->spawn.stage!=10||
    c->spawn.requested_flags!=0x400e||c->spawn.observed_flags!=0x400e||
    c->spawn.duplicate_count!=4||c->spawn.primary_error||c->spawn.spawn_error||
    c->spawn.attr_destroy_error||c->spawn.actions_destroy_error||
    c->spawn.fd_close_error||c->spawn.fd_close_failures||c->spawn.reserved)
  bad(c,rc?rc:EIO);
finish:
 retire(c,&out[1]);retire(c,&err[1]);retire(c,&null);
 if(c->created){c->output_fd=out[0];c->error_fd=err[0];out[0]=err[0]=-1;}
 retire(c,&out[0]);retire(c,&err[0]);
 if(c->error){if(c->created)dnds_child_stop(c,now);return -1;}
 return 0;
}
void dnds_child_stop(struct dnds_child *c,uint64_t now) {
 if(!c||!c->initialized||!c->created||c->reaped||c->lost||c->stop_requested)return;
 c->stop_requested=1;c->stop_at=now;
}
static void drain(struct dnds_child *c,int *fd,uint8_t *b,size_t *used,size_t cap,int *eof) {
 if(*fd<0)return;
 size_t budget=8192;
 while(budget) {
  uint8_t discard[1];size_t space=cap-*used,want=space;
  if(want>budget)want=budget;
  ssize_t n=read(*fd,space?b+*used:discard,space?want:1);
  if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return;
  if(n<0){bad(c,errno);retire(c,fd);return;}
  if(!n){*eof=1;retire(c,fd);return;}
  if(!space){bad(c,EFBIG);retire(c,fd);return;}
  *used+=(size_t)n;budget-=(size_t)n;
 }
}
void dnds_child_tick(struct dnds_child *c,uint64_t now) {
 if(!c||!c->initialized||!c->created)return;
 if(!now||now<c->last){bad(c,EINVAL);dnds_child_stop(c,c->last);}else c->last=now;
 drain(c,&c->output_fd,c->output,&c->output_used,sizeof(c->output),&c->output_eof);
 drain(c,&c->error_fd,c->errors,&c->error_used,sizeof(c->errors),&c->error_eof);
 if(!c->reaped&&!c->lost) {
  int status=0;pid_t pid=waitpid(c->pid,&status,WNOHANG);
  if(pid==c->pid){c->wait_status=status;c->reaped=1;}
  else if(pid<0&&errno!=EINTR){bad(c,errno);if(errno==ECHILD)c->lost=1;}
  else if(pid>0){bad(c,EIO);c->lost=1;}
 }
 if(c->error)dnds_child_stop(c,c->last);
 if(c->stop_requested&&!c->reaped&&!c->lost) {
  int sig=0;
  if(!c->term_sent){sig=SIGTERM;c->term_sent=1;}
  else if(!c->kill_sent&&c->last>=c->stop_at&&c->last-c->stop_at>=c->kill_after_ms){sig=SIGKILL;c->kill_sent=1;}
  if(sig&&kill(c->pid,sig)<0)bad(c,errno);
 }
}
int dnds_child_done(const struct dnds_child *c) {
 return c&&c->initialized&&c->created&&c->reaped&&!c->lost&&c->output_fd<0&&c->error_fd<0;
}
