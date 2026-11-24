/* SPDX-License-Identifier: MIT */
/* Owned producer unmaps and dies while its GPU BO consumer survives. No jobs. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include "asahi_drm.h"
#include <sfavd/sfavd.h>
#include "sfavd_rec.h"
#define PAGE16 ((size_t)16384)
static pid_t child_pid=-1;
static double now(void);
static void deadline(int sig){(void)sig;if(child_pid>1)kill(child_pid,SIGKILL);_exit(124);}
static void die(const char *why){fprintf(stderr,"TASK_PIN FAIL %s errno=%d (%s)\n",why,errno,strerror(errno));if(child_pid>1){kill(child_pid,SIGKILL);double until=now()+2.0;int status;while(waitpid(child_pid,&status,WNOHANG)==0&&now()<until)usleep(2000);}exit(1);}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void canary(uint8_t *p,size_t bytes,uint8_t data){for(size_t j=0;j<64;j++)if(p[j]!=0xa5||p[PAGE16-64+j]!=0xa5||p[PAGE16+j]!=data||p[bytes-64+j]!=data)die("surviving header/DATA");}
int main(int argc,char **argv){
 char uuid[128]={0};size_t un=sizeof uuid;
 if(argc!=5||strlen(argv[1])!=36||strncmp(argv[2],"/dev/dri/renderD",16)||atoi(argv[4])<1||atoi(argv[4])>3)return 2;
 if(sysctlbyname("kern.kernelcacheuuid",uuid,&un,NULL,0)||strcasecmp(uuid,argv[1])){fprintf(stderr,"REFUSED wrong fixed kernel %s\n",uuid);return 2;}
 setbuf(stdout,NULL);setbuf(stderr,NULL);signal(SIGALRM,deadline);alarm(30);
 FILE *f=fopen(argv[3],"rb");struct sfavd_rec rec={0};
 if(!f||sfavd_rec_read(f,&rec)!=1||rec.type!=SFAVD_REC_CREATE)die("recording CREATE");fclose(f);
 sfavd_dev *probe=sfavd_open(NULL);struct sfavd_info info;
 if(!probe||sfavd_info(probe,&info))die("INFO");
 if(!(info.caps&SFAVD_CAP_EXTERNAL_BUFFERS)||info.contexts){fprintf(stderr,"REFUSED import unavailable or decoder already has a context\n");sfavd_close(probe);return 77;}
 struct sfavd_create layout=rec.create;layout.ctx=0;layout.format_ctrl.ptr=(uintptr_t)rec.format_ctrl;
 if(sfavd_create(probe,&layout))die("layout CREATE");
 if(!layout.buffers||layout.buffers>SFAVD_MAX_BUFFERS||!layout.format.sizeimage||layout.format.sizeimage>(128u<<20))die("layout bounds");
 if(sfavd_destroy(probe,layout.ctx))die("layout DESTROY");sfavd_close(probe); /* no AVD FD inherited by child */
 int fd=open(argv[2],O_RDWR|O_CLOEXEC);if(fd<0)die("render open");
 const size_t bytes=PAGE16+(((size_t)layout.format.sizeimage+PAGE16-1)&~(PAGE16-1));
 if(bytes>(512u<<20)/layout.buffers)die("bounded total BO bytes");
 for(int round=0;round<atoi(argv[4]);round++){
  uint32_t handles[SFAVD_MAX_BUFFERS]={0};uint8_t *maps[SFAVD_MAX_BUFFERS]={0};struct sfavd_import_buffer imports[SFAVD_MAX_BUFFERS]={0};
  for(unsigned i=0;i<layout.buffers;i++){
   struct drm_asahi_gem_create g={.size=bytes,.flags=DRM_ASAHI_GEM_WRITEBACK};if(ioctl(fd,DRM_IOCTL_ASAHI_GEM_CREATE,&g))die("GEM CREATE");handles[i]=g.handle;
   struct drm_asahi_gem_mmap_offset mo={.handle=g.handle};if(ioctl(fd,DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET,&mo))die("GEM offset");
   maps[i]=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t)mo.offset);if(maps[i]==MAP_FAILED)die("consumer mmap");
   memset(maps[i],0xa5,PAGE16);memset(maps[i]+PAGE16,0x5a,bytes-PAGE16);imports[i].address=(uintptr_t)maps[i]+PAGE16;imports[i].size=bytes-PAGE16;
  }
  int ready[2];if(pipe(ready))die("pipe");child_pid=fork();if(child_pid<0)die("fork");
  if(!child_pid){
   close(ready[0]);sfavd_dev *dev=sfavd_open(NULL);struct sfavd_create c=rec.create;c.ctx=0;c.format_ctrl.ptr=(uintptr_t)rec.format_ctrl;
   if(!dev||sfavd_create(dev,&c)||c.buffers!=layout.buffers||c.format.sizeimage!=layout.format.sizeimage){(void)!write(ready[1],"E",1);_exit(3);}
   printf("TASK_PIN_IMPORT round=%d pid=%d ctx=%u buffers=%u no_jobs=1\n",round,getpid(),c.ctx,c.buffers);(void)fsync(STDOUT_FILENO);
   if(sfavd_import_buffers(dev,c.ctx,imports,c.buffers)){(void)!write(ready[1],"E",1);_exit(4);}
   for(unsigned i=0;i<c.buffers;i++)if(munmap(maps[i],bytes)){(void)!write(ready[1],"E",1);_exit(5);}
   close(fd);printf("TASK_PIN_UNMAPPED pid=%d imported=1 own_GPU_VAs=0 AVD_FD_alive=1\n",getpid());(void)fsync(STDOUT_FILENO);
   (void)!write(ready[1],"R",1);close(ready[1]);for(;;)pause(); /* parent kills only this known child */
  }
  close(ready[1]);struct pollfd event={.fd=ready[0],.events=POLLIN};char ready_byte=0;
  if(poll(&event,1,5000)!=1||read(ready[0],&ready_byte,1)!=1||ready_byte!='R')die("producer import/unmap not ready");close(ready[0]);
  probe=sfavd_open(NULL);if(!probe||sfavd_info(probe,&info)||info.contexts!=1||!(info.caps&SFAVD_CAP_EXTERNAL_BUFFERS))die("live imported child context");
  for(unsigned i=0;i<layout.buffers;i++)canary(maps[i],bytes,0);
  printf("TASK_PIN_KILL round=%d owned_pid=%d consumer_alive=1\n",round,child_pid);(void)fsync(STDOUT_FILENO);
  double start=now();if(kill(child_pid,SIGKILL))die("own child kill");int status=0;
  for(;;){pid_t r=waitpid(child_pid,&status,WNOHANG);if(r==child_pid)break;if(r<0||now()-start>2.0)die("producer did not reap within2s");usleep(2000);}
  double elapsed=(now()-start)*1000;child_pid=-1;
  if(!WIFSIGNALED(status)||WTERMSIG(status)!=SIGKILL)die("producer status");
  int info_rc=sfavd_info(probe,&info),info_errno=errno;
  printf("TASK_PIN_POSTEXIT round=%d info_rc=%d errno=%d contexts=%u caps=0x%x reap_ms=%.2f\n",round,info_rc,info_errno,info.contexts,info.caps,elapsed);
  if(info_rc||info.contexts>1||!(info.caps&SFAVD_CAP_EXTERNAL_BUFFERS))die("postexit INFO/capability");
  for(unsigned i=0;i<layout.buffers;i++){
   canary(maps[i],bytes,0);
   /* Producer is dead, no jobs were submitted, and no decoder can write
    * these captures. Mark the surviving consumer before real retirement. */
   memset(maps[i]+PAGE16,0x3c,64);memset(maps[i]+bytes-64,0x3c,64);
  }
  struct sfavd_create next=rec.create;next.ctx=0;next.format_ctrl.ptr=(uintptr_t)rec.format_ctrl;
  printf("TASK_PIN_REAP_TRIGGER round=%d next_CREATE=1 (documented dead-context reaper)\n",round);(void)fsync(STDOUT_FILENO);
  if(sfavd_create(probe,&next))die("documented CREATE reaper");
  for(unsigned i=0;i<layout.buffers;i++)canary(maps[i],bytes,0x3c);
  if(sfavd_destroy(probe,next.ctx)||sfavd_info(probe,&info)||info.contexts||!(info.caps&SFAVD_CAP_EXTERNAL_BUFFERS))die("postreap context/import capability");sfavd_close(probe);
  for(unsigned i=0;i<layout.buffers;i++){canary(maps[i],bytes,0x3c);if(munmap(maps[i],bytes))die("consumer unmap");struct drm_gem_close c={.handle=handles[i]};if(ioctl(fd,DRM_IOCTL_GEM_CLOSE,&c))die("consumer GEM close");}
  printf("TASK_PIN_ROUND PASS round=%d producer_unmapped=1 producer_killed=1 consumer_survived_after_real_CREATE_reap=1 contexts=0 reap_ms=%.2f\n",round,elapsed);
 }
 close(fd);free(rec.body);alarm(0);puts("TASK_PIN PASS no_gpu_or_decoder_jobs=1; no GPU-fence proof");return 0;
}
