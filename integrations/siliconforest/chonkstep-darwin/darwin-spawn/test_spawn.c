#ifdef NDEBUG
#error assertion-based fake API tests require assertions
#endif
#define _POSIX_C_SOURCE 200809L
#include "darwin_spawn.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "spawn_test_api.h"

/* No host or target process is ever spawned by this policy fixture. */
static struct fake {
    int step,fail_at,dups,close_attempts[4],attr_init,actions_init;
    int attr_destroy,actions_destroy,flags_calls,spawn_calls,spawn_succeeded;
    int default_calls,mask_calls,group_calls,signal_count,actions;
    int corrupt_flags,zero_pid;
    short flags;
} f;
static int api(void) { f.step++; return f.step==f.fail_at?EIO:0; }
static int syscall_result(void) { int e=api(); if(e) { errno=e; return -1; } return 0; }
int csp_fake_fcntl(int fd,int command,...)
{
    va_list args;int min;
    assert(fd>=0);assert(command==F_DUPFD_CLOEXEC);
    va_start(args,command);min=va_arg(args,int);va_end(args);assert(min==4);
    if(syscall_result()) return -1;
    return 100+f.dups++;
}
int csp_fake_close(int fd)
{ assert(fd>=100&&fd<104);assert(!f.close_attempts[fd-100]++);return syscall_result(); }
int csp_fake_sigemptyset(sigset_t *s) { memset(s,0,sizeof(*s));return syscall_result(); }
int csp_fake_sigaddset(sigset_t *s,int signal)
{
    static const int expected[]={SIGTERM,SIGINT,SIGHUP,SIGPIPE,SIGCHLD};(void)s;
    assert(f.signal_count<5&&signal==expected[f.signal_count++]);return syscall_result();
}
int csp_fake_attr_init(posix_spawnattr_t *a)
{ int e=api();if(!e){memset(a,0,sizeof(*a));f.attr_init++;}return e; }
int csp_fake_actions_init(posix_spawn_file_actions_t *a)
{ int e=api();if(!e){memset(a,0,sizeof(*a));f.actions_init++;}return e; }
int csp_fake_setdefault(posix_spawnattr_t *a,const sigset_t *s)
{ (void)a;(void)s;assert(f.signal_count==5);f.default_calls++;return api(); }
int csp_fake_setmask(posix_spawnattr_t *a,const sigset_t *s)
{ (void)a;(void)s;f.mask_calls++;return api(); }
int csp_fake_setgroup(posix_spawnattr_t *a,pid_t p)
{ (void)a;assert(p==0);f.group_calls++;return api(); }
int csp_fake_setflags(posix_spawnattr_t *a,short flags)
{
    int e;(void)a;assert(++f.flags_calls==1);
    assert(flags==(POSIX_SPAWN_CLOEXEC_DEFAULT|POSIX_SPAWN_SETSIGDEF|POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETPGROUP));
    assert(f.default_calls==1&&f.mask_calls==1&&f.group_calls==1);
    e=api();if(!e) f.flags=flags;return e;
}
int csp_fake_getflags(const posix_spawnattr_t *a,short *flags)
{ (void)a;*flags=f.corrupt_flags?(short)(f.flags&~POSIX_SPAWN_CLOEXEC_DEFAULT):f.flags;return api(); }
int csp_fake_adddup2(posix_spawn_file_actions_t *a,int from,int to)
{ (void)a;assert(from==100+to&&to==f.actions++&&to>=0&&to<=3);return api(); }
int csp_fake_spawn(pid_t *p,const char *exe,const posix_spawn_file_actions_t *a,
                    const posix_spawnattr_t *b,char *const argv[],char *const envp[])
{
    int e;(void)a;(void)b;
    f.spawn_calls++;assert(f.flags_calls==1&&(f.flags&POSIX_SPAWN_CLOEXEC_DEFAULT));
    assert(f.actions==4&&f.dups==4);
    assert(!strcmp(exe,"/usr/bin/chonk-terminal-darwin")&&!strcmp(argv[0],exe));
    assert(!argv[1]&&!strcmp(envp[0],"TERM=dumb")&&!envp[1]);
    e=api();if(!e){*p=f.zero_pid?0:4201;f.spawn_succeeded=1;}return e;
}
int csp_fake_attr_destroy(posix_spawnattr_t *a)
{ (void)a;assert(f.attr_init&&!f.attr_destroy++);return api(); }
int csp_fake_actions_destroy(posix_spawn_file_actions_t *a)
{ (void)a;assert(f.actions_init&&!f.actions_destroy++);return api(); }

static struct chonk_spawn_request_v1 request(void)
{
    static const char *const argv[]={"/usr/bin/chonk-terminal-darwin",NULL};
    static const char *const envp[]={"TERM=dumb",NULL};
    struct chonk_spawn_request_v1 q={1,1,1,0,argv[0],argv,envp,{0,1,2,3}};
    return q;
}
static void cleanup_attempts(void)
{
    int i;assert(f.attr_init==f.attr_destroy);assert(f.actions_init==f.actions_destroy);
    for(i=0;i<f.dups;i++) assert(f.close_attempts[i]==1);
}
int main(void)
{
    struct chonk_spawn_request_v1 q=request();struct chonk_spawn_result_v1 r;
    int rc,steps,i;
    memset(&f,0,sizeof(f));rc=chonk_darwin_spawn_v1(&q,&r);assert(rc==0);
    assert(r.pid==4201&&r.child_created==1&&r.stage==CHONK_SPAWN_FINISHED);
    assert(!r.primary_error&&!r.spawn_error&&!r.attr_destroy_error&&!r.actions_destroy_error&&!r.fd_close_error);
    assert(r.requested_flags==r.observed_flags&&r.requested_flags==0x400e&&r.duplicate_count==4);
    cleanup_attempts();steps=f.step;
    for(i=1;i<=steps;i++) {
        memset(&f,0,sizeof(f));f.fail_at=i;
        rc=chonk_darwin_spawn_v1(&q,&r);assert(rc==EIO);cleanup_attempts();
        if(f.spawn_succeeded) {
            assert(r.pid==4201&&r.child_created==1&&!r.primary_error);
            assert(r.attr_destroy_error||r.actions_destroy_error||r.fd_close_error);
        } else { assert(!r.pid&&!r.child_created&&r.primary_error==EIO); }
    }
    memset(&f,0,sizeof(f));f.corrupt_flags=1;
    rc=chonk_darwin_spawn_v1(&q,&r);assert(rc==EINVAL&&!f.spawn_calls&&!r.child_created);cleanup_attempts();
    memset(&f,0,sizeof(f));f.zero_pid=1;
    rc=chonk_darwin_spawn_v1(&q,&r);assert(rc==EIO&&!r.pid&&!r.child_created);cleanup_attempts();
    for(i=0;i<7;i++) {
        q=request();memset(&f,0,sizeof(f));
        switch(i) {
        case 0:q.version=0;break;case 1:q.reserved=1;break;case 2:q.argc=33;break;
        case 3:q.envc=33;break;case 4:q.executable="relative";break;
        case 5:q.argv=NULL;break;case 6:q.source_fds[3]=-1;break;
        }
        rc=chonk_darwin_spawn_v1(&q,&r);assert(rc!=0&&!f.step&&!r.child_created);
    }
    printf("PASS: %d fake API failure boundaries, PID retained on local cleanup failure, exact fd whitelist/flags\n",steps);
    return 0;
}
