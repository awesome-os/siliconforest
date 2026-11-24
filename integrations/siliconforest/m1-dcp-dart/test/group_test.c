// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Production group/table algorithms over explicit host-only stream models. */
#include "sf_dart_group.h"
#include "sf_dart_retire.h"
#include "sf_dart_pair.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct sf_dart_stream {
    uint64_t root[SF_DART_PTES],leaf[SF_DART_PTES];
    struct sf_dart_tables tables;
    bool poisoned;
    unsigned flushes;
    int flush_error,map_error;
};
static struct sf_dart_stream *streams;
static int poisoned(void){return streams[0].poisoned || streams[1].poisoned;}
static uint64_t *table(void *ctx,uint64_t pa){return pa==0x400000000ULL?((struct sf_dart_stream*)ctx)->leaf:NULL;}
static uint64_t alloc_table(void *ctx){(void)ctx;return 0x400000000ULL;}
static void init(void) {
    memset(streams,0,2*sizeof(*streams));
    for(unsigned i=0;i<2;++i)streams[i].tables=(struct sf_dart_tables){.l1=streams[i].root,.ops={&streams[i],table,alloc_table}};
}
int sf_dart_map_deferred(struct sf_dart_stream *s,uint64_t iova,uint64_t phys,uint64_t bytes,unsigned prot)
{ if(poisoned())return -EIO;if(s->map_error)return s->map_error;return sf_dart_table_map(&s->tables,iova,phys,bytes,prot); }
int sf_dart_flush(struct sf_dart_stream *s)
{ if(poisoned())return -EIO;++s->flushes;if(s->flush_error)s->poisoned=true;return s->flush_error; }
static int flush(void *ctx){return sf_dart_flush(ctx);}
int sf_dart_unmap_checked(struct sf_dart_stream *s,uint64_t iova,uint64_t bytes,uint64_t *done)
{ if(poisoned())return -EIO;return sf_dart_retire(&s->tables,&s->poisoned,iova,bytes,s,flush,done); }
static uint64_t phys(unsigned i,uint64_t addr){struct sf_dart_walk w;assert(!sf_dart_table_walk(&streams[i].tables,addr,&w));return w.phys;}
int main(void) {
    unsigned cases=0;
    for(unsigned av=0;av<256;++av)for(unsigned bv=0;bv<256;++bv)for(unsigned wanted=1;wanted<=8;++wanted) {
        uint8_t a=av,b=bv;uint64_t first=99;int expected=-1;
        for(unsigned start=0;start+wanted<=8;++start)
            if(!((av|bv)&(((1u<<wanted)-1)<<start))){expected=start;break;}
        int ret=sf_dart_pair_find(&a,&b,8,wanted,&first);
        assert(ret==(expected<0?-ENOSPC:0));assert(first==(expected<0?99:(unsigned)expected));++cases;
    }
    for(unsigned av=0;av<256;++av)for(unsigned wanted=1;wanted<=8;++wanted) {
        uint8_t a=av;uint64_t first=99;int expected=-1;
        for(unsigned start=0;start+wanted<=8;++start)
            if(!(av&(((1u<<wanted)-1)<<start))){expected=start;break;}
        int ret=sf_dart_pair_find(&a,NULL,8,wanted,&first);
        assert(ret==(expected<0?-ENOSPC:0) && first==(expected<0?99:(unsigned)expected));
    }
    streams=calloc(2,sizeof(*streams));assert(streams);struct sf_dart_stream *a=streams,*b=streams+1;
    const uint64_t addr=0x10000000,pa=0x500000000ULL,foreign=0x600000000ULL;uint64_t done;
    init();assert(!sf_dart_group_map_deferred(a,b,addr,pa,2*SF_DART_PAGE,3));
    assert(phys(0,addr)==pa && phys(1,addr)==pa && !a->flushes && !b->flushes);
    assert(!sf_dart_group_flush(a,b) && a->flushes==1 && b->flushes==1);
    assert(!sf_dart_group_unmap_checked(a,b,addr,2*SF_DART_PAGE,&done) && done==2*SF_DART_PAGE);
    // A peer failure after partial writes rolls back its own additions and
    // preserves the foreign PTE. Only the successful first map is undone.
    init();assert(!sf_dart_map_deferred(b,addr+SF_DART_PAGE,foreign,SF_DART_PAGE,3));
    assert(sf_dart_group_map_deferred(a,b,addr,pa,2*SF_DART_PAGE,3)==-EEXIST);
    assert(!phys(0,addr) && !phys(0,addr+SF_DART_PAGE) && !phys(1,addr) && phys(1,addr+SF_DART_PAGE)==foreign);
    assert(a->flushes==1 && b->flushes==1 && !poisoned());
    for(unsigned stage=0;stage<2;++stage) {
        init();streams[stage].map_error=-ENOMEM;
        assert(sf_dart_group_map_deferred(a,b,addr,pa,SF_DART_PAGE,3)==-ENOMEM);
        assert(!phys(0,addr) && !phys(1,addr));
        assert(a->flushes==1 && b->flushes==(stage?1u:0u));
        init();assert(!sf_dart_group_map_deferred(a,b,addr,pa,SF_DART_PAGE,3));
        streams[stage].flush_error=-ETIMEDOUT;
        assert(sf_dart_group_flush(a,b)==-ETIMEDOUT && poisoned());
        done=99;assert(sf_dart_group_unmap_checked(a,b,addr,SF_DART_PAGE,&done)==-EIO && done==99);
        assert(phys(0,addr)==pa && phys(1,addr)==pa); // backing still owned, never reclaim on error
        init();assert(!sf_dart_group_map_deferred(a,b,addr,pa,SF_DART_PAGE,3));
        assert(!sf_dart_group_flush(a,b));streams[stage].flush_error=-ETIMEDOUT;
        done=99;assert(sf_dart_group_unmap_checked(a,b,addr,SF_DART_PAGE,&done)==-ETIMEDOUT && done==99 && poisoned());
    }
    // Preserve the original single-stream operation as well.
    init();assert(!sf_dart_group_map_deferred(a,NULL,addr,pa,SF_DART_PAGE,3));
    assert(!sf_dart_group_flush(a,NULL));assert(!sf_dart_group_unmap_checked(a,NULL,addr,SF_DART_PAGE,&done) && done==SF_DART_PAGE);
    free(streams);
    printf("DART mirrored domain: %u common-IOVA bitmap cases, actual table pairs, foreign-PTE-preserving rollback and both-stream map/flush/retire failures passed\n",cases);
}
