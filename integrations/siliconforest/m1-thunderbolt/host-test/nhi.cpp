// SPDX-License-Identifier: MIT
#include "sf_apple_nhi.h"
#include <vector>
#include <tuple>
#include <cassert>
#include <cstdio>
#include <cerrno>
#include <cstring>
struct Model {
    std::vector<uint32_t> reg=std::vector<uint32_t>(0x100000/4);
    std::vector<uint32_t> pdf=std::vector<uint32_t>(0x40000/4);
    std::vector<std::tuple<unsigned,unsigned,unsigned>> writes;
    unsigned fences=0;
    bool on=true, failDisable=false;
    Model(){reg[0]=16;}
    static bool powered(void *c){return ((Model*)c)->on;}
    static uint32_t read(void *c,uint32_t off){auto &m=*(Model*)c;assert(m.on && !(off&3) && off/4<m.reg.size());return m.reg[off/4];}
    static void write(void *c,uint32_t off,uint32_t val){
        auto &m=*(Model*)c;assert(m.on && !(off&3) && off/4<m.reg.size());
        m.writes.emplace_back(0,off,val);
        if (m.failDisable && (off&0x3fff)==0x10 && !val)return;
        m.reg[off/4]=val;
        if(off<0xd0000 && (off&0x3fff)==0x10 && val&(1u<<31)) {
            assert(m.fences); // memory/address/config visible before VALID
            assert(m.reg[(off-0x10)/4]==0x12340000);
        }
    }
    static void writePDF(void *c,uint32_t off,uint32_t val){auto &m=*(Model*)c;assert(m.on && !(off&3) && off/4<m.pdf.size());m.pdf[off/4]=val;m.writes.emplace_back(1,off,val);}
    static void fence(void *c){++((Model*)c)->fences;}
    sf_nhi_io io(){return {this,powered,read,write,fence,0x100000,0x40000,writePDF};}
};
struct Dpin {
    uint32_t ctrl=0x55550001,ack=1;
    unsigned waits=0,writes=0,hpdAfter=0,ackAfter=0;
    bool invalid=false;
    static uint32_t read(void *c,uint32_t off){auto &m=*(Dpin*)c;
        if(m.invalid)return UINT32_MAX;
        if(off==0)return m.waits>=m.hpdAfter?4:0;
        if(off==0xc)return m.ctrl;
        assert(off==0x10);if(m.waits>=m.ackAfter)m.ack=m.ctrl&1;return m.ack;
    }
    static void write(void *c,uint32_t off,uint32_t v){auto &m=*(Dpin*)c;assert(off==0xc);m.ctrl=v;++m.writes;}
    static int wait(void *c){auto &m=*(Dpin*)c;return ++m.waits>=500?-ETIMEDOUT:0;}
    sf_dpin_io io(){return {this,read,write,wait};}
};
int main(){
    for(unsigned hop=0;hop<16;++hop)for(bool tx:{false,true})for(bool e2e:{false,true}) {
        Model m;auto io=m.io();
        sf_nhi_ring r={0x12340000,hop,16,256,0x1234,0x5678,UINT32_MAX,7,tx,false,e2e,true,false};
        assert(!sf_apple_nhi_ring_start(&io,&r) && r.running);
        unsigned desc=(tx?0x10000:0x80000)+hop*0x4000;
        unsigned idx=hop+(tx?0:16);
        assert(m.reg[(desc+0x10)/4]==((1u<<31)|(1u<<30)|(e2e?((1u<<28)|(tx?0:7u<<12)):0)));
        assert(m.reg[(desc+0x14)/4]==(tx?(hop==0?2:hop<=5?40:5):0x12345678));
        assert(m.reg[(0xd004c+4*idx)/4]==65535);
        if(!tx)assert(m.pdf[hop*0x4000/4]==0x12345678);
        assert(m.reg[0xd0010/4]&(1u<<idx));
        assert(sf_apple_nhi_ring_start(&io,&r)==-EBUSY);
        m.failDisable=true;
        assert(sf_apple_nhi_ring_stop(&io,&r)==-EIO && r.running);
        assert(m.reg[desc/4]==0x12340000); // retain descriptor address after failed disable
        m.failDisable=false;
        assert(!sf_apple_nhi_ring_stop(&io,&r) && !r.running);
        assert(m.reg[desc/4]==0 && m.reg[(desc+0x10)/4]==0);
        m.on=false;auto writes=m.writes.size();
        assert(sf_apple_nhi_ring_start(&io,&r)==-ESHUTDOWN && m.writes.size()==writes);
    }
    Model m;auto io=m.io();sf_nhi_ring r={0x12340000,0,16,256,0,0,0,0,true,false,false,false,false};
    m.reg[0]=15;assert(sf_apple_nhi_ring_start(&io,&r)==-ENODEV && m.writes.empty());
    r.dma=(1ULL<<42)-16;r.count=2;assert(sf_apple_nhi_ring_start(&io,&r)==-EINVAL && m.writes.empty());
    uint8_t descriptor[16];memset(descriptor,0xa5,16);
    assert(!sf_apple_nhi_descriptor(descriptor,0x123456789a,4096,2,1,true));
    const uint8_t expected[]={0x9a,0x78,0x56,0x34,0x12,0,0,0,0,0x10,0xc2,0,0,0,0,0};
    assert(!memcmp(descriptor,expected,16));
    for(unsigned len=1;len<=4096;++len)assert(!sf_apple_nhi_descriptor(descriptor,0x40000,len,15,15,false));
    memset(descriptor,0xa5,16);
    assert(sf_apple_nhi_descriptor(descriptor,0x40000,4097,0,0,false)==-EINVAL && descriptor[0]==0xa5);
    uint64_t off=123;
    assert(!sf_apple_cio_sram_offset(0x10000000,0x4000,0x501000000,0x4000,&off) && off==0);
    assert(sf_apple_cio_sram_offset(0x10003fff,2,0x501000000,0x4000,&off)==-EFAULT);
    assert(sf_apple_cio_sram_offset(0xfffffff,1,0x501000000,0x4000,&off)==-EINVAL);
    assert(sf_apple_cio_sram_offset(0x10000000,1,UINT64_MAX,2,&off)==-EINVAL);
    assert(sf_apple_cio_sram_offset(0x10000000,0,0x501000000,0x4000,&off)==-EINVAL);
    for(unsigned hpd:{0,100,400})for(unsigned ack:{0,200,499}) {
        Dpin d;auto x=d.io();d.hpdAfter=hpd;d.ackAfter=ack;
        assert(!sf_apple_dpin_t8103_active(&x,true));
        assert(d.ctrl==0x55550000 && d.waits<500);
        assert(!sf_apple_dpin_t8103_active(&x,false) && d.ctrl==0x55550001);
    }
    Dpin noHPD;auto x=noHPD.io();noHPD.hpdAfter=501;
    assert(sf_apple_dpin_t8103_active(&x,true)==-ETIMEDOUT && noHPD.writes==0);
    Dpin noACK;auto y=noACK.io();noACK.hpdAfter=400;noACK.ackAfter=501;
    assert(sf_apple_dpin_t8103_active(&y,true)==-ETIMEDOUT && noACK.waits==500); // one budget
    Dpin invalid;auto z=invalid.io();invalid.invalid=true;
    assert(sf_apple_dpin_t8103_active(&z,true)==-EIO && invalid.writes==0);
    puts("Apple NHI 64 ring/E2E cases, DMA bounds/descriptor wire bytes, SRAM bounds and T8103 DP-IN one-budget handshake passed");
}
