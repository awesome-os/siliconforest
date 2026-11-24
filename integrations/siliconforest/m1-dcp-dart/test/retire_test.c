// SPDX-License-Identifier: GPL-2.0-only OR MIT
#include "sf_dart_retire.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct model { uint64_t root[SF_DART_PTES],leaf[SF_DART_PTES];unsigned flushes;int fail; };
static uint64_t *table(void *ctx,uint64_t pa)
{ return pa==0x400000000ULL?((struct model*)ctx)->leaf:NULL; }
static uint64_t alloc(void *ctx)
{ (void)ctx;return 0x400000000ULL; }
static int flush(void *ctx)
{ struct model *m=ctx;++m->flushes;return m->fail; }
int main(void)
{
    for(unsigned failure=0;failure<2;++failure)for(unsigned pages=1;pages<=256;++pages) {
        struct model *m=calloc(1,sizeof(*m));assert(m);
        struct sf_dart_tables t={.l1=m->root,.ops={m,table,alloc}};
        bool poisoned=false;uint64_t done=0xfeed;
        uint64_t bytes=(uint64_t)pages*SF_DART_PAGE;
        assert(!sf_dart_table_map(&t,0x10000000,0x500000000ULL,bytes,3));
        assert(!sf_dart_table_map(&t,0x11000000,0x510000000ULL,SF_DART_PAGE,3));
        m->fail=failure?-ETIMEDOUT:0;
        int ret=sf_dart_retire(&t,&poisoned,0x10000000,bytes,m,flush,&done);
        assert(ret==(failure?-ETIMEDOUT:0) && poisoned==!!failure && m->flushes==1);
        assert(done==(failure?0xfeed:bytes));
        struct sf_dart_walk w;assert(!sf_dart_table_walk(&t,0x10000000,&w) && !w.pte);
        // Cleared PTEs after timeout are not a hardware quiescence fact.
        if(failure) {
            assert(sf_dart_retire(&t,&poisoned,0x11000000,SF_DART_PAGE,m,flush,&done)==-EIO);
            assert(m->flushes==1 && done==0xfeed);
            assert(!sf_dart_table_walk(&t,0x11000000,&w) && w.phys==0x510000000ULL);
        }
        free(m);
    }
    struct model *m=calloc(1,sizeof(*m));assert(m);
    struct sf_dart_tables t={.l1=m->root,.ops={m,table,alloc}};bool poisoned=false;uint64_t done=99;
    assert(sf_dart_retire(&t,&poisoned,3,SF_DART_PAGE,m,flush,&done)==-EINVAL && !poisoned && done==99 && !m->flushes);
    assert(sf_dart_retire(&t,&poisoned,0xffffc000,0x8000,m,flush,&done)==-EINVAL);
    m->root[8]=0x600000001ULL; // a valid but unreachable foreign table
    assert(sf_dart_retire(&t,&poisoned,0x10000000,SF_DART_PAGE,m,flush,&done)<0);
    assert(poisoned && !m->flushes && done==99 && m->root[8]==0x600000001ULL);
    free(m);
    puts("DART retirement: 512 real table clear/flush cases, failed flush retains output/poison ownership, later range protected, invalid/unreachable walks refuse before writes");
}
