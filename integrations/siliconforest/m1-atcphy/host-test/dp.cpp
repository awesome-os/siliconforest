// SPDX-License-Identifier: MIT
#include "sf_atc_dp.h"
#include <vector>
#include <array>
#include <tuple>
#include <cassert>
#include <cstdio>
#include <cerrno>
struct Model {
    std::vector<uint32_t> core=std::vector<uint32_t>(0x4c000/4);
    std::vector<uint32_t> aux=std::vector<uint32_t>(0x8000/4);
    std::vector<std::tuple<unsigned,unsigned,unsigned>> writes;
    unsigned fail=0, delays=0;
    static uint32_t read(void *ctx,sf_atc_window w,uint32_t off) {
        auto &m=*(Model*)ctx;
        assert(w==SF_ATC_CORE||w==SF_ATC_LPDPTX);
        auto &bank=w==SF_ATC_CORE?m.core:m.aux;
        assert(!(off&3) && off/4<bank.size());
        if(w==SF_ATC_CORE) {
            if(off==0xa74)return m.fail==1?0:1; // command-ready
            if(off==0x7044)return m.fail==3?0:8; // PLL lock
            if(off==0x2000)return bank[off/4]|((m.fail!=2 && (bank[off/4]&1))?2:0);
        }
        return bank[off/4];
    }
    static void write(void *ctx,sf_atc_window w,uint32_t off,uint32_t value) {
        auto &m=*(Model*)ctx; assert(w==SF_ATC_CORE||w==SF_ATC_LPDPTX);
        auto &bank=w==SF_ATC_CORE?m.core:m.aux;
        assert(!(off&3) && off/4<bank.size());
        bank[off/4]=value; m.writes.emplace_back(w,off,value);
    }
    static void delay(void *ctx,unsigned us) { assert(us==10||us==2);++((Model*)ctx)->delays; }
    sf_atc_io io() { return {this,read,write,delay,nullptr}; }
};
int main() {
    for(unsigned mask=1;mask<=3;++mask) for(unsigned rate:{1620,2700,5400,8100}) {
        Model m; auto io=m.io(); sf_atc_dp dp={&io,mask,-1,false,false,false,false};
        assert(!sf_atc_dp_aux_start(&dp));
        assert(!(m.aux[0x4000/4]&0x14)); // AUX unclamped/powered
        assert(!sf_atc_dp_set_rate(&dp,rate) && dp.rate==(int)rate);
        unsigned writes=m.writes.size();
        assert(!sf_atc_dp_set_rate(&dp,0) && dp.rate==(int)rate && m.writes.size()==writes);
        assert(!sf_atc_dp_set_rate(&dp,rate) && m.writes.size()==writes); // no retune
        bool lane0=false,lane1=false;
        for(auto event:m.writes) {
            auto off=std::get<1>(event);
            if(std::get<0>(event)!=SF_ATC_CORE)continue;
            if(off>=0x9000 && off<0xe000)lane0=true;
            if(off>=0x10000 && off<0x15000)lane1=true;
        }
        assert(lane0==!!(mask&1) && lane1==!!(mask&2));
        assert(!sf_atc_dp_aux_stop(&dp) && dp.rate==-1);
        assert((m.aux[0x4000/4]&0x14)==0x14);
    }
    for(unsigned fail=1;fail<=3;++fail) {
        Model m;auto io=m.io();sf_atc_dp dp={&io,3,1620,false,false,false,false};m.fail=fail;
        assert(sf_atc_dp_set_rate(&dp,2700)==-ETIMEDOUT);
        assert(dp.rate==(fail==1?1620:-1)); // only CMD_READY failure precedes programming
        assert(!(m.core[0x2000/4]&1)); // request cleared after failed command
    }
    for(unsigned field=0;field<4;++field) {
        Model m;auto io=m.io();sf_atc_dp dp={&io,3,-1,false,false,false,false};
        if(field==0)dp.mode_dirty=true;
        if(field==1)dp.pipe_pending=true;
        if(field==2)dp.tunnel_clock_on=true;
        if(field==3)dp.tunnel_saved=true;
        assert(sf_atc_dp_set_rate(&dp,5400)==-EBUSY && m.writes.empty());
        assert(sf_atc_dp_aux_start(&dp)==-EBUSY && m.writes.empty());
    }
    for (uint8_t rate : {0x06,0x0a,0x14,0x1e}) {
        Model m;auto io=m.io();sf_atc_tunnel t={&io,true,false,false,false,0};
        assert(!sf_atc_tunnel_set_rate(&t,rate) && t.clock_on && t.rate==rate);
        unsigned writes=m.writes.size();
        assert(!sf_atc_tunnel_set_rate(&t,rate) && m.writes.size()==writes);
        assert(!sf_atc_tunnel_set_rate(&t,0) && !t.clock_on && !t.rate);
    }
    for(unsigned fail : {1,2,3}) {
        Model m;auto io=m.io();sf_atc_tunnel t={&io,true,false,false,false,0};m.fail=fail;
        assert(sf_atc_tunnel_set_rate(&t,0x14)==-ETIMEDOUT);
        assert(t.clock_on==(fail==2)); // failed APB stop retains ownership
        if(t.clock_on) {
            assert(sf_atc_tunnel_set_rate(&t,0x14)==-EBUSY);
            m.fail=0;
            assert(!sf_atc_tunnel_set_rate(&t,0) && !t.clock_on);
        }
    }
    Model m;auto io=m.io();sf_atc_dp dp={&io,3,-1,false,false,false,false};
    for(unsigned rate:{1,1619,1621,8101})assert(sf_atc_dp_set_rate(&dp,rate)==-EINVAL);
    assert(m.writes.empty());
    puts("T8103 DP AUX + four rates/all PMA lane masks, repeated-rate, shared-PLL refusal, TB pixel clocks/failed-stop retention and command/lock timeout cases passed");
}
