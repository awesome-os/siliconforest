#ifdef NDEBUG
#error assertions required
#endif
#include "owned_child.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static int out=-1,err=-1,exited,lost,spawn_error,created=1,signals[8],signal_count,wait_calls,status,variant;
static const char nonce[]="0123456789abcdef0123456789abcdef0123456789abcdef";
static void reset(void) {out=err=-1;exited=lost=spawn_error=signal_count=wait_calls=status=variant=0;created=1;memset(signals,0,sizeof(signals));}
int chonk_darwin_spawn_v1(const struct chonk_spawn_request_v1 *q,struct chonk_spawn_result_v1 *r) {
 assert(q->version==1&&q->source_fds[0]==q->source_fds[3]);
 assert(q->envc==(q->argc==2?3:7));
 for(unsigned i=0;i<4;i++)assert(fcntl(q->source_fds[i],F_GETFD)&FD_CLOEXEC);
 assert(!strcmp(q->argv[0],q->executable));assert(!q->envp[q->envc]&&!q->argv[q->argc]);
 if(q->argc==2)assert(!strcmp(q->executable,"/usr/sbin/mdnsd")&&!strcmp(q->argv[1],"-debug"));
 else {assert(q->argc==20);assert(!strcmp(q->executable,"/usr/bin/chonkstep-darwin-desktop"));
 assert(!strcmp(q->argv[1],"--run")&&!strcmp(q->argv[3],"/private/var/run/darwin-desktop-network/wayland-0"));
 assert(!strcmp(q->argv[5],"/private/var/run/darwin-desktop-network/session-0123456789abcdef0123456789abcdef0123456789abcdef"));
 assert(!strcmp(q->argv[7],nonce));assert(!strcmp(q->argv[9],"/usr/bin/chonk-desktop-network-terminal"));
 assert(!strcmp(q->argv[11],"90")&&!strcmp(q->argv[17],"pdhid")&&!strcmp(q->argv[19],"2"));
 assert(!strcmp(q->envp[3],"XDG_RUNTIME_DIR=/private/var/run/darwin-desktop-network"));
 }
 memset(r,0,sizeof(*r));r->version=1;r->stage=10;r->pid=77;r->child_created=(unsigned)created;r->duplicate_count=4;r->requested_flags=r->observed_flags=0x400e;
 if(variant==1)r->observed_flags=0;
 if(variant==2)r->attr_destroy_error=EIO;
 if(variant==3)r->fd_close_failures=1;
 if(variant==4)r->reserved=1;
 if(variant==5)r->stage=9;
 if(created){out=dup(q->source_fds[1]);err=dup(q->source_fds[2]);assert(out>=0&&err>=0);}
 return spawn_error;
}
pid_t test_waitpid(pid_t pid,int *s,int flags) {assert(pid==77&&flags==WNOHANG);wait_calls++;if(lost){errno=ECHILD;return -1;}if(exited){*s=status;return pid;}return 0;}
int test_kill(pid_t pid,int sig) {assert(pid==77&&!lost&&!exited);assert(signal_count<8);signals[signal_count++]=sig;return 0;}
static void writers_close(void){assert(close(out)==0&&close(err)==0);out=err=-1;}
int main(void) {
 struct dnds_child c;
 reset();memset(&c,0,sizeof(c));assert(!dnds_child_start(&c,DNDS_FRONTEND,nonce,100));assert(c.created&&c.pid==77&&!c.error);assert(dnds_child_start(&c,DNDS_FRONTEND,nonce,101)<0);
 assert(write(out,"abc\n",4)==4&&write(err,"diagnostic",10)==10);dnds_child_tick(&c,101);assert(c.output_used==4&&c.error_used==10&&!c.reaped&&!signal_count);writers_close();status=0;exited=1;dnds_child_tick(&c,102);assert(dnds_child_done(&c)&&!c.error&&c.output_eof&&c.error_eof&&c.wait_status==0);dnds_child_stop(&c,103);dnds_child_tick(&c,104);assert(!signal_count);
 reset();memset(&c,0,sizeof(c));assert(!dnds_child_start(&c,DNDS_MDNS,nonce,100));dnds_child_stop(&c,110);dnds_child_tick(&c,110);assert(signal_count==1&&signals[0]==SIGTERM);dnds_child_tick(&c,2109);assert(signal_count==1);dnds_child_tick(&c,2110);assert(signal_count==2&&signals[1]==SIGKILL);dnds_child_tick(&c,6000);assert(signal_count==2);exited=1;status=9;writers_close();dnds_child_tick(&c,6001);assert(dnds_child_done(&c)&&c.wait_status==9);
 reset();memset(&c,0,sizeof(c));assert(!dnds_child_start(&c,DNDS_MDNS,nonce,100));lost=1;dnds_child_stop(&c,101);dnds_child_tick(&c,101);assert(c.lost&&c.error==ECHILD&&!signal_count&&!dnds_child_done(&c));dnds_child_tick(&c,10000);assert(!signal_count);writers_close();dnds_child_tick(&c,10001);
 reset();memset(&c,0,sizeof(c));spawn_error=EIO;assert(dnds_child_start(&c,DNDS_FRONTEND,nonce,100)<0);assert(c.created&&c.stop_requested&&c.error==EIO);dnds_child_tick(&c,101);assert(signal_count==1);exited=1;writers_close();dnds_child_tick(&c,102);assert(dnds_child_done(&c)&&c.error==EIO);
 reset();memset(&c,0,sizeof(c));spawn_error=ENOENT;created=0;assert(dnds_child_start(&c,DNDS_FRONTEND,nonce,100)<0);assert(!c.created&&!c.stop_requested&&c.error==ENOENT);dnds_child_tick(&c,9000);assert(!signal_count&&!wait_calls);
 reset();memset(&c,0,sizeof(c));assert(!dnds_child_start(&c,DNDS_FRONTEND,nonce,100));assert(!fcntl(out,F_SETFL,O_NONBLOCK));char data[1024];memset(data,'x',sizeof(data));for(unsigned i=0;i<64;i++){assert(write(out,data,sizeof(data))==(ssize_t)sizeof(data));dnds_child_tick(&c,101+i);}assert(c.output_used==DNDS_CHILD_OUT&&!c.error);assert(write(out,"x",1)==1);dnds_child_tick(&c,170);assert(c.error==EFBIG&&c.output_used==DNDS_CHILD_OUT&&c.stop_requested&&signals[0]==SIGTERM);exited=1;writers_close();dnds_child_tick(&c,171);assert(dnds_child_done(&c)&&!c.output_eof);
 reset();memset(&c,0,sizeof(c));assert(!dnds_child_start(&c,DNDS_MDNS,nonce,100));dnds_child_tick(&c,99);assert(c.error==EINVAL&&c.stop_requested&&signal_count==1);exited=1;writers_close();dnds_child_tick(&c,102);assert(dnds_child_done(&c));
 reset();memset(&c,0,sizeof(c));assert(!dnds_child_start(&c,DNDS_FRONTEND,nonce,100));
 dnds_child_stop(&c,110);dnds_child_tick(&c,110);assert(signal_count==1&&signals[0]==SIGTERM);
 dnds_child_tick(&c,15109);assert(signal_count==1);dnds_child_tick(&c,15110);assert(signal_count==2&&signals[1]==SIGKILL);
 exited=1;status=9;writers_close();dnds_child_tick(&c,15111);assert(dnds_child_done(&c));
 reset();memset(&c,0,sizeof(c));assert(dnds_child_start(&c,DNDS_FRONTEND,"ABC",100)<0&&!c.initialized);
 assert(dnds_child_start(&c,DNDS_FRONTEND,nonce,UINT64_MAX)<0&&!c.initialized);
 for(int v=1;v<=5;v++) {
 reset();variant=v;memset(&c,0,sizeof(c));assert(dnds_child_start(&c,DNDS_FRONTEND,nonce,100)<0);
 assert(c.created&&c.stop_requested&&c.error);dnds_child_tick(&c,101);assert(signal_count==1);
 exited=1;writers_close();dnds_child_tick(&c,102);assert(dnds_child_done(&c));
 }
 puts("owned_child: 14 lifecycle/fault scenarios passed (real pipes, shim/wait/signal adapter)");return 0;
}
