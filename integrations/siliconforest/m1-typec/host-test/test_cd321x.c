/* SPDX-License-Identifier: GPL-2.0 */
#include "sf_cd321x.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint8_t wire[65], out[66];
    for (unsigned n = 1; n <= 64; ++n) {
        for (unsigned i = 1; i < sizeof(wire); ++i) wire[i] = (uint8_t)(i * 17);
        for (unsigned advertised = 0; advertised <= 255; ++advertised) {
            wire[0] = (uint8_t)advertised;
            memset(out, 0xa5, sizeof(out));
            int ret = sf_cd321x_unpack(wire, n + 1, out + 1, n);
            assert(ret == (advertised < n ? EIO : 0));
            assert(out[0] == 0xa5 && out[n + 1] == 0xa5);
            if (!ret) assert(memcmp(out + 1, wire + 1, n) == 0);
            else for (unsigned i = 0; i < sizeof(out); ++i) assert(out[i] == 0xa5);
        }
        wire[0] = (uint8_t)n;
        for (unsigned size = 0; size <= n; ++size) {
            memset(out, 0xa5, sizeof(out));
            assert(sf_cd321x_unpack(wire, size, out, n) == EIO);
            for (unsigned i = 0; i < sizeof(out); ++i) assert(out[i] == 0xa5);
        }
    }
    assert(sf_cd321x_unpack(NULL, 65, out, 4) == EINVAL);
    assert(sf_cd321x_unpack(wire, 65, NULL, 4) == EINVAL);
    assert(sf_cd321x_unpack(wire, 65, out, 0) == EINVAL);
    assert(sf_cd321x_unpack(wire, 65, out, 65) == EINVAL);
    const uint8_t word[] = {0xef, 0xcd, 0xab, 0x89};
    assert(sf_cd321x_le32(word) == 0x89abcdefu);
    for (unsigned flags = 0; flags < 256; ++flags) {
        uint32_t d = ((flags & 1) ? SF_TPS_DATA_USB2 : 0) |
                     ((flags & 2) ? SF_TPS_DATA_USB3 : 0) |
                     ((flags & 4) ? SF_TPS_DATA_USB_DEVICE : 0) |
                     ((flags & 8) ? SF_TPS_DATA_TBT : 0) |
                     ((flags & 16) ? SF_CD321X_DATA_USB4 : 0) |
                     ((flags & 32) ? SF_TPS_DATA_DP : 0);
        uint32_t s = ((flags & 64) ? SF_TPS_STATUS_PLUG_PRESENT : 0) |
                     ((flags & 128) ? SF_TPS_STATUS_DATAROLE : 0);
        enum sf_cd321x_usb_role want = SF_CD321X_USB_NONE;
        if ((flags & 64) && (flags & 3))
            want = (flags & 4) ? SF_CD321X_USB_DEVICE : SF_CD321X_USB_HOST;
        assert(sf_cd321x_usb_role(s, d) == want);
    }
    // The actual TS3-only snapshot from tc01 must not start the direct xHC.
    assert(sf_cd321x_usb_role(0x100cb20d, 0x16110081) == SF_CD321X_USB_NONE);
    const uint8_t intel[] = {0x12,0xa1,0xa2,0xa3,0xa4,0x34,0x56,0x78,0x9a};
    assert(sf_cd321x_tbt_vdo(intel)==0x56349a78);
    struct sf_cd321x_cable_snapshot full={{0x100cb20d,0x16110081,1,0},0,SF_CD321X_TBT_VALID}, decoded;
    uint32_t info=0;
    assert(!sf_cd321x_snapshot_decode(&full,sizeof(full),&decoded));
    assert(!memcmp(&full,&decoded,sizeof(full)));
    assert(!sf_cd321x_tbt_cable_info(&decoded,&info) && info==0x411);
    assert(!sf_cd321x_snapshot_decode(&full,sizeof(full.port),&decoded));
    assert(!decoded.valid_flags && !decoded.protocol_vdo);
    assert(sf_cd321x_tbt_cable_info(&decoded,&info)==ENODEV);
    decoded=full;
    for(unsigned n=0;n<sizeof(full)+2;++n) {
        if(n==sizeof(full.port) || n==sizeof(full))continue;
        assert(sf_cd321x_snapshot_decode(&full,n,&decoded)==EINVAL);
        assert(!memcmp(&full,&decoded,sizeof(full)));
    }
    // Independent expected Apple cable value for all input bit combinations
    // and all raw speed encodings, including the non-20Gbps value 2.
    for(unsigned speed=0;speed<8;++speed)for(unsigned bits=0;bits<16;++bits) {
        decoded=full;decoded.port.status=SF_TPS_STATUS_PLUG_PRESENT|((bits&1)?SF_TPS_STATUS_UPSIDE_DOWN:0);
        decoded.port.data_status=SF_TPS_DATA_TBT|(speed<<25)|((bits&2)?1u<<22:0)|((bits&4)?1u<<20:0);
        decoded.protocol_vdo=(bits&8)?1:0;
        uint32_t want=0x401|((bits&1)?2:0)|((bits&2)?4:0)|
            ((bits&2) && !(bits&4)?8:0)|((bits&8)?0x200:0)|(speed==3?0x10:0);
        assert(!sf_cd321x_tbt_cable_info(&decoded,&info) && info==want);
    }
    for(unsigned bad=0;bad<5;++bad) {
        decoded=full;
        if(bad==0)decoded.port.error=EIO;
        if(bad==1)decoded.port.sample=0;
        if(bad==2)decoded.port.status&=~SF_TPS_STATUS_PLUG_PRESENT;
        if(bad==3)decoded.port.data_status&=~SF_TPS_DATA_TBT;
        if(bad==4)decoded.valid_flags=0;
        info=0xfeed;assert(sf_cd321x_tbt_cable_info(&decoded,&info)==ENODEV && info==0xfeed);
    }
    puts("CD321x atomic v1/v2 snapshot + 128 TBT cable encodings + invalid state passed (host only)");
    puts("CD321x register framing: 16384 lengths + 2080 truncations passed (host only)");
    puts("CD321x USB role: 256 flag combinations + actual TS3 snapshot passed (host only)");
    return 0;
}
