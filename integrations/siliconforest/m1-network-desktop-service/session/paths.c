#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "paths.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef DNDS_PATH_ROOT
#define DNDS_PATH_ROOT "/"
#endif
static int failed(struct dnds_paths *p) {if(!p->error)p->error=errno?errno:EIO;return -1;}
static int metadata(const struct stat *s,int private) {
 return S_ISDIR(s->st_mode)&&s->st_uid==0&&
  (private?(s->st_mode&07777)==0700:!(s->st_mode&0022));
}
static const char *component(const struct dnds_paths *p,unsigned i) {
 static const char *names[]={NULL,"private","var","run","darwin-desktop-network"};
 return i==5?p->session:names[i];
}
int dnds_paths_verify(struct dnds_paths *p) {
 if(!p||!p->initialized||p->error){errno=EINVAL;return -1;}
 for(unsigned i=0;i<6;i++) {
  struct stat a,b;
  if(p->fd[i]<0||fstat(p->fd[i],&a)||!metadata(&a,i>=4)){errno=EPERM;return failed(p);}
  if(i&&(fstatat(p->fd[i-1],component(p,i),&b,AT_SYMLINK_NOFOLLOW)||
      !metadata(&b,i>=4)||a.st_dev!=b.st_dev||a.st_ino!=b.st_ino)){errno=ESTALE;return failed(p);}
 }
 return 0;
}
int dnds_paths_create(struct dnds_paths *p,const char nonce[49]) {
 if(!p||p->initialized||!nonce||strlen(nonce)!=48){errno=EINVAL;return -1;}
 for(unsigned i=0;i<48;i++)if(!((nonce[i]>='0'&&nonce[i]<='9')||(nonce[i]>='a'&&nonce[i]<='f'))){errno=EINVAL;return -1;}
 p->initialized=1;
 for(unsigned i=0;i<6;i++)p->fd[i]=-1;
 if(snprintf(p->session,sizeof(p->session),"session-%s",nonce)!=56){errno=EINVAL;return failed(p);}
 p->fd[0]=open(DNDS_PATH_ROOT,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 for(unsigned i=0;i<6;i++) {
  if(i) {
   if(i>=4&&mkdirat(p->fd[i-1],component(p,i),0700))return failed(p);
   p->fd[i]=openat(p->fd[i-1],component(p,i),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  }
  struct stat a;
  if(p->fd[i]<0||fstat(p->fd[i],&a))return failed(p);
  if(!metadata(&a,i>=4)){errno=EPERM;return failed(p);}
 }
 return dnds_paths_verify(p);
}
int dnds_paths_close(struct dnds_paths *p) {
 if(!p||!p->initialized)return 0;
 for(unsigned n=6;n>0;n--) {
  unsigned i=n-1;
  if(p->fd[i]>=0){int fd=p->fd[i];p->fd[i]=-1;if(close(fd))failed(p);}
 }
 return p->error?-1:0;
}
