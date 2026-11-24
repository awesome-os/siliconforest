#ifdef NDEBUG
#error assertions required
#endif
#include "endpoint_owner.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#ifndef DLR_ROOT
#error private test root required
#endif
#define RUN DLR_ROOT "/var/run"
#define UDS RUN "/mDNSResponder"
#define PIDFILE RUN "/mDNSResponder.pid"
#define CONFIG DLR_ROOT "/etc/resolv.conf"
static void write_file(const char *path,const char *data) {int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);assert(write(fd,data,strlen(data))==(ssize_t)strlen(data));assert(close(fd)==0);}
static int endpoint(void) {int fd=socket(AF_UNIX,SOCK_STREAM,0);assert(fd>=0);struct sockaddr_un s;memset(&s,0,sizeof(s));s.sun_family=AF_UNIX;assert(strlen(UDS)<sizeof(s.sun_path));strcpy(s.sun_path,UDS);assert(bind(fd,(struct sockaddr *)&s,sizeof(s))==0);assert(listen(fd,2)==0);return fd;}
int main(void) {
 assert(geteuid()==0);assert(mkdir(DLR_ROOT,0700)==0);assert(mkdir(DLR_ROOT "/var",0755)==0);assert(mkdir(RUN,0755)==0);assert(mkdir(DLR_ROOT "/etc",0755)==0);write_file(CONFIG,"nameserver 10.0.2.3\n");
 struct dlr_endpoints e;memset(&e,0,sizeof(e));assert(!dlr_endpoints_preflight(&e));assert(dlr_endpoints_ready(&e,77)==0);int fd=endpoint();write_file(PIDFILE,"77\n");assert(dlr_endpoints_ready(&e,77)==1&&e.ready);assert(dlr_endpoints_cleanup(&e,0)<0);assert(!dlr_endpoints_cleanup(&e,1)&&e.cleaned&&e.socket.removed&&e.pidfile.removed);assert(close(fd)==0);assert(dlr_endpoints_cleanup(&e,1)<0);
 memset(&e,0,sizeof(e));assert(!dlr_endpoints_preflight(&e));fd=endpoint();write_file(PIDFILE,"77\n");assert(dlr_endpoints_ready(&e,77)==1);assert(unlink(UDS)==0);int replacement=endpoint();assert(dlr_endpoints_ready(&e,77)<0);assert(dlr_endpoints_cleanup(&e,1)<0&&!e.cleaned);assert(access(UDS,F_OK)==0);assert(unlink(UDS)==0);assert(close(fd)==0&&close(replacement)==0);
 write_file(PIDFILE,"88\n");memset(&e,0,sizeof(e));assert(dlr_endpoints_preflight(&e)<0);assert(unlink(PIDFILE)==0);
 assert(symlink(CONFIG,PIDFILE)==0);memset(&e,0,sizeof(e));assert(dlr_endpoints_preflight(&e)<0);assert(unlink(PIDFILE)==0);
 memset(&e,0,sizeof(e));assert(!dlr_endpoints_preflight(&e));fd=endpoint();write_file(PIDFILE,"88\n");assert(dlr_endpoints_ready(&e,77)<0);assert(dlr_endpoints_cleanup(&e,1)<0);assert(access(PIDFILE,F_OK)==0);assert(unlink(PIDFILE)==0);assert(close(fd)==0);
 memset(&e,0,sizeof(e));assert(!dlr_endpoints_preflight(&e));fd=endpoint();write_file(PIDFILE,"77\n");assert(dlr_endpoints_ready(&e,77)==1);assert(unlink(UDS)==0&&unlink(PIDFILE)==0);assert(!dlr_endpoints_cleanup(&e,1));assert(close(fd)==0);
 assert(chmod(CONFIG,0666)==0);memset(&e,0,sizeof(e));assert(dlr_endpoints_preflight(&e)<0);assert(chmod(CONFIG,0600)==0);
 memset(&e,0,sizeof(e));assert(!dlr_endpoints_preflight(&e));fd=endpoint();write_file(PIDFILE,"77\n");assert(dlr_endpoints_ready(&e,77)==1);assert(chmod(UDS,0700)==0);assert(dlr_endpoints_cleanup(&e,1)<0);assert(unlink(UDS)==0);assert(close(fd)==0);
 write_file(DLR_ROOT "/etc/mdnsd.conf","hostname evil.invalid\n");memset(&e,0,sizeof(e));assert(dlr_endpoints_preflight(&e)<0);assert(unlink(DLR_ROOT "/etc/mdnsd.conf")==0);
 assert(unlink(CONFIG)==0);assert(rmdir(DLR_ROOT "/etc")==0&&rmdir(RUN)==0&&rmdir(DLR_ROOT "/var")==0&&rmdir(DLR_ROOT)==0);
 puts("endpoint_owner: 9 real filesystem/socket ownership scenarios passed");return 0;
}
