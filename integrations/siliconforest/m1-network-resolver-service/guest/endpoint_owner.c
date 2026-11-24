#include "endpoint_owner.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef DLR_ROOT
#define DLR_ROOT ""
#endif
#define RUN DLR_ROOT "/var/run"
#define UDS RUN "/mDNSResponder"
#define PIDFILE RUN "/mDNSResponder.pid"
#define CONFIG DLR_ROOT "/etc/resolv.conf"
#define DDNS DLR_ROOT "/etc/mdnsd.conf"
static int bad(struct dlr_endpoints *e,int rc){if(!e->error)e->error=rc?rc:EIO;return -1;}
static int absent(const char *p) {struct stat s;return lstat(p,&s)<0&&errno==ENOENT;}
static int read_regular(const char *p,char *out,size_t cap,struct stat *st) {
 int fd=open(p,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return -1;
 int ok=fstat(fd,st)==0&&S_ISREG(st->st_mode)&&st->st_nlink==1&&st->st_uid==0&&!(st->st_mode&0022)&&st->st_size>0&&(uint64_t)st->st_size<cap;
 size_t used=0;
 while(ok&&used<(size_t)st->st_size){ssize_t n=read(fd,out+used,(size_t)st->st_size-used);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}used+=(size_t)n;}
 char extra;ssize_t n=ok?read(fd,&extra,1):-1;struct stat after;
 if(!ok||n!=0||fstat(fd,&after)||after.st_dev!=st->st_dev||after.st_ino!=st->st_ino||after.st_size!=st->st_size||after.st_mode!=st->st_mode||after.st_uid!=st->st_uid||after.st_nlink!=1)ok=0;
 if(close(fd))ok=0;
 if(ok)out[used]=0;
 return ok?0:-1;
}
static int same(const struct dlr_endpoint_identity *i,const struct stat *s) {
 return i->seen&&i->device==(uint64_t)s->st_dev&&i->inode==(uint64_t)s->st_ino&&i->uid==(uint32_t)s->st_uid&&i->mode==(uint32_t)s->st_mode&&s->st_nlink==1;
}
static void identity(struct dlr_endpoint_identity *i,const struct stat *s) {
 i->device=(uint64_t)s->st_dev;i->inode=(uint64_t)s->st_ino;i->mode=(uint32_t)s->st_mode;i->uid=(uint32_t)s->st_uid;i->seen=1;
}
static int pid_contents(int32_t pid,struct stat *st) {
 char actual[32],want[32];int n=snprintf(want,sizeof(want),"%d\n",pid);
 return n>0&&(size_t)n<sizeof(want)&&read_regular(PIDFILE,actual,sizeof(actual),st)==0&&!strcmp(actual,want);
}
int dlr_endpoints_preflight(struct dlr_endpoints *e) {
 if(!e||e->initialized)return -1;e->initialized=1;
 struct stat run,conf;char data[128];
 if(lstat(RUN,&run)||!S_ISDIR(run.st_mode)||run.st_uid||run.st_mode&0022)return bad(e,EPERM);
 if(!absent(UDS)||!absent(PIDFILE)||!absent(DDNS))return bad(e,EEXIST);
 if(read_regular(CONFIG,data,sizeof(data),&conf)||strcmp(data,"nameserver 10.0.2.3\n"))return bad(e,EINVAL);
 return 0;
}
int dlr_endpoints_ready(struct dlr_endpoints *e,int32_t pid) {
 if(!e||!e->initialized||e->error||pid<2||(e->pid&&e->pid!=pid))return -1;
 e->pid=pid;struct stat s,p;
 if(lstat(UDS,&s))return errno==ENOENT&&!e->socket.seen?0:bad(e,errno);
 if(!S_ISSOCK(s.st_mode)||s.st_uid||s.st_nlink!=1)return bad(e,EPERM);
 if(e->socket.seen&&!same(&e->socket,&s))return bad(e,ESTALE);
 if(!e->socket.seen)identity(&e->socket,&s);
 if(lstat(PIDFILE,&p))return errno==ENOENT&&!e->pidfile.seen?0:bad(e,errno);
 if(!pid_contents(pid,&p))return bad(e,EINVAL);
 if(e->pidfile.seen&&!same(&e->pidfile,&p))return bad(e,ESTALE);
 if(!e->pidfile.seen)identity(&e->pidfile,&p);
 e->ready=1;return 1;
}
static int cleanup_one(struct dlr_endpoints *e,const char *path,struct dlr_endpoint_identity *id,int is_pid) {
 struct stat s;
 if(lstat(path,&s)){if(errno==ENOENT){id->removed=1;return 0;}return bad(e,errno);}
 if(!same(id,&s))return bad(e,ESTALE);
 if(is_pid&&(!pid_contents(e->pid,&s)||!same(id,&s)))return bad(e,ESTALE);
 if(unlink(path))return bad(e,errno);
 if(!absent(path))return bad(e,ESTALE);
 id->removed=1;return 0;
}
int dlr_endpoints_cleanup(struct dlr_endpoints *e,int reaped) {
 if(!e||!e->initialized||!reaped||e->cleaned)return -1;
 int a=cleanup_one(e,UDS,&e->socket,0),b=cleanup_one(e,PIDFILE,&e->pidfile,1);
 e->cleaned=a==0&&b==0&&e->socket.removed&&e->pidfile.removed;
 return e->cleaned?0:-1;
}
