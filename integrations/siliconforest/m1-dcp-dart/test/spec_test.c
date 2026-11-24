// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Exercise the production aperture parser against the actual display-tree
 * builder, with invented ADT values; no device capture/firmware fixture. */
#include "sf_dart_spec.h"
#include "dispdt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint64_t coproc[]={0x31c00000,0x6c000};
static const uint64_t display[]={0x30000000,0x3e8000,0x31320000,0x4000,0x31344000,0x4000,0x31800000,0x800000,0x3b3d0000,0x4000};
static const uint64_t regions[][2]={{0x800000000ULL,0x4000},{0x800004000ULL,0x4000},{0x800008000ULL,0x4000},{0x80000c000ULL,0x4000},{0x800010000ULL,0x4000}};
static const void *get(void *ctx,const char *path,const char *name,uint32_t *len) {
    (void)ctx;
    if(!strcmp(path,"/")&&!strcmp(name,"compatible")){*len=7;return "J293AP";}
    if(!strcmp(path,"/arm-io/dcp")&&!strcmp(name,"reg")){*len=sizeof(coproc);return coproc;}
    if(!strcmp(path,"/arm-io/disp0")&&!strcmp(name,"reg")){*len=sizeof(display);return display;}
    if(!strcmp(path,"/vram")&&!strcmp(name,"reg")){*len=16;return regions[4];}
    if(!strcmp(path,"/chosen/carveout-memory-map")) {
        const char *names[]={"region-id-50","region-id-57","region-id-94","region-id-95"};
        for(unsigned i=0;i<4;++i)if(!strcmp(name,names[i])){*len=16;return regions[i];}
    }
    return NULL;
}
static uint32_t be(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static struct dispdt_tree tree;
int main(void) {
    struct gpudt_adt_ops adt={NULL,get};struct dispdt_config cfg={.chip_id=GPUDT_CHIP_T8103,.phandle_base=DISPDT_DARWIN_PHANDLE_BASE,.fw_version={13,6,1},.fw_version_len=3};
    assert(!dispdt_build_t8103(&adt,&cfg,&tree));
    unsigned checked=0;
    for(unsigned node=0;node<DISPDT_NODE_COUNT;++node) {
        const struct dispdt_prop *p=dispdt_find(&tree,node,"iommus");if(!p)continue;
        assert(p->len==24);uint32_t args[5],sid=99;
        for(unsigned i=0;i<5;++i)args[i]=be(p->value+4+4*i);
        assert(!sf_dart_spec(args,5,NULL,&sid) && sid==args[0]);++checked;
    }
    assert(checked==3);
    uint32_t args[6]={1,0,0,0,0xfc000000},sid=99;
    assert(!sf_dart_spec(args,1,NULL,&sid) && sid==1);
    for(unsigned count=0;count<=6;++count)if(count!=1&&count!=5)assert(sf_dart_spec(args,count,NULL,&sid)==-EINVAL);
    sid=99;args[0]=16;assert(sf_dart_spec(args,5,NULL,&sid)==-EINVAL && sid==99);args[0]=1;
    uint64_t full[]={0,1ULL<<32},narrow[]={0,0x80000000};
    assert(!sf_dart_spec(args,5,full,&sid));
    assert(sf_dart_spec(args,5,narrow,&sid)==-EOPNOTSUPP);
    args[4]=0;assert(sf_dart_spec(args,5,full,&sid)==-EINVAL); // malformed spec is not hidden by override
    args[4]=0xfc000001;assert(sf_dart_spec(args,5,NULL,&sid)==-EINVAL);
    args[4]=0xfc000000;args[1]=1;assert(sf_dart_spec(args,5,NULL,&sid)==-EINVAL);
    printf("DART spec: actual display builder's %u five-cell bindings, one-cell SID, malformed/unaligned/overflow/narrow windows and override validation pass\n",checked);
}
