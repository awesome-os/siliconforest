/* SPDX-License-Identifier: MIT */
/* Dedicated disposable QEMU fixture only; all device opens are read-only. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#define BYTES (8u<<20)
#define CHUNK (512u<<10)
static unsigned char buffer[CHUNK];
static unsigned char expected(unsigned offset){return (unsigned char)(offset*37+(offset>>8)*19+13);}
int main(void){
 for(unsigned disk=0;disk<8;disk++){
  char path[64];snprintf(path,sizeof path,"/dev/rdisk%u",disk);int fd=open(path,O_RDONLY);if(fd<0)continue;
  if(pread(fd,buffer,4096,0)!=4096||memcmp(buffer,"SF_UPSTREAM_USB_FIXTURE_v1",26)){close(fd);continue;}
  for(unsigned off=0;off<BYTES;off+=CHUNK){
   if(pread(fd,buffer,CHUNK,off)!=CHUNK){fprintf(stderr,"USB_READ FAIL short offset=%u\n",off);close(fd);return 1;}
   for(unsigned n=0;n<CHUNK;n++)if(off+n>=4096&&buffer[n]!=expected(off+n)){
    fprintf(stderr,"USB_READ FAIL data offset=%u\n",off+n);close(fd);return 1;
   }
  }
  close(fd);printf("USB_READ PASS bytes=%u chunk=%u device=%s (synthetic QEMU media only)\n",BYTES,CHUNK,path);return 0;
 }
 fprintf(stderr,"USB_READ FAIL no synthetic USB medium\n");return 1;
}
