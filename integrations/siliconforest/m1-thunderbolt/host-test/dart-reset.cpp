// SPDX-License-Identifier: MIT
#include "sf_cio_dart_reset.h"
#include <map>
#include <cassert>
#include <cstdio>
struct Bank {
    std::map<unsigned,unsigned> regs;
    unsigned calls=0,failAt=0,flushes=0;
    bool stuckTcr=false,stuckRoot=false,stuckError=false,timeout=false;
    Bank(){
        for(unsigned sid=0;sid<16;++sid){
            regs[0x100+4*sid]=0x80080;
            for(unsigned n=0;n<4;++n)regs[0x200+16*sid+4*n]=0x80001000+sid;
        }
        regs[0x40]=0x8f0f0001;
    }
    int step(){return ++calls==failAt?EIO:0;}
    static int read(void *p,uint32_t off,uint32_t *v){
        auto &b=*(Bank *)p;int ret=b.step();if(ret)return ret;*v=b.regs[off];return 0;
    }
    static int write(void *p,uint32_t off,uint32_t v){
        auto &b=*(Bank *)p;int ret=b.step();if(ret)return ret;
        if(off>=0x100 && off<0x140){assert(!v);if(b.stuckTcr && off==0x13c)return 0;}
        if(off>=0x200 && off<0x300){assert(!v);if(b.stuckRoot && off==0x2fc)return 0;}
        if(off==0x40){assert(!b.flushes);if(!b.stuckError)b.regs[off]&=~v;}
        else b.regs[off]=v;
        return 0;
    }
    static int invalidate(void *p,uint32_t streams){
        auto &b=*(Bank *)p;int ret=b.step();if(ret)return ret;
        assert(streams==0xffff && b.regs[0xfc]==0xffff && !b.regs[0x40]);
        for(unsigned sid=0;sid<16;++sid){
            assert(!b.regs[0x100+4*sid]);
            for(unsigned n=0;n<4;++n)assert(!b.regs[0x200+16*sid+4*n]);
        }
        ++b.flushes;return b.timeout?ETIMEDOUT:0;
    }
    sf_cio_dart_reset_io io(){return {this,read,write,invalidate};}
};
int main(){
    Bank ok;auto io=ok.io();assert(!sf_cio_dart_reset(&io));
    assert(!ok.regs[0x13c] && !ok.regs[0x40] && ok.flushes==1);
    for(unsigned fail=1;fail<=ok.calls;++fail){
        Bank b;b.failAt=fail;io=b.io();assert(sf_cio_dart_reset(&io)==EIO && b.calls==fail);
    }
    for(unsigned mode=0;mode<4;++mode){
        Bank b;b.stuckTcr=mode==0;b.stuckRoot=mode==1;b.stuckError=mode==2;b.timeout=mode==3;
        io=b.io();assert(sf_cio_dart_reset(&io)==(mode==3?ETIMEDOUT:EIO));
        if(mode==0 || mode==1)assert(!b.flushes && b.regs[0x40]==0x8f0f0001);
        if(mode==2)assert(!b.flushes); // never flush while a fault remains latched
    }
    assert(sf_cio_dart_reset(nullptr)==EINVAL);
    puts("NHI DART full-bank reset: 16 streams/64 roots disabled, no bypass, all access failures stop, stuck SID15/root/error and TLB timeout retained (host model)");
}
