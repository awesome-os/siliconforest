#ifdef NDEBUG
#error assertions required
#endif
#define _POSIX_C_SOURCE 200809L
#include "paths.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef DNDS_PATH_ROOT
#error test requires a private scratch root
#endif
static const char nonce[]="0123456789abcdef0123456789abcdef0123456789abcdef";
static char base[512],session[640],moved[640];
static void create_system(void) {
 char b[512];
 assert(snprintf(b,sizeof(b),"%s/private",DNDS_PATH_ROOT)>0);assert(!mkdir(b,0755));
 assert(snprintf(b,sizeof(b),"%s/private/var",DNDS_PATH_ROOT)>0);assert(!mkdir(b,0755));
 assert(snprintf(b,sizeof(b),"%s/private/var/run",DNDS_PATH_ROOT)>0);assert(!mkdir(b,0755));
}
static void remove_known(void) {
 /* Only these exact test-created directories; no production cleanup API. */
 (void)unlink(base);(void)rmdir(session);(void)rmdir(moved);(void)rmdir(base);
}
static void close_all(struct dnds_paths *p,int expected) {
 int f[6];memcpy(f,p->fd,sizeof(f));assert(dnds_paths_close(p)==expected);
 for(unsigned i=0;i<6;i++)if(f[i]>=0){errno=0;assert(fcntl(f[i],F_GETFD)<0&&errno==EBADF);}
}
int main(void) {
 assert(geteuid()==0);umask(0);assert(!chown(DNDS_PATH_ROOT,0,0));
 assert(snprintf(base,sizeof(base),"%s/private/var/run/darwin-desktop-network",DNDS_PATH_ROOT)>0);
 assert(snprintf(session,sizeof(session),"%s/session-%s",base,nonce)>0);
 assert(snprintf(moved,sizeof(moved),"%s/rebound",base)>0);
 create_system();struct dnds_paths p={0};
 assert(!dnds_paths_create(&p,nonce)&&!dnds_paths_verify(&p));
 for(unsigned i=0;i<6;i++)assert(fcntl(p.fd[i],F_GETFD)&FD_CLOEXEC);
 assert(dnds_paths_create(&p,nonce)<0);assert(!dnds_paths_verify(&p));close_all(&p,0);assert(!dnds_paths_close(&p));
 struct dnds_paths q={0};assert(dnds_paths_create(&q,nonce)<0&&q.error==EEXIST);close_all(&q,-1);
 struct stat st;assert(!lstat(session,&st)&&S_ISDIR(st.st_mode));remove_known();
 memset(&p,0,sizeof(p));assert(dnds_paths_create(&p,"BAD")<0&&!p.initialized);assert(!dnds_paths_close(&p));
 memset(&p,0,sizeof(p));assert(!dnds_paths_create(&p,nonce));assert(!chmod(session,0777));
 assert(dnds_paths_verify(&p)<0);close_all(&p,-1);remove_known();
 memset(&p,0,sizeof(p));assert(!dnds_paths_create(&p,nonce));assert(!rename(session,moved));assert(!mkdir(session,0700));
 assert(dnds_paths_verify(&p)<0);close_all(&p,-1);remove_known();
 assert(!symlink("/",base));memset(&p,0,sizeof(p));assert(dnds_paths_create(&p,nonce)<0);close_all(&p,-1);
 assert(!lstat(base,&st)&&S_ISLNK(st.st_mode));remove_known();
 char run[512];assert(snprintf(run,sizeof(run),"%s/private/var/run",DNDS_PATH_ROOT)>0);
 assert(!chmod(run,0777));memset(&p,0,sizeof(p));assert(dnds_paths_create(&p,nonce)<0);close_all(&p,-1);
 assert(lstat(base,&st)<0&&errno==ENOENT);assert(!chmod(run,0755));
 char outside[512];assert(snprintf(outside,sizeof(outside),"%s/outside",DNDS_PATH_ROOT)>0);assert(!mkdir(outside,0755));
 assert(!rmdir(run));assert(!symlink(outside,run));memset(&p,0,sizeof(p));assert(dnds_paths_create(&p,nonce)<0);close_all(&p,-1);
 char escaped[640];assert(snprintf(escaped,sizeof(escaped),"%s/darwin-desktop-network",outside)>0);
 assert(lstat(escaped,&st)<0&&errno==ENOENT);
 assert(!unlink(run));assert(!mkdir(run,0755));
 memset(&p,0,sizeof(p));assert(!dnds_paths_create(&p,nonce));assert(!close(p.fd[5]));close_all(&p,-1);remove_known();
 puts("paths: 8 root-owned filesystem/retirement scenarios passed");return 0;
}
