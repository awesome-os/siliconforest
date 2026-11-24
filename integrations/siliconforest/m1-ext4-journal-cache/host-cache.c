// SPDX-License-Identifier: MIT
// Literal reader/overlay with an I/O-failing fake device and explicit buffer ownership.
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
typedef int64_t daddr64_t;
#define NOCRED NULL
#define BLK_META 7
struct buf {unsigned char data[4096];int refs,invalid,error,resid;};typedef struct buf *buf_t;
struct tx {uint64_t pblk;char *data;};struct e4jnl {unsigned j_tx_count;struct tx j_tx[2];};
struct ext4mount {void *em_devvp;uint32_t em_blocksize,em_dev_bsize;struct e4jnl *em_jnl;};
static struct buf buffer;static unsigned char disk[4096],newer[4096];static struct e4jnl journal;
static int gets,reads,releases,read_error,get_null,late_overlay,enabled=1;static daddr64_t last_block;
static uint64_t sf_ext4_cache_reads,sf_ext4_cache_hits,sf_ext4_cache_breads;
@COUNT@
static int sf_ext4_cache_enabled(void){return enabled;}
@OVERLAY@
static buf_t buf_getblk(void *vp,daddr64_t blk,int size,int a,int b,int kind){assert(size==4096&&kind==BLK_META);++gets;last_block=blk;if(get_null)return NULL;assert(!buffer.refs++);buffer.error=99;buffer.resid=4096;return &buffer;}
static void buf_brelse(buf_t b){assert(b==&buffer&&b->refs==1);--b->refs;++releases;}
static int buf_meta_bread(void *vp,daddr64_t blk,int size,void *cred,buf_t *out){assert(size==4096);++reads;last_block=blk;assert(!buffer.refs++);memcpy(buffer.data,disk,4096);buffer.invalid=0;buffer.error=read_error;*out=&buffer;if(late_overlay)journal.j_tx_count=1;return read_error;}
static void *buf_dataptr(buf_t b){return b->data;}
static void buf_markinvalid(buf_t b){b->invalid=1;}
static void buf_seterror(buf_t b,int e){b->error=e;}
static void buf_setresid(buf_t b,int n){b->resid=n;}
@READER@
static void reset(void){memset(&buffer,0,sizeof buffer);gets=reads=releases=read_error=get_null=late_overlay=0;sf_ext4_cache_reads=sf_ext4_cache_hits=sf_ext4_cache_breads=0;enabled=1;memset(disk,0x15,sizeof disk);memset(newer,0x71,sizeof newer);journal.j_tx_count=1;journal.j_tx[0]=(struct tx){17,(char*)newer};}
int main(void){
 struct ext4mount m={NULL,4096,4096,&journal};buf_t out=NULL;
 reset();read_error=EIO;assert(!ext4_blkread(&m,17,&out));assert(gets==1&&!reads&&sf_ext4_cache_hits==1&&out->invalid&&out->error==0&&!out->resid&&!memcmp(out->data,newer,4096));buf_brelse(out);assert(!buffer.refs);
 reset();enabled=0;assert(!ext4_blkread(&m,17,&out));assert(!gets&&reads==1&&!out->invalid&&!memcmp(out->data,newer,4096));buf_brelse(out);
 reset();journal.j_tx_count=0;assert(!ext4_blkread(&m,17,&out));assert(gets==1&&reads==1&&releases==1&&!memcmp(out->data,disk,4096));buf_brelse(out);
 reset();m.em_jnl=NULL;assert(!ext4_blkread(&m,17,&out));assert(!gets&&reads==1&&!memcmp(out->data,disk,4096));buf_brelse(out);m.em_jnl=&journal;
 reset();journal.j_tx_count=0;read_error=EIO;out=(buf_t)1;assert(ext4_blkread(&m,17,&out)==EIO&&out==NULL&&!buffer.refs&&releases==2);
 reset();get_null=1;assert(!ext4_blkread(&m,17,&out));assert(gets==1&&reads==1&&!memcmp(out->data,newer,4096));buf_brelse(out);
 reset();journal.j_tx_count=0;late_overlay=1;assert(!ext4_blkread(&m,17,&out));assert(reads==1&&!memcmp(out->data,newer,4096));buf_brelse(out);
 for(unsigned sector=512;sector<=4096;sector*=2){reset();m.em_dev_bsize=sector;assert(!ext4_blkread(&m,17,&out));assert(last_block==(daddr64_t)(17*4096/sector));buf_brelse(out);}
 reset();journal.j_tx[0].pblk=18;assert(!ext4_blkread(&m,17,&out));assert(reads==1&&!memcmp(out->data,disk,4096));buf_brelse(out);
 puts("PASS literal ext4 reader+WAL overlay: cache hit avoids failing physical read, exact data/sector units, invalid clean buffer, disabled/no-journal/miss/late-overlay/error paths and balanced buffer references. Fake provider only.");
}
