/* SPDX-License-Identifier: MIT */
/* Run only against the reviewed fixed image; all child processes are ours. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
struct counts {unsigned used,scan,capacity,peak;unsigned long long alloc,freed,fail;};
static int snapshot(struct counts *s){
 char b[256]={0};size_t n=sizeof(b);
 if(sysctlbyname("kern.sf_dri.clone_slots",b,&n,NULL,0)){perror("clone_slots");return 1;}
 if(sscanf(b,"used=%u scan=%u capacity=%u allocations=%llu frees=%llu failures=%llu peak=%u",&s->used,&s->scan,&s->capacity,&s->alloc,&s->freed,&s->fail,&s->peak)!=7 || s->used!=s->scan || s->capacity!=1023 || s->alloc-s->freed!=s->used){fprintf(stderr,"invalid clone snapshot: %s\n",b);return 1;}
 printf("CLONE_SNAPSHOT %s\n",b);fflush(stdout);return 0;
}
static int worker(const char *path,unsigned index){
 alarm(90);
 for(unsigned i=0;i<5000;i++){struct stat s;if(stat(path,&s)||!S_ISCHR(s.st_mode)){fprintf(stderr,"worker%u stat%u errno%d\n",index,i,errno);return 1;}}
 for(unsigned i=0;i<5000;i++){struct stat s;int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0){fprintf(stderr,"worker%u open%u errno%d\n",index,i,errno);return 1;}int e=fstat(fd,&s);int c=close(fd);if(e||c||!S_ISCHR(s.st_mode)){fprintf(stderr,"worker%u fstat/close%u\n",index,i);return 1;}}
 for(unsigned i=0;i<5000;i++){int fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd>=0){close(fd);fprintf(stderr,"unexpected directory open\n");return 1;}if(errno!=ENOTDIR){fprintf(stderr,"worker%u failedopen%u errno%d\n",index,i,errno);return 1;}}
 printf("CLONE_WORKER PASS index=%u stat=5000 openclose=5000 failedopen=5000\n",index);fflush(stdout);return 0;
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char **argv){
 char uuid[128]={0};size_t n=sizeof(uuid);struct counts before,after;
 if(argc!=3 || strlen(argv[1])!=36 || strncmp(argv[2],"/dev/dri/renderD",16)){fprintf(stderr,"usage: clone-probe FIXED_KERNEL_UUID /dev/dri/renderD128\n");return 2;}
 if(sysctlbyname("kern.kernelcacheuuid",uuid,&n,NULL,0) || strcasecmp(uuid,argv[1])){fprintf(stderr,"refused actual kernel %s\n",uuid);return 2;}
 if(snapshot(&before))return 1;
 pid_t p[4]={0};unsigned remaining=0;int failed=0;
 for(unsigned i=0;i<4;i++){p[i]=fork();if(p[i]<0){perror("fork");failed=1;break;}if(!p[i])_exit(worker(argv[2],i));remaining++;}
 double deadline=now()+100;
 while(remaining && now()<deadline){for(unsigned i=0;i<4;i++)if(p[i]>0){int s;pid_t r=waitpid(p[i],&s,WNOHANG);if(r==p[i]){if(!WIFEXITED(s)||WEXITSTATUS(s))failed=1;p[i]=0;remaining--;}}if(remaining)usleep(10000);}
 if(remaining){for(unsigned i=0;i<4;i++)if(p[i]>0)kill(p[i],SIGKILL);fprintf(stderr,"FAIL child timeout remaining=%u\n",remaining);return 1;}
 usleep(300000);if(snapshot(&after))return 1;
 if(after.used!=before.used || after.fail!=before.fail){fprintf(stderr,"FAIL clone count drift before=%u after=%u failures=%llu\n",before.used,after.used,after.fail-before.fail);failed=1;}
 printf("DEVFS_STRESS %s workers=4 stat=20000 openclose=20000 failedopen=20000 allocations=%llu frees=%llu used_before=%u used_after=%u\n",failed?"FAIL":"PASS",after.alloc-before.alloc,after.freed-before.freed,before.used,after.used);
 return failed?1:0;
}
