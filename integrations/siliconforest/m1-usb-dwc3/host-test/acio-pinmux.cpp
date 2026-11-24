/* SPDX-License-Identifier: MIT */
#include "sf_acio_pinmux.h"
#include <cassert>
#include <cstdio>
struct Pin {
    uint32_t value,writes=0;bool stuck=false,edge=false;
    static uint32_t read(void *p){return ((Pin *)p)->value;}
    static void write(void *p,uint32_t v){
        auto &s=*(Pin *)p;++s.writes;
        if(!s.stuck)s.value=v^(s.edge?1:0);
    }
    int apply(uint32_t &a,uint32_t &b){sf_acio_pinmux_io io={this,read,write};return sf_acio1_pinmux(&io,&a,&b);}
};
int main()
{
    uint32_t before=0,after=0;
    for(uint32_t mux=0;mux<4;++mux)for(uint32_t input=0;input<2;++input){
        Pin p{0x40818e|(mux<<5)|(input<<9)};uint32_t old=p.value;
        assert(!p.apply(before,after) && before==old && after==((old&~0x260)|0x220));
        assert(!p.apply(before,after) && p.writes==(old==after?0u:1u));
    }
    // Each unrelated bit survives; the pin's DATA bit may change on readback.
    for(unsigned bit=0;bit<32;++bit){
        if((1u<<bit)&(0x260|(1u<<21)))continue;
        Pin p{1u<<bit};assert(!p.apply(before,after));assert(after==((1u<<bit)|0x220));
    }
    Pin locked{1u<<21};assert(locked.apply(before,after)==EBUSY && !locked.writes);
    Pin correctLocked{(1u<<21)|0x220};assert(!correctLocked.apply(before,after) && !correctLocked.writes);
    Pin inaccessible{UINT32_MAX};assert(inaccessible.apply(before,after)==ENXIO && !inaccessible.writes);
    Pin stuck{0};stuck.stuck=true;assert(stuck.apply(before,after)==EIO && stuck.writes==1);
    Pin edge{0};edge.edge=true;assert(!edge.apply(before,after) && after==0x221);
    assert(sf_acio1_pinmux(nullptr,&before,&after)==EINVAL);
    puts("ACIO1 pinctrl host model: mux/input combinations, preservation, lock, inaccessible, stuck write, changing DATA and idempotence passed");
}
