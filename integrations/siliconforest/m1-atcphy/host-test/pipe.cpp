// SPDX-License-Identifier: MIT
#include "sf_atc_pipe.h"
#include <cassert>
#include <vector>
#include <cstdio>
#include <cerrno>
struct Model {
    unsigned r[9]={0xa5,0x96,0,0x122,0,0,0,0,0};
    bool noLock=false,noUnlock=false;unsigned requests=0;
    std::vector<unsigned> muxes;
    static uint32_t read(void *ctx,sf_atc_window w,uint32_t off){auto &m=*(Model*)ctx;
        assert(w==SF_ATC_PIPE && off<=0x20 && !(off&3));return m.r[off/4];}
    static void write(void *ctx,sf_atc_window w,uint32_t off,uint32_t value){auto &m=*(Model*)ctx;
        assert(w==SF_ATC_PIPE && off<=0x20 && !(off&3));m.r[off/4]=value;
        if(off==0xc)m.muxes.push_back(value);
        if(off==0x10){if(value&1){++m.requests;if(!m.noLock)m.r[5]|=1;}else if(!m.noUnlock)m.r[5]&=~1u;}
    }
    static void delay(void *,unsigned us){assert(us==10||us==5000);}
    sf_atc_io io(){return {this,read,write,delay,nullptr};}
    void checkmux(unsigned final){assert(muxes.size()==3);assert((muxes[0]&0x38)==0 && (muxes[1]&0x38)==0);assert((muxes[2]&0x3f)==final);for(auto v:muxes)assert((v&~0x3fu)==0x100);}
};
int main(){
    for(unsigned i=0;i<100;++i) {
        Model m;auto io=m.io();sf_atc_pipe_state s={SF_ATC_DUMMY,false};
        assert(!sf_atc_pipe_route(&io,&s,SF_ATC_USB3_BACKEND));m.checkmux(8);assert(!m.requests && !(m.r[0]&5));
        assert(!sf_atc_pipe_route(&io,&s,SF_ATC_USB3_BACKEND) && m.muxes.size()==3);
        m.muxes.clear();assert(!sf_atc_pipe_park(&io,&s,false));m.checkmux(0x22);
        assert(s.backend==SF_ATC_DUMMY && !s.dummy_pending && (m.r[8]&0x100f)==0x1002);
    }
    {
        Model m;auto io=m.io();sf_atc_pipe_state s={SF_ATC_USB3_BACKEND,false};
        m.r[3]=0x108;auto overrides=m.r[0],values=m.r[1];m.noLock=true;
        assert(sf_atc_pipe_park(&io,&s,false)==ETIMEDOUT && m.requests==10);
        assert(s.backend==SF_ATC_USB3_BACKEND && !s.dummy_pending && m.muxes.empty());
        assert(m.r[0]==overrides && m.r[1]==values && m.r[8]==0);
        // Only actual controller halt/reset permits the clockless fallback.
        assert(!sf_atc_pipe_park(&io,&s,true));m.checkmux(0x22);
    }
    {
        Model m;auto io=m.io();sf_atc_pipe_state s={SF_ATC_USB3_BACKEND,false};
        m.r[3]=0x108;m.noUnlock=true;
        assert(sf_atc_pipe_park(&io,&s,false)==ETIMEDOUT && s.dummy_pending && s.backend==SF_ATC_DUMMY);
        assert(m.r[8]==0 && sf_atc_pipe_route(&io,&s,SF_ATC_USB3_BACKEND)==EBUSY);
        unsigned requests=m.requests;auto muxes=m.muxes;
        assert(sf_atc_pipe_park(&io,&s,true)==ETIMEDOUT && m.requests==requests && m.muxes==muxes);
        m.noUnlock=false;assert(!sf_atc_pipe_park(&io,&s,true) && !s.dummy_pending && m.requests==requests && m.muxes==muxes);
    }
    {
        Model m;auto io=m.io();sf_atc_pipe_state s={SF_ATC_DUMMY,false};m.noLock=true;
        assert(!sf_atc_pipe_route(&io,&s,SF_ATC_USB4_BACKEND));m.checkmux(0x11);assert(!(m.r[0]&5));
        assert(sf_atc_pipe_park(&io,&s,false)==EBUSY);assert(!sf_atc_pipe_park(&io,&s,true));
    }
    {
        Model m;auto io=m.io();sf_atc_pipe_state s={SF_ATC_DUMMY,false};m.r[5]=1;m.noUnlock=true;
        assert(sf_atc_pipe_route(&io,&s,SF_ATC_USB3_BACKEND)==ETIMEDOUT && m.muxes.empty());
        m.r[5]=0;assert(sf_atc_pipe_route(&io,&s,SF_ATC_DUMMY)==EINVAL);
        s.backend=(sf_atc_pipe)-1;assert(sf_atc_pipe_park(&io,&s,true)==EINVAL);
    }
    puts("T8103 PIPE: 100 USB3 cycles, live ACK retries/restoration, clockless stopped park, retained failed completion, USB4 advisory lock and stuck ACK cases pass");
}
