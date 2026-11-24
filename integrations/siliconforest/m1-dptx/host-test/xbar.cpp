// SPDX-License-Identifier: MIT
#include "sf_dpxbar.h"
#include <cassert>
#include <vector>
#include <array>
#include <cstdio>
#include <cerrno>
struct Model {
    std::array<uint32_t,0x1000/4> r={};
    struct Write{unsigned off,value;};std::vector<Write> writes;std::vector<unsigned> delays;
    bool stuck=false;unsigned warnings=0;sf_dpxbar x={};
    static uint32_t read(void *ctx,uint32_t off) {
        auto &m=*(Model*)ctx;assert(!(off&3)&&off<0x1000);
        if(off==0x804||off==0x828||off==0x848)return m.stuck?~0u:m.r[(off-0x800)/4];
        return m.r[off/4];
    }
    static void write(void *ctx,uint32_t off,uint32_t value){auto &m=*(Model*)ctx;assert(!(off&3)&&off<0x1000);m.r[off/4]=value;m.writes.push_back({off,value});}
    static void delay(void *ctx,unsigned us){assert(us==1||us==10);((Model*)ctx)->delays.push_back(us);}
    static void warn(void *ctx,uint32_t wr,uint32_t rd,uint32_t out){assert(wr||rd||out);++((Model*)ctx)->warnings;}
    Model(){for(auto &v:r)v=0xa5000000;sf_dpxbar_io io={this,read,write,delay,warn};assert(!sf_dpxbar_init(&x,&io));assert(r[0xc00/4]==0);assert(sf_dpxbar_init(&x,&io)==-EALREADY);writes.clear();}
};
static unsigned atc(unsigned output){return output==0?256:output==1?1:16;}
static unsigned mask(unsigned output){return output==0?0xf00f00:output==1?0xf00f:0xf00f0;}
static unsigned route(unsigned output,unsigned input){return output==0?(input<<20)|(input<<8):output==1?(input<<12)|input:(input<<16)|(input<<4);}
int main(){unsigned cases=0;
    for(unsigned output=0;output<3;++output)for(unsigned input=0;input<2;++input)for(unsigned cycle=0;cycle<32;++cycle) {
        Model m;unsigned old=m.r[0x60/4],bit=1u<<input,a=atc(output);auto o=(sf_dpxbar_output)output;
        assert(!sf_dpxbar_select(&m.x,o,input));
        assert(m.x.selected_dispext[output]==(int)input && m.r[0x60/4]==((old&~mask(output))|route(output,input)));
        for(unsigned off:{0u,0x20u,0x50u})assert(m.r[off/4]&bit);
        for(unsigned off:{0x40u,0x70u})assert(m.r[off/4]&a);
        assert(!(m.r[4/4]&bit) && !(m.r[0x28/4]&bit) && !(m.r[0x48/4]&a));
        std::vector<unsigned> rdclk;for(auto w:m.writes)if(w.off==0x20)rdclk.push_back(w.value);
        assert(rdclk.size()>=3 && !(rdclk[rdclk.size()-2]&bit) && (rdclk.back()&bit));
        assert(m.delays.size()==2 && m.delays[0]==1 && m.delays[1]==10 && !m.warnings);
        assert(!sf_dpxbar_link(&m.x,o,false) && m.x.selected_dispext[output]==(int)input);
        assert(!(m.r[0]&bit) && !(m.r[0x20/4]&bit) && !(m.r[0x40/4]&a));
        assert((m.r[4/4]&bit) && (m.r[0x28/4]&bit) && (m.r[0x48/4]&a));
        assert(!sf_dpxbar_link(&m.x,o,true) && (m.r[0]&bit) && (m.r[0x40/4]&a));
        assert(!sf_dpxbar_select(&m.x,o,SF_DPXBAR_DISCONNECT) && m.x.selected_dispext[output]==-1);
        assert(!(m.r[0x50/4]&bit) && !(m.r[0x70/4]&a));
        ++cases;
    }
    {
        Model m;assert(!sf_dpxbar_select(&m.x,SF_DPXBAR_PHY,0));auto n=m.writes.size();
        assert(sf_dpxbar_select(&m.x,SF_DPXBAR_DPIN0,0)==-EBUSY && m.writes.size()==n);
        assert(!sf_dpxbar_select(&m.x,SF_DPXBAR_DPIN0,1));n=m.writes.size();
        assert(sf_dpxbar_select(&m.x,SF_DPXBAR_PHY,1)==-EBUSY && m.writes.size()==n && m.x.selected_dispext[0]==0);
        assert(!sf_dpxbar_select(&m.x,SF_DPXBAR_PHY,SF_DPXBAR_DISCONNECT));
        assert((m.r[0]&2) && (m.r[0x50/4]&2) && (m.r[0x70/4]&1) && m.x.selected_dispext[1]==1);
    }
    for(auto o:{SF_DPXBAR_DPIN0,SF_DPXBAR_DPIN1}) {
        Model m;assert(!sf_dpxbar_preselect(&m.x,o,1) && m.writes.size()==1 && m.writes[0].off==0x60 && m.x.selected_dispext[o]==-1);
        auto n=m.writes.size();assert(sf_dpxbar_link(&m.x,o,true)==-ENODEV && m.writes.size()==n);
        assert(!sf_dpxbar_select(&m.x,o,1));n=m.writes.size();assert(sf_dpxbar_preselect(&m.x,o,0)==-EBUSY && m.writes.size()==n);
    }
    {
        Model m;assert(sf_dpxbar_preselect(&m.x,SF_DPXBAR_PHY,0)==-EOPNOTSUPP && m.writes.empty());
        assert(sf_dpxbar_select(&m.x,SF_DPXBAR_PHY,2)==-EINVAL && m.writes.empty());
        assert(sf_dpxbar_select(&m.x,(sf_dpxbar_output)-1,0)==-EINVAL && m.writes.empty());
        m.stuck=true;assert(!sf_dpxbar_select(&m.x,SF_DPXBAR_PHY,0) && m.warnings==1); // upstream diagnostic, not a readiness ACK
    }
    printf("T8103 crossbar: %u PHY/DP-IN select/link/disconnect cycles, duplicate-source refusal, preselection, shared-field preservation, gate ordering and warning-only status pass\n",cases);
}
