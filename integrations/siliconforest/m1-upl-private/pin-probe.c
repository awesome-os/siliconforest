/* SPDX-License-Identifier: MIT */
/* Root-only native GPU-BO pin/unpin gate. No decoder or GPU job is submitted. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "asahi_drm.h"
#include <sfavd/sfavd.h>
#include "sfavd_rec.h"
#define PAGE16 ((size_t)16384)
static void die(const char *s){fprintf(stderr,"PIN_ONLY_FAIL %s errno=%d (%s)\n",s,errno,strerror(errno));exit(1);}
static void canary(const uint8_t *p,size_t size){for(size_t j=0;j<64;j++)if(p[j]!=0xa5 || p[PAGE16-64+j]!=0xa5 || p[PAGE16+j]!=0 || p[size-64+j]!=0)die("header changed or DATA not initialized");}
int main(int argc,char **argv){
 char uuid[128]={0};size_t un=sizeof(uuid);
 if(argc!=5 || strlen(argv[1])!=36 || strncmp(argv[2],"/dev/dri/renderD",16) || atoi(argv[4])<1 || atoi(argv[4])>10)return 2;
 if(sysctlbyname("kern.kernelcacheuuid",uuid,&un,NULL,0)||strcasecmp(uuid,argv[1])){fprintf(stderr,"REFUSED wrong fixed kernel %s\n",uuid);return 2;}
 alarm(30);setbuf(stdout,NULL);setbuf(stderr,NULL);
 sfavd_dev *dev=sfavd_open(NULL);struct sfavd_info info;
 if(!dev || sfavd_info(dev,&info))die("AVD INFO");
 if(!(info.caps&SFAVD_CAP_EXTERNAL_BUFFERS)){fprintf(stderr,"REFUSED no import capability; no GPU node opened\n");return 2;}
 FILE *f=fopen(argv[3],"rb");struct sfavd_rec rec={0};
 if(!f || sfavd_rec_read(f,&rec)!=1 || rec.type!=SFAVD_REC_CREATE)die("recording first CREATE");
 fclose(f);int fd=open(argv[2],O_RDWR|O_CLOEXEC);if(fd<0)die("render open");
 for(int round=0;round<atoi(argv[4]);round++){
  struct sfavd_create c=rec.create;c.ctx=0;c.format_ctrl.ptr=(uintptr_t)rec.format_ctrl;
  if(sfavd_create(dev,&c))die("context CREATE");
  if(!c.buffers || c.buffers>SFAVD_MAX_BUFFERS || !c.format.sizeimage || c.format.sizeimage>(128u<<20))die("format bounds");
  size_t bytes=PAGE16+(((size_t)c.format.sizeimage+PAGE16-1)&~(PAGE16-1));
  struct sfavd_import_buffer imports[SFAVD_MAX_BUFFERS]={0};uint32_t handles[SFAVD_MAX_BUFFERS]={0};uint8_t *maps[SFAVD_MAX_BUFFERS]={0};
  for(unsigned i=0;i<c.buffers;i++){
   struct drm_asahi_gem_create g={.size=bytes,.flags=DRM_ASAHI_GEM_WRITEBACK};if(ioctl(fd,DRM_IOCTL_ASAHI_GEM_CREATE,&g))die("GEM CREATE");handles[i]=g.handle;
   struct drm_asahi_gem_mmap_offset mo={.handle=g.handle};if(ioctl(fd,DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET,&mo))die("GEM MMAP OFFSET");
   maps[i]=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t)mo.offset);if(maps[i]==MAP_FAILED)die("original mmap");
   uint8_t *alias=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t)mo.offset);if(alias==MAP_FAILED || alias==maps[i])die("distinct duplicate mmap");
   memset(maps[i],0xa5,PAGE16);memset(maps[i]+PAGE16,0x5a,bytes-PAGE16);
   if(alias[0]!=0xa5 || alias[PAGE16]!=0x5a || munmap(alias,bytes))die("duplicate map alias/unmap");
   if(maps[i][0]!=0xa5 || maps[i][PAGE16]!=0x5a)die("original invalid after alias unmap");
   imports[i].address=(uintptr_t)maps[i]+PAGE16;imports[i].size=bytes-PAGE16;
  }
  printf("PIN_ONLY_IMPORT round=%d ctx=%u buffers=%u sizeimage=%u DATA16K=1 WB=1 no_jobs=1\n",round,c.ctx,c.buffers,c.format.sizeimage);
  (void)fsync(STDOUT_FILENO);
  if(sfavd_import_buffers(dev,c.ctx,imports,c.buffers))die("atomic GPU DATA IMPORT");
  for(unsigned i=0;i<c.buffers;i++)canary(maps[i],bytes);
  printf("PIN_ONLY_DESTROY round=%d imported=1 before_complete=1\n",round);
  (void)fsync(STDOUT_FILENO);
  if(sfavd_destroy(dev,c.ctx))die("context DESTROY/complete");
  for(unsigned i=0;i<c.buffers;i++){
   canary(maps[i],bytes);if(munmap(maps[i],bytes))die("original munmap");
   struct drm_gem_close cl={.handle=handles[i]};if(ioctl(fd,DRM_IOCTL_GEM_CLOSE,&cl))die("GEM close");
  }
  if(sfavd_info(dev,&info) || info.contexts || !(info.caps&SFAVD_CAP_EXTERNAL_BUFFERS))die("postdestroy context/cap");
  printf("PIN_ONLY_ROUND PASS round=%d buffers=%u duplicate_maps_distinct=1 original_alive=1 header_preserved=1 imported_destroyed=1\n",round,c.buffers);
 }
 close(fd);sfavd_close(dev);free(rec.body);puts("PIN_ONLY PASS no_gpu_or_decoder_jobs=1");return 0;
}
