// SPDX-License-Identifier: MIT
#include "sf_atc_modes.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cerrno>
struct Model {
    std::vector<uint32_t> core=std::vector<uint32_t>(0x4c000/4),aux=std::vector<uint32_t>(0x8000/4);
    uint32_t usb[8]={};unsigned writes=0,validated=0,applied=0,fail=0;bool ss=true;
    static uint32_t read(void *ctx,sf_atc_window w,uint32_t off){auto &m=*(Model*)ctx;
        if(w==SF_ATC_USB2){assert(off<0x20);return m.usb[off/4];}
        auto &bank=w==SF_ATC_CORE?m.core:m.aux;assert(!(off&3)&&off/4<bank.size());
        if(w==SF_ATC_CORE&&off==0x20004)return m.core[0x20000/4]&3;
        return bank[off/4];
    }
    static void write(void *ctx,sf_atc_window w,uint32_t off,uint32_t value){auto &m=*(Model*)ctx;
        if(m.ss)assert(m.validated==1);
        if(w==SF_ATC_CORE && (off==0x48 || (off==0x20000&&(value&16))))assert(m.applied==1||!m.ss);
        if(w==SF_ATC_USB2){assert(off<0x20);m.usb[off/4]=value;}
        else{auto &bank=w==SF_ATC_CORE?m.core:m.aux;assert(off/4<bank.size());bank[off/4]=value;}
        ++m.writes;
    }
    static void delay(void *,unsigned us){assert(us==10||us==100);}
    static int validate(void *ctx,const sf_atc_program *p){auto &m=*(Model*)ctx;assert(!m.writes);++m.validated;assert(p->dp_lanes||p->usb3_lanes||p->usb4_lanes);return m.fail==1?EINVAL:0;}
    static int apply(void *ctx,const sf_atc_program *){auto &m=*(Model*)ctx;assert(m.validated==1&&m.writes);assert(!(m.core[0x20000/4]&16));++m.applied;return m.fail==2?EIO:0;}
    sf_atc_io io(){return {this,read,write,delay,nullptr};}
    sf_atc_calibration cal(){return {this,validate,apply};}
};
int main(){
    // Independent expected Linux table: mode, normal/reverse lanes/crossbar,
    // DP mask, lane swap, AUX and PIPE. TBT and USB4 intentionally differ.
    struct Expect{sf_atc_mode mode;unsigned lane[2],cross[2],dp[2];bool swap[2],aux;sf_atc_pipe pipe;};
    const Expect cases[]={
        {SF_ATC_MODE_USB2,{0x6db,0x6db},{0xa,0xb},{0,0},{false,false},false,SF_ATC_DUMMY},
        {SF_ATC_MODE_USB3,{0x489,0x252},{0x110,0x111},{2,1},{false,true},false,SF_ATC_USB3_BACKEND},
        {SF_ATC_MODE_USB3_DP,{0x489,0x252},{0x110,0x111},{2,1},{false,true},true,SF_ATC_USB3_BACKEND},
        {SF_ATC_MODE_TBT,{0,0},{0,1},{0,0},{false,false},false,SF_ATC_DUMMY},
        {SF_ATC_MODE_USB4,{0,0},{0,1},{0,0},{false,false},false,SF_ATC_USB4_BACKEND},
        {SF_ATC_MODE_DP,{0x492,0x492},{0x22014,0x114},{3,3},{false,false},true,SF_ATC_DUMMY}
    };
    for(const auto &c:cases)for(unsigned reverse=0;reverse<2;++reverse){
        sf_atc_program p={};assert(!sf_atc_mode_plan(c.mode,reverse,&p));
        assert(p.lanes==c.lane[reverse]&&p.crossbar==c.cross[reverse]&&p.dp_lanes==c.dp[reverse]);
        assert(p.swap==c.swap[reverse]&&p.aux==c.aux&&p.pipe==c.pipe);
        Model m;m.ss=c.mode!=SF_ATC_MODE_USB2;auto io=m.io();auto cal=m.cal();
        assert(!sf_atc_mode_start(&io,c.mode,reverse,m.ss?&cal:nullptr));
        assert(m.core[0x48/4]==c.lane[reverse]&&m.core[0x4c/4]==c.cross[reverse]);
        assert(!!(m.core[0x20008/4]&4)==c.swap[reverse]);
        assert(m.core[0x20000/4]&16);
        assert(!!(m.core[0x9010/4]&1)==!!(c.dp[reverse]&1));
        assert(!!(m.core[0x10010/4]&1)==!!(c.dp[reverse]&2));
        if(m.ss)assert(m.validated==1&&m.applied==1);
    }
    for(unsigned failure:{1,2}){
        Model m;m.fail=failure;auto io=m.io();auto cal=m.cal();
        assert(sf_atc_mode_start(&io,SF_ATC_MODE_TBT,false,&cal)==(failure==1?EINVAL:EIO));
        assert(!(m.core[0x20000/4]&16));if(failure==1)assert(!m.writes);
    }
    Model m;auto io=m.io();assert(sf_atc_mode_start(&io,SF_ATC_MODE_TBT,false,nullptr)==EINVAL&&m.writes==0);
    sf_atc_program p={};assert(sf_atc_mode_plan(SF_ATC_OFF,false,&p)==EINVAL);
    puts("ATC six modes/both orientations match Linux table; calibration preflight/apply failures prevent routing/reset release");
}
