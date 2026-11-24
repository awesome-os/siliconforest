/* SPDX-License-Identifier: MIT */
/* QEMU scratch filesystem only. The host checks bytes and e2fsck after unmount. */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void fail(const char *s){fprintf(stderr,"EOF_IO FAIL %s errno=%d (%s)\n",s,errno,strerror(errno));exit(1);}
static uint8_t pattern(size_t n){return (uint8_t)((n*37u+(n>>8)*19u+13u)&255u);}
static void verify(int fd,size_t size){uint8_t buf[4096];size_t off=0;struct stat st;if(fstat(fd,&st)||st.st_size!=(off_t)size)fail("size");while(off<size){size_t count=size-off;if(count>sizeof buf)count=sizeof buf;ssize_t got=pread(fd,buf,count,(off_t)off);if(got!=(ssize_t)count)fail("pread");for(size_t n=0;n<count;n++)if(buf[n]!=pattern(off+n))fail("readback bytes");off+=count;}}
int main(int argc,char **argv){
 char args[1024]={0};size_t n=sizeof args;setbuf(stdout,NULL);
 const char *root="/tmp/eofscratch";
 if(argc==1){
  if(sysctlbyname("kern.bootargs",args,&n,NULL,0)||!strstr(args,"sfqemu_eof=1")){fprintf(stderr,"REFUSED: QEMU scratch boot flag required\n");return 77;}
 }else if(argc==3){
  /* Native mode: exact runtime collection, fresh uniquely named own directory. */
  const char *prefix="/private/var/db/sftb-ext4-eof-";size_t len=strlen(prefix);
  if(sysctlbyname("kern.kernelcacheuuid",args,&n,NULL,0)||strcmp(args,argv[1])||strncmp(argv[2],prefix,len)||!argv[2][len]){fprintf(stderr,"REFUSED: wrong kernel or native prefix\n");return 77;}
  for(const char *p=argv[2]+len;*p;p++)if(*p<'0'||*p>'9'){fprintf(stderr,"REFUSED: native suffix must be digits\n");return 77;}
  root=argv[2];if(strlen(root)>128||mkdir(root,0700))fail("fresh native directory");
 }else{fprintf(stderr,"usage: eof-io [KERNELCACHE_UUID /private/var/db/sftb-ext4-eof-DIGITS]\n");return 77;}
 struct statfs fs;if(getpagesize()!=16384||statfs(root,&fs)||fs.f_bsize!=4096||strcmp(fs.f_fstypename,"ext4")){fprintf(stderr,"REFUSED: require16KiBVM/4KiBext4FS\n");return 77;}
 printf("EOF_IO ENV pagesize=%d fsblock=%u\n",getpagesize(),(unsigned)fs.f_bsize);
 const size_t sizes[]={1,4095,4096,4097,16383,16384,16385,49151,49152,49153,52208,65535,65536,65537};
 unsigned cases=0;
 for(int mode=0;mode<2;mode++)for(size_t c=0;c<sizeof sizes/sizeof sizes[0];c++){
  size_t size=sizes[c],rounded=(size+16383)&~(size_t)16383;char name[256];snprintf(name,sizeof name,"%s/%c-%zu",root,mode?'w':'m',size);
  int fd=open(name,O_CREAT|O_EXCL|O_RDWR,0600);if(fd<0)fail("create");
  if(!mode){
   if(ftruncate(fd,(off_t)size))fail("ftruncate");uint8_t *p=mmap(NULL,rounded,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(p==MAP_FAILED)fail("mmap");
   for(size_t i=0;i<size;i++)p[i]=pattern(i);
   if(msync(p,rounded,MS_SYNC)||munmap(p,rounded))fail("msync/unmap");
  }else{
   uint8_t buf[4096];size_t off=0;while(off<size){size_t count=size-off;if(count>sizeof buf)count=sizeof buf;for(size_t i=0;i<count;i++)buf[i]=pattern(off+i);ssize_t written=write(fd,buf,count);if(written!=(ssize_t)count)fail("write");off+=count;}
  }
  if(fsync(fd))fail("fsync");verify(fd,size);struct stat st;if(fstat(fd,&st))fail("stat");if(close(fd))fail("close");
  fd=open(name,O_RDONLY);if(fd<0)fail("reopen");verify(fd,size);close(fd);
  printf("EOF_IO FILE %c-%zu size=%zu blocks512=%lld\n",mode?'w':'m',size,size,(long long)st.st_blocks);cases++;
 }
 /* Exercise shrink then grow, and repeated dirty partial tails. */
 const size_t sequence[]={65537,52208,49153,4097,1,16385,52208};
 char name[256];snprintf(name,sizeof name,"%s/resized",root);
 int fd=open(name,O_CREAT|O_EXCL|O_RDWR,0600);if(fd<0)fail("resize create");
 for(size_t c=0;c<sizeof sequence/sizeof sequence[0];c++){
  size_t size=sequence[c],rounded=(size+16383)&~(size_t)16383;if(ftruncate(fd,(off_t)size))fail("resize truncate");
  uint8_t *p=mmap(NULL,rounded,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(p==MAP_FAILED)fail("resize mmap");for(size_t i=0;i<size;i++)p[i]=pattern(i);
  if(msync(p,rounded,MS_SYNC)||munmap(p,rounded)||fsync(fd))fail("resize flush");verify(fd,size);
 }
 close(fd);puts("EOF_IO FILE resized size=52208");printf("EOF_IO PASS cases=%u resize_steps=7\n",cases);return 0;
}
