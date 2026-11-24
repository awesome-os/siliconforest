// SPDX-License-Identifier: MIT
#include "sf_dcpext_plan.h"
#include <map>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cerrno>
using Bytes=std::vector<uint8_t>;
using Key=std::pair<std::string,std::string>;
struct Tree {
    std::map<Key,Bytes> props;
    static const void *get(void *c,const char *path,const char *prop,size_t *size) {
        auto &m=((Tree*)c)->props;auto i=m.find({path,prop});if(i==m.end())return nullptr;
        *size=i->second.size();return i->second.data();
    }
    void reg(const char *node,uint64_t base,uint64_t span,const char *prop="reg",bool arm=true) {
        auto &v=props[{node,prop}];if(arm)base-=0x200000000ULL;
        for(uint64_t x:{base,span})for(unsigned i=0;i<8;++i)v.push_back(x>>(8*i));
    }
    sf_dcpext_adt ops(){return {this,get};}
    Tree() {
        props[{"/","compatible"}]={'J','2','9','3','A','P',0};
        reg("/arm-io/dcpext",0x271c00000,0x6c000);
        reg("/arm-io/dispext0",0x270000000,0x118000);
        reg("/arm-io/dispext0",0x271320000,0x4000);
        reg("/arm-io/dispext0",0x271344000,0x4000);
        reg("/arm-io/dispext0",0x271800000,0x800000);
        reg("/arm-io/dispext0",0x23b3d0000,0x4000);
        reg("/arm-io/dispext0",0x23b738000,0x1000);
        reg("/arm-io/dispext0",0x23bc3c000,0x1000);
        reg("/arm-io/dart-dcpext",0x27130c000,0x4000);
        reg("/arm-io/dart-dispext0",0x271304000,0x4000);
        // Synthetic carve-outs: no captured firmware/address fixture.
        reg("/chosen/carveout-memory-map",0x800100000,0x4000,"region-id-73",false);
        reg("/chosen/carveout-memory-map",0x800200000,0x4000,"region-id-74",false);
        auto &v=props[{"/arm-io/dcpext","interrupts"}];
        for(unsigned x:{467,466,469,468})for(unsigned i=0;i<4;++i)v.push_back(x>>(8*i));
    }
};
int main(){
    Tree tree;auto ops=tree.ops();uint32_t ver[]={13,6,1};sf_dcpext_plan plan;
    assert(!sf_dcpext_plan_t8103(&ops,0x8103,ver,&plan));
    assert(plan.firmware_compat[1]==5&&plan.mailbox.phys==0x271c08000&&plan.mailbox_irq[0]==466);
    assert(plan.typec_port==1&&plan.dptx_phy==1&&plan.remote_core==0&&plan.stream_count==1);
    assert(plan.piodma_dart.phys==plan.disp_dart.phys);
    unsigned count=0;
    for(auto pair:tree.props) {
        Tree missing=tree;missing.props.erase(pair.first);auto m=missing.ops();
        memset(&plan,0xa5,sizeof(plan));sf_dcpext_plan unchanged=plan;
        assert(sf_dcpext_plan_t8103(&m,0x8103,ver,&plan)<0 && !memcmp(&plan,&unchanged,sizeof(plan)));++count;
        for(unsigned length=0;length<pair.second.size();++length) {
            Tree shorted=tree;shorted.props[pair.first].resize(length);auto x=shorted.ops();
            assert(sf_dcpext_plan_t8103(&x,0x8103,ver,&plan)<0);++count;
        }
    }
    Tree overflow=tree;auto &v=overflow.props[{"/chosen/carveout-memory-map","region-id-73"}];
    for(unsigned i=0;i<8;++i)v[i]=0xff;auto x=overflow.ops();assert(sf_dcpext_plan_t8103(&x,0x8103,ver,&plan)==-EINVAL);
    ver[0]=14;assert(sf_dcpext_plan_t8103(&ops,0x8103,ver,&plan)==-EOPNOTSUPP);
    ver[0]=13;assert(sf_dcpext_plan_t8103(&ops,0x8112,ver,&plan)==-ENODEV);
    printf("dcpext J293 resource/firmware/port plan: %u absent/truncated properties + overflow/ABI refusals passed\n",count);
}
