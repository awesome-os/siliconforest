/* SPDX-License-Identifier: MIT */
/* Literal reader, fake device syscalls; no disk is opened. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define MAXPATHLEN 1024
#define EXT4_SUPERBLOCK_OFFSET 1024
#define DKIOCGETBLOCKSIZE 1
@SUPERBLOCK@
static unsigned char device[131072];
static uint32_t blocksize,reported;
static int opens,closes,reads,allocs,ioctl_error,short_read,oom;
static int fake_open(const char *path,int flags){assert(flags==O_RDONLY);opens++;return path[0]=='/'?7:-1;}
static int fake_close(int fd){assert(fd==7);closes++;return 0;}
static int fake_ioctl(int fd,unsigned request,uint32_t *out){assert(fd==7&&request==DKIOCGETBLOCKSIZE);*out=reported;return ioctl_error?-1:0;}
static ssize_t fake_pread(int fd,void *out,size_t size,off_t offset){
 assert(fd==7);reads++;if(offset%blocksize||size%blocksize){errno=EINVAL;return -1;}
 assert(offset>=0&&(size_t)offset+size<=sizeof device);memcpy(out,device+offset,size);return (ssize_t)size-short_read;
}
static void *fake_malloc(size_t bytes){allocs++;return oom?NULL:malloc(bytes);}
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define pread fake_pread
#define malloc fake_malloc
@READER@
#undef open
#undef close
#undef ioctl
#undef pread
#undef malloc
static void reset(uint32_t size){blocksize=reported=size;opens=closes=reads=allocs=0;ioctl_error=short_read=oom=0;}
int main(void){
 for(unsigned i=0;i<sizeof device;i++)device[i]=(unsigned char)(i*37+(i>>8));
 for(unsigned size=512;size<=65536;size*=2){
  reset(size);struct ext4_super_block sb={0};int rc=read_superblock("disk-test",&sb);
#ifdef FROZEN
  assert(rc==(size<=1024?0:-1));
#else
  assert(rc==0);
#endif
  if(!rc)assert(!memcmp(&sb,device+1024,sizeof sb));assert(closes==1&&opens==1&&reads==1);
 }
#ifndef FROZEN
 for(unsigned invalid=0;invalid<2;invalid++){reset(512);reported=invalid?65537:0;struct ext4_super_block sb;assert(!read_superblock("disk-test",&sb));}
 reset(512);ioctl_error=1;struct ext4_super_block sb;assert(!read_superblock("disk-test",&sb));
 reset(4096);ioctl_error=1;assert(read_superblock("disk-test",&sb)==-1); /* truthful unsupported fallback */
 reset(4096);oom=1;assert(read_superblock("disk-test",&sb)==-1&&closes==1&&!reads);
 reset(4096);short_read=1;memset(&sb,0xa5,sizeof sb);struct ext4_super_block before=sb;
 assert(read_superblock("disk-test",&sb)==-1&&closes==1&&!memcmp(&sb,&before,sizeof sb));
 puts("PASS patched ext4.util actual reader: 512..65536-byte devices, whole reads, fallback, OOM and short-read failure");
#else
 puts("PASS frozen counterexample: actual 1024-byte/offset1024 read rejected by 4096-byte fake device");
#endif
}
