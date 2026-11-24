// SPDX-License-Identifier: MIT
#include "sf_atc_calibration.h"
#include <map>
#include <string>
#include <array>
#include <vector>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cerrno>
struct Model {
    std::map<std::string,std::array<uint8_t,12>> props;
    std::vector<std::string> reads;
    std::vector<unsigned> writes;
    std::string missing;
    static const void *get(void *ctx,const char *path,const char *name,size_t *len) {
        auto &m=*(Model*)ctx;assert(!strcmp(path,"/arm-io/atc-phy1"));m.reads.push_back(name);
        if(m.missing==name)return nullptr;
        auto &v=m.props[name];v={0,0,0,32,0xf,0,0,0,0xff,0,0,0};*len=12;return v.data();
    }
    static uint32_t read(void *,sf_atc_window w,uint32_t off){assert(w==SF_ATC_CORE&&off<0x15000);return 0x12345670;}
    static void write(void *ctx,sf_atc_window w,uint32_t off,uint32_t value){assert(w==SF_ATC_CORE&&off<0x15000&&value==0x1234567f);((Model*)ctx)->writes.push_back(off);}
    static void delay(void *,unsigned){}
    sf_atc_io io(){return {this,read,write,delay,nullptr};}
};
int main(){unsigned count=0;
    for(sf_atc_mode mode:{SF_ATC_MODE_USB3,SF_ATC_MODE_USB3_DP,SF_ATC_MODE_TBT,SF_ATC_MODE_USB4,SF_ATC_MODE_DP})for(bool reverse:{false,true}) {
        Model m;auto io=m.io();sf_atc_program p;assert(!sf_atc_mode_plan(mode,reverse,&p));
        sf_atc_cal_bundle bundle={};bundle.io=&io;bundle.adt_ctx=&m;bundle.getprop=Model::get;
        auto callbacks=sf_atc_calibration_callbacks(&bundle);
        assert(!callbacks.validate(callbacks.ctx,&p) && m.writes.empty());
        unsigned groups=mode==SF_ATC_MODE_DP?2:(mode==SF_ATC_MODE_TBT||mode==SF_ATC_MODE_USB4)?8:5;
        assert(bundle.count==groups);
        assert(m.reads[0].find(reverse?"LN1":"LN0")!=std::string::npos);
        if(mode==SF_ATC_MODE_USB3||mode==SF_ATC_MODE_USB3_DP) {
            assert(m.reads[0].find("USB_")!=std::string::npos && m.reads[4].find("DP_")!=std::string::npos);
            assert(m.reads[4].find(reverse?"LN0":"LN1")!=std::string::npos);
        }
        assert(!callbacks.apply(callbacks.ctx,&p) && m.writes.size()==groups);
        auto names=m.reads;
        for(auto missing:names) {
            m.reads.clear();m.writes.clear();m.missing=missing;
            assert(callbacks.validate(callbacks.ctx,&p)==ENOENT && !bundle.checked && m.writes.empty());++count;
            assert(callbacks.apply(callbacks.ctx,&p)==EINVAL && m.writes.empty());
        }
        m.missing.clear();assert(!callbacks.validate(callbacks.ctx,&p));
        auto changed=p;changed.crossbar^=1;
        assert(callbacks.apply(callbacks.ctx,&changed)==EINVAL && m.writes.empty());
        m.props[m.reads.back()][3]=16;
        assert(callbacks.apply(callbacks.ctx,&p)==EINVAL && m.writes.empty()); // no partial early writes
    }
    printf("ATC real lane-property grouping: 10 mode/orientation bundles, %u missing properties, changed/malformed bundle refuses before writes\n",count);
}
