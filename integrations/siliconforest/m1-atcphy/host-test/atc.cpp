// SPDX-License-Identifier: MIT
#include "sf_atc_usb2.h"
#include "sf_atc_tunables.h"
#include <array>
#include <vector>
#include <tuple>
#include <cassert>
#include <cstdio>
#include <cstring>
struct Model {
    std::array<std::array<uint32_t, 0x50000 / 4>, 4> regs{};
    std::vector<std::tuple<unsigned,unsigned,unsigned>> writes;
    unsigned fail = 0, common = 0;
    bool down = false, ackSeen = false;
    static uint32_t read(void *c, sf_atc_window w, uint32_t r) {
        auto &m=*(Model*)c;
        assert(r < 0x50000 && !(r & 3));
        if (w == SF_ATC_PIPE && r == 0x14) {
            if (m.fail == 1 || (m.fail == 7 && m.ackSeen)) return 1;
            uint32_t ack=m.regs[SF_ATC_PIPE][0x10/4]&1;
            if (ack) m.ackSeen=true;
            return m.fail==6?0:ack;
        }
        if (w == SF_ATC_CORE && r == 0x20004) {
            uint32_t power = m.regs[SF_ATC_CORE][0x20000/4] & 3;
            if (m.fail == 2) return 0;
            if (m.fail == 3) return power & ~2u;
            if (m.fail == 5 && m.down) return 3;
            return power;
        }
        return m.regs[w][r/4];
    }
    static void write(void *c, sf_atc_window w, uint32_t r,uint32_t v) {
        auto &m=*(Model*)c;
        assert(r < 0x50000 && !(r & 3));
        m.regs[w][r/4]=v; m.writes.emplace_back(w,r,v);
    }
    static void delay(void *, unsigned us) { assert(us == 10 || us == 100); }
    static int tunables(void *c) { auto &m=*(Model*)c; ++m.common; return m.fail == 4 ? EINVAL : 0; }
    sf_atc_io io() { return {this,read,write,delay,tunables}; }
};
static uint32_t tr(void *ctx, uint32_t off) { assert(off==4); return *(uint32_t*)ctx; }
static void tw(void *ctx, uint32_t off, uint32_t v) { assert(off==4); *(uint32_t*)ctx=v; }
int main() {
    for (unsigned flip=0; flip<2; ++flip) for (unsigned n=0; n<50; ++n) {
        Model m; auto io=m.io();
        m.regs[SF_ATC_CORE][0x4c/4] = 0xa5000000;
        assert(!sf_atc_usb2_reset_assert(&io));
        assert(m.regs[SF_ATC_USB2][0] == 4);
        assert(!sf_atc_usb2_start(&io,flip));
        assert(m.common==1 && !(m.regs[SF_ATC_PIPE][0x1c/4]&1));
        assert((m.regs[SF_ATC_CORE][0x48/4]&0xfff)==0x6db);
        assert(m.regs[SF_ATC_CORE][0x4c/4] == (0xa5000000 | (flip?0xb:0xa)));
        assert(!(m.regs[SF_ATC_USB2][8/4] & (7u<<12)));
        sf_atc_usb2_release_reset(&io);
        assert((m.regs[SF_ATC_PIPE][0x1c/4]&0x11)==1);
        sf_atc_usb2_select_host(&io);
        assert((m.regs[SF_ATC_USB2][8/4]&(7u<<12))==(7u<<12));
        m.down=true;
        assert(!sf_atc_usb2_stop(&io));
        assert((m.regs[SF_ATC_PIPE][0x1c/4]&0x11)==0x10);
        assert(m.regs[SF_ATC_USB2][0]==4);
        assert(!(m.regs[SF_ATC_CORE][0x20000/4]&0x1b));
    }
    for (unsigned fail=1; fail<=5; ++fail) {
        Model m; auto io=m.io(); m.fail=fail;
        if (fail==1) {
            assert(sf_atc_usb2_reset_assert(&io)==ETIMEDOUT);
            assert(m.writes.size()==1); // failed PIPE ack: no reset or PHY changes
        } else if (fail==5) {
            assert(!sf_atc_usb2_start(&io,0)); m.down=true;
            assert(sf_atc_usb2_stop(&io)==ETIMEDOUT);
            assert(m.regs[SF_ATC_CORE][0x20000/4]&1); // don't advance to SMALL
        } else {
            assert(sf_atc_usb2_start(&io,0)==(fail==4?EINVAL:ETIMEDOUT));
            assert(!(m.regs[SF_ATC_CORE][0x20000/4]&(1u<<4)));
            assert(!(m.regs[SF_ATC_PIPE][0x1c/4]&1));
        }
    }
    for (unsigned fail : {0u,6u,7u}) {
        Model m; auto io=m.io(); m.fail=fail;
        m.regs[SF_ATC_PIPE][0x1c/4]=1; // previous non-dummy backend, stopped host
        int ret=sf_atc_usb2_reset_assert(&io);
        assert(ret==(fail?ETIMEDOUT:0));
        if (fail) {
            assert(m.regs[SF_ATC_PIPE][0x1c/4]==1); // no reset until handoff finishes
            assert(m.regs[SF_ATC_USB2][0]==0);
        }
    }
    uint8_t raw[24]={4,0,0,32,0xf,0,0,0,0xff,0,0,0};
    uint32_t val=0x12345670;
    assert(!sf_atc_tunable_apply(raw,12,8,0,&val,tr,tw) && val==0x1234567f);
    // A malformed final record must prevent the first valid write too.
    val=0x12345670; raw[15]=16;
    assert(sf_atc_tunable_apply(raw,24,8,0,&val,tr,tw)==EINVAL && val==0x12345670);
    for (unsigned width=0;width<256;++width) for (unsigned off=0;off<64;++off) {
        raw[0]=off; raw[3]=width;
        bool valid=width==32 && !(off&3) && off<=28;
        assert((sf_atc_tunable_validate(raw,12,32)==0)==valid);
    }
    puts("100 ATC USB2 cycles/orientations; PIPE/power/tunable failure ordering; 16384 compact-record cases passed");
}
