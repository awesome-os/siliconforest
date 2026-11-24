// SPDX-License-Identifier: MIT
#include "sf_dwc3_host.h"
#include <array>
#include <vector>
#include <utility>
#include <cassert>
#include <cstdio>
#include <cerrno>
struct Model {
    std::array<uint32_t,0x10000/4> regs{};
    std::vector<std::pair<unsigned,unsigned>> writes;
    unsigned resetReads=0, delays=0;
    bool stuck=false;
    Model() { regs[0xc120/4]=0x33310000; regs[0xc1a0/4]=0x3139302a; regs[0xc140/4]=2; }
    static uint32_t read(void *ctx,uint32_t off) {
        auto &m=*(Model*)ctx; assert(off<0x10000 && !(off&3));
        if (off==0xc704 && (m.regs[off/4]&(1u<<30))) {
            ++m.resetReads;
            if (!m.stuck && m.resetReads==3) m.regs[off/4]&=~(1u<<30);
        }
        return m.regs[off/4];
    }
    static void write(void *ctx,uint32_t off,uint32_t value) {
        auto &m=*(Model*)ctx; assert(off<0x10000 && !(off&3));
        m.regs[off/4]=value; m.writes.emplace_back(off,value);
        // Hardware must finish device reset before Apple settings or role host.
        if (off==0xcd38 || (off==0xc110 && ((value>>12)&3)==1))
            assert(!(m.regs[0xc704/4]&(1u<<30)));
    }
    static void delay(void *ctx,unsigned us) { auto &m=*(Model*)ctx; assert(us==20000||us==50000);++m.delays; }
    sf_dwc3_io io() { return {this,read,write,delay}; }
};
int main() {
    for (unsigned role=0;role<4;++role) for(unsigned power=0;power<3;++power) {
        Model m; auto io=m.io();
        m.regs[0xc110/4]=(role<<12)|0xabc00000|0x38;
        m.regs[0xc144/4]=power<<24; m.regs[0xcd40/4]=0x5a000000;
        assert(!sf_dwc3_host_init(&io));
        assert((m.regs[0xc110/4]&(3u<<12))==(1u<<12));
        assert((m.regs[0xc110/4]&0xabc00000)==0xabc00000);
        assert(m.regs[0xcd40/4]==0x5a140a10);
        assert(m.regs[0xc408/4]==(1u<<31));
        assert(m.resetReads==(role==1?0:3));
    }
    Model stuck; auto io=stuck.io(); stuck.stuck=true;
    assert(sf_dwc3_host_init(&io)==ETIMEDOUT);
    assert(stuck.regs[0xcd38/4]==0 && (stuck.regs[0xc110/4]&(3u<<12))==0);
    for (unsigned fault=0;fault<4;++fault) {
        Model m; auto x=m.io();
        if(fault==0)m.regs[0xc120/4]=UINT32_MAX;
        if(fault==1)m.regs[0xc1a0/4]=0;
        if(fault==2)m.regs[0xc140/4]=0;
        if(fault==3)m.regs[0xc140/4]=3;
        assert(sf_dwc3_host_init(&x)==ENODEV && m.writes.empty());
    }
    puts("DWC_usb31 core/reset/role ordering and invalid-ID/reset-timeout cases passed");
}
