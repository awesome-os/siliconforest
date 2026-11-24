// SPDX-License-Identifier: MIT
#include "sf_pciec_port.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <map>
struct Device {
    std::map<unsigned,unsigned> regs;
    unsigned operations=0,failAt=0,elapsed=0,rids=0;
    bool ready=true,busy=false,reset=false,up=true,stale=false,started=false;
    int access(){return ++operations==failAt?-EIO:0;}
    static int read(void *p,sf_pciec_window w,uint32_t off,uint32_t *v){
        auto &d=*(Device *)p;assert(w==SF_PCIEC_PORT && !(off&3));
        int ret=d.access();if(ret)return ret;
        if(off==0x804)*v=d.ready?1:0;
        else if(off==0x208)*v=(d.busy?4:0)|(d.started && d.up?1:0);
        else if(off==0x98c)*v=d.reset?1:0;
        else *v=d.regs[off] | (d.stale && off==0x924?0x80000000u:0);
        return 0;
    }
    static int write(void *p,sf_pciec_window w,uint32_t off,uint32_t v){
        auto &d=*(Device *)p;assert(w==SF_PCIEC_PORT && !(off&3));
        int ret=d.access();if(ret)return ret;
        d.regs[off]=v;
        if(off>=0x828 && off<0x928){assert(!v);++d.rids;}
        if(off==0x80 && (v&1))d.started=true;
        return 0;
    }
    static int tune(void *p,sf_pciec_window w){
        auto &d=*(Device *)p;int ret=d.access();if(ret)return ret;
        if(w==SF_PCIEC_CONFIG){
            assert((d.regs[0x800]&0x101)==1 && (d.regs[0x814]&1));
            assert(d.rids==64 && !d.started);
        }
        return 0;
    }
    static void delay(void *p,unsigned us){((Device *)p)->elapsed+=us;}
    sf_pciec_io io(){return {this,read,write,tune,delay};}
};
int main(){
    Device ok;auto io=ok.io();assert(!sf_pciec_cold_init(&io));
    assert(ok.rids==64 && ok.regs[0x130]==0 && !ok.started && !ok.elapsed);
    unsigned n=ok.operations;
    for(unsigned i=1;i<=n;++i){
        Device d;d.failAt=i;auto o=d.io();assert(sf_pciec_cold_init(&o)==-EIO);
        assert(d.operations==i && !d.started); // no access or LTSSM after failure
    }
    Device stale;stale.stale=true;io=stale.io();assert(sf_pciec_cold_init(&io)==-EIO && !stale.started);
    for(unsigned mode=0;mode<2;++mode){
        Device d;d.ready=mode;d.busy=mode;io=d.io();
        assert(sf_pciec_cold_init(&io)==-ETIMEDOUT && d.elapsed==250000 && !d.started);
    }
    Device link;io=link.io();assert(!sf_pciec_link_start(&io) && link.started);
    n=link.operations;
    for(unsigned i=1;i<=n;++i){
        Device d;d.failAt=i;io=d.io();assert(sf_pciec_link_start(&io)==-EIO && d.operations==i);
    }
    Device reset;reset.reset=true;io=reset.io();
    assert(sf_pciec_link_start(&io)==-ETIMEDOUT && reset.elapsed==250000 && !reset.started);
    Device down;down.up=false;io=down.io();
    assert(sf_pciec_link_start(&io)==-ETIMEDOUT && down.elapsed==500000 && down.started);
    Device invalid;io=invalid.io();io.tune=nullptr;
    assert(sf_pciec_cold_init(&io)==-EINVAL && !invalid.operations);
    assert(sf_pciec_link_start(nullptr)==-EINVAL);
    puts("PCIe-C host register model: all I/O failures stop access, stale RID rejected, bounded RUN/BUSY/reset/link timeouts; no hardware claim");
}
