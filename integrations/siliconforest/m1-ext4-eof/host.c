/* SPDX-License-Identifier: MIT */
/* Actual blockmap code with a bounded allocation recorder, not a filesystem. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#define PAGE_SIZE 16384
#define PAGE_MASK (PAGE_SIZE-1)
#define VNODE_WRITE 2
#define VNODE_READ 1
#define VTOE(vp) ((vp)->node)
typedef int64_t daddr64_t;
typedef struct {} IOLock;
static void IOLockLock(IOLock *p){(void)p;}
static void IOLockUnlock(IOLock *p){(void)p;}
struct ext4mount{uint32_t em_blocksize,em_dev_bsize;};
struct ext4node{struct ext4mount *e_mount;uint64_t e_ino,e_size,e_new_size;bool e_freed;int e_raw;void *e_lock;};
struct vnode{struct ext4node *node;bool dir;};
struct vnop_blockmap_args{struct vnode *a_vp;daddr64_t *a_bpn;off_t a_foffset;size_t a_size,*a_run;int a_flags;};
static bool blocks[64];
static int allocations;
static bool vnode_isdir(struct vnode *v){return v->dir;}
static int ext4_bmap_run(struct ext4mount *m,uint64_t ino,int *raw,uint32_t start,uint32_t want,uint64_t *phys,uint32_t *run,int *unwritten){
 (void)m;(void)ino;(void)raw;assert(start<64&&want&&want<=64-start);*unwritten=0;*phys=blocks[start]?100+start:0;*run=1;
 while(*run<want&&blocks[start+*run]==blocks[start])(*run)++;return 0;
}
static int ext4_alloc_range(struct ext4node *e,uint32_t start,uint32_t count,bool *grew){
 (void)e;assert(start<64&&count<=64-start);for(uint32_t n=start;n<start+count;n++)if(!blocks[n]){blocks[n]=true;allocations++;*grew=true;}return 0;
}
static int ext4_write_inode(struct ext4mount *m,uint64_t ino,int *raw){(void)m;(void)ino;(void)raw;return 0;}
@LIMIT@
@BLOCKMAP@
static int round_request(int non_rounded_size){int pg_offset=0,size;@ROUND@ return size;}
static int remaining_after(int non_rounded_size,int io_size,int size){size-=io_size;@DECREMENT@ @TAILSTOP@ return size;}
static void check(uint64_t eof,uint64_t growing,uint64_t offset,bool existing,int flags){
 memset(blocks,0,sizeof blocks);allocations=0;struct ext4mount m={4096,512};struct ext4node e={.e_mount=&m,.e_size=eof,.e_new_size=growing};struct vnode v={.node=&e};
 uint64_t limit=eof>growing?eof:growing;assert(offset<limit);int actual=(int)(limit-offset);int requested=round_request(actual);uint32_t first=(uint32_t)(offset/4096),last=(uint32_t)((limit+4095)/4096);
 if(existing)for(unsigned n=first;n<first+(unsigned)requested/4096;n++)blocks[n]=true;
 size_t run=0;daddr64_t phys=-2;struct vnop_blockmap_args a={.a_vp=&v,.a_bpn=&phys,.a_foffset=(off_t)offset,.a_size=(size_t)requested,.a_run=&run,.a_flags=flags};
 assert(!ext4_vnop_blockmap_impl(&a));
 if(flags&VNODE_WRITE){
  for(unsigned n=last;n<64;n++)assert(existing||!blocks[n]); /* allocator must not extend beyond fs-block-rounded EOF */
  assert(run==(last-first)*4096);assert(remaining_after(actual,(int)run,requested)==0);
  assert(allocations==(existing?0:(int)(last-first)));
 }else{assert(!allocations&&phys==-1&&run==(size_t)requested);}
}
int main(int argc,char **argv){
 (void)argv;if(argc>1){check(52208,0,49152,false,VNODE_WRITE);return 0;}
 for(uint64_t eof=1;eof<=65536;eof+=137){uint64_t off=(eof-1)&~(uint64_t)PAGE_MASK;check(eof,0,off,false,VNODE_WRITE);check(eof,0,off,true,VNODE_WRITE);check(0,eof,off,false,VNODE_WRITE);check(eof,0,off,false,VNODE_READ);}
 check(52208,0,49152,false,VNODE_WRITE);check(49152,52208,49152,false,VNODE_WRITE);check(65536,0,49152,false,VNODE_WRITE);
 struct ext4mount m={4096,512};struct ext4node e={.e_mount=&m,.e_size=52208};struct vnode v={.node=&e};size_t run=123;daddr64_t phys=456;struct vnop_blockmap_args a={.a_vp=&v,.a_bpn=&phys,.a_foffset=53248,.a_size=16384,.a_run=&run,.a_flags=VNODE_WRITE};memset(blocks,0,sizeof blocks);allocations=0;assert(ext4_vnop_blockmap_impl(&a)==EINVAL&&!allocations);
 e.e_freed=true;assert(ext4_vnop_blockmap_impl(&a)==ENOENT&&!allocations);
 puts("PATCHED actual blockmap + XNU round/tail-stop: EOF52208, current/growing EOF, existing allocation, read holes and guards PASS");return 0;
}
