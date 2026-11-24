#include "channel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static unsigned checks, groups;
static const enum agxc_profile P = AGXC_V13_5_G13_SOURCE_ONLY;
/* Literal fixtures independently reviewed against pinned Construct field order. */
static const uint8_t dc_init[48] = {0x1a};
static const uint8_t dc_idle[48] = {0x23};
static const uint8_t fw[20] = {0x00,0x40,0x56,0x34,0x12,0,0,0, 0,0,0,0,
    0xd4,0xc3,0xb2,0xa1, 1,0,2,0};
static void fill32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static void test_profiles_dc(void) {
    ++groups; enum agxc_profile p = AGXC_NO_PROFILE;
    CHECK(agxc_select(AGXC_V13_5, AGXC_G13, AGXC_PINNED_SOURCE_TEST, &p) == AGXC_OK && p == P);
    for (int v=-1;v<4;++v) for (int g=-1;g<4;++g) for (int e=-1;e<4;++e) {
        if (v==AGXC_V13_5 && g==AGXC_G13 && e==AGXC_PINNED_SOURCE_TEST) continue;
        p=AGXC_NO_PROFILE;
        CHECK(agxc_select((enum agxc_firmware)v,(enum agxc_gpu)g,(enum agxc_evidence)e,&p)==AGXC_ABI_UNKNOWN);
        CHECK(p==AGXC_NO_PROFILE);
    }
    CHECK(agxc_select(AGXC_V13_5,AGXC_G13,AGXC_PINNED_SOURCE_TEST,NULL)==AGXC_ARGUMENT);
    uint8_t b[64]; enum agxc_dc_pattern decoded;
    for (unsigned k=0;k<2;++k) {
        enum agxc_dc_pattern kind=k?AGXC_DC_UPDATE_IDLE_TS:AGXC_DC_INIT;
        const uint8_t *gold=k?dc_idle:dc_init;
        memset(b,0xa5,sizeof(b)); CHECK(agxc_dc_encode(P,kind,b,48)==AGXC_OK);
        CHECK(!memcmp(b,gold,48) && b[48]==0xa5);
        CHECK(agxc_dc_decode(P,b,48,&decoded)==AGXC_OK && decoded==kind);
        for (unsigned i=4;i<48;++i) for (unsigned bit=0;bit<8;++bit) {
            b[i]=(uint8_t)(1u<<bit); decoded=AGXC_DC_UPDATE_IDLE_TS;
            CHECK(agxc_dc_decode(P,b,48,&decoded)==AGXC_PATTERN); CHECK(decoded==AGXC_DC_UPDATE_IDLE_TS); b[i]=0;
        }
    }
    for (unsigned k=0;k<256;++k) if (k!=0x1a && k!=0x23) {
        memset(b,0xa5,sizeof(b)); CHECK(agxc_dc_encode(P,(enum agxc_dc_pattern)k,b,48)==AGXC_OPCODE); CHECK(b[0]==0xa5);
        memcpy(b,dc_init,48); fill32(b,k); CHECK(agxc_dc_decode(P,b,48,&decoded)==AGXC_OPCODE);
    }
    memcpy(b,dc_init,48); b[1]=1; CHECK(agxc_dc_decode(P,b,48,&decoded)==AGXC_OPCODE);
    for (size_t n=0;n<65;++n) if(n!=48) {
        memset(b,0xa5,sizeof(b)); CHECK(agxc_dc_encode(P,AGXC_DC_INIT,b,n)==AGXC_SIZE); CHECK(b[0]==0xa5);
        CHECK(agxc_dc_decode(P,b,n,&decoded)==AGXC_SIZE);
    }
    CHECK(agxc_dc_encode(AGXC_NO_PROFILE,AGXC_DC_INIT,b,48)==AGXC_PROFILE);
    CHECK(agxc_dc_decode((enum agxc_profile)99,b,48,&decoded)==AGXC_PROFILE);
    CHECK(agxc_dc_encode(P,AGXC_DC_INIT,NULL,48)==AGXC_ARGUMENT);
    CHECK(agxc_dc_decode(P,NULL,48,&decoded)==AGXC_ARGUMENT);
    CHECK(agxc_dc_decode(P,b,48,NULL)==AGXC_ARGUMENT);
    CHECK(agxc_dc_decode(P,b,48,(enum agxc_dc_pattern *)b)==AGXC_ARGUMENT);
}
static void test_fw_patterns(void) {
    ++groups; struct agxc_synthetic_range range={UINT64_C(0x1234564000),0x8000};
    struct agxc_range_facts f={&range,1};
    struct agxc_fwctl_pattern in={range.address,UINT32_C(0xa1b2c3d4)}, out={77,88}; uint8_t b[64];
    memset(b,0xa5,sizeof(b)); CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_OK); CHECK(!memcmp(b,fw,20) && b[20]==0xa5);
    CHECK(agxc_fwctl_decode(P,fw,20,f,&out)==AGXC_OK); CHECK(out.address==in.address && out.context_id==in.context_id);
    for(unsigned i=8;i<20;++i) if(i<12 || i>=16) for(unsigned bit=0;bit<8;++bit) {
        memcpy(b,fw,20); b[i]^=(uint8_t)(1u<<bit); out=(struct agxc_fwctl_pattern){77,88};
        CHECK(agxc_fwctl_decode(P,b,20,f,&out)==AGXC_PATTERN); CHECK(out.address==77 && out.context_id==88);
    }
    for(size_t n=0;n<65;++n) if(n!=20) {
        memset(b,0xa5,sizeof(b)); CHECK(agxc_fwctl_encode(P,&in,f,b,n)==AGXC_SIZE); CHECK(b[0]==0xa5);
        CHECK(agxc_fwctl_decode(P,b,n,f,&out)==AGXC_SIZE);
    }
    for(unsigned bit=0;bit<32;++bit) {
        in.context_id=UINT32_C(1)<<bit; CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_OK);
        CHECK(agxc_fwctl_decode(P,b,20,f,&out)==AGXC_OK && out.context_id==in.context_id);
    }
    in.address=0; in.context_id=UINT32_MAX; f=(struct agxc_range_facts){NULL,0};
    CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_OK); CHECK(agxc_fwctl_decode(P,b,20,f,&out)==AGXC_OK);
    CHECK(out.address==0 && out.context_id==UINT32_MAX);
    CHECK(agxc_fwctl_encode(AGXC_NO_PROFILE,&in,f,b,20)==AGXC_PROFILE);
    CHECK(agxc_fwctl_decode(AGXC_NO_PROFILE,b,20,f,&out)==AGXC_PROFILE);
    CHECK(agxc_fwctl_encode(P,NULL,f,b,20)==AGXC_ARGUMENT);
    CHECK(agxc_fwctl_encode(P,&in,f,NULL,20)==AGXC_ARGUMENT);
    CHECK(agxc_fwctl_decode(P,NULL,20,f,&out)==AGXC_ARGUMENT);
    CHECK(agxc_fwctl_decode(P,b,20,f,NULL)==AGXC_ARGUMENT);
    CHECK(agxc_fwctl_encode(P,&in,f,(uint8_t *)&in,20)==AGXC_ARGUMENT);
    CHECK(agxc_fwctl_decode(P,b,20,f,(struct agxc_fwctl_pattern *)b)==AGXC_ARGUMENT);
    f=(struct agxc_range_facts){&range,1};
    CHECK(agxc_fwctl_encode(P,&in,f,(uint8_t *)&range,20)==AGXC_ARGUMENT);
    CHECK(agxc_fwctl_decode(P,b,20,f,(struct agxc_fwctl_pattern *)&range)==AGXC_ARGUMENT);
}
static void bad_range(struct agxc_synthetic_range r, uint64_t address) {
    struct agxc_range_facts f={&r,1}; struct agxc_fwctl_pattern in={address,0},out={77,88}; uint8_t b[20];
    memset(b,0xa5,sizeof(b)); CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_RANGE); CHECK(b[0]==0xa5);
    memcpy(b,fw,20); CHECK(agxc_fwctl_decode(P,b,20,f,&out)==AGXC_RANGE); CHECK(out.address==77 && out.context_id==88);
}
static void test_ranges(void) {
    ++groups; struct agxc_synthetic_range r[17];
    for(unsigned i=0;i<17;++i) r[i]=(struct agxc_synthetic_range){UINT64_C(0x100000)+i*0x8000,0x4000};
    uint8_t b[20]; struct agxc_fwctl_pattern in={r[15].address,9},out;
    struct agxc_range_facts f={r,16}; CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_OK);
    CHECK(agxc_fwctl_decode(P,b,20,f,&out)==AGXC_OK && out.address==in.address);
    f.count=17; CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_RANGE);
    f.count=SIZE_MAX; CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_RANGE);
    f.count=1; f.ranges=NULL; CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_RANGE);
    f=(struct agxc_range_facts){r,2}; r[1]=r[0]; in.address=r[0].address;
    CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_RANGE);
    bad_range((struct agxc_synthetic_range){0,0x4000},0);
    bad_range((struct agxc_synthetic_range){0x4000,0},0x4000);
    bad_range((struct agxc_synthetic_range){0x4001,0x4000},0x4001);
    bad_range((struct agxc_synthetic_range){0x4000,0x4001},0x4000);
    bad_range((struct agxc_synthetic_range){UINT64_C(0x8000000000),0x4000},UINT64_C(0x8000000000));
    bad_range((struct agxc_synthetic_range){UINT64_C(0x7fffffc000),0x8000},UINT64_C(0x7fffffc000));
    bad_range((struct agxc_synthetic_range){UINT64_C(0xffffffffffffc000),0x8000},UINT64_C(0xffffffffffffc000));
    const uint64_t good[]={0x4000,UINT64_C(0x7fffffc000),UINT64_C(0xffffff8000000000),UINT64_C(0xffffffffffffc000)};
    for(unsigned i=0;i<4;++i) {
        r[0]=(struct agxc_synthetic_range){good[i],0x4000}; f=(struct agxc_range_facts){r,1}; in.address=good[i];
        CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_OK); CHECK(agxc_fwctl_decode(P,b,20,f,&out)==AGXC_OK && out.address==in.address);
    }
    r[0]=(struct agxc_synthetic_range){0x8000,0x4000}; f=(struct agxc_range_facts){r,1};
    const uint64_t missing[]={0x4000,0xc000,0x8001,UINT64_C(0x10000000000),UINT64_MAX};
    for(unsigned i=0;i<5;++i) { in.address=missing[i]; CHECK(agxc_fwctl_encode(P,&in,f,b,20)==AGXC_RANGE); }
}
static void test_ring(void) {
    ++groups; uint8_t state[64]; struct agxc_ring_snapshot s;
    for(unsigned ch=0;ch<2;++ch) {
        enum agxc_channel kind=(enum agxc_channel)ch; size_t size=ch?32:48, write=ch?16:32,stride=ch?20:48;
        memset(state,0xa5,sizeof(state)); fill32(state,255); fill32(state+write,7);
        CHECK(agxc_state_decode(P,kind,state,size,&s)==AGXC_OK && s.read_index==255 && s.write_index==7);
        for(size_t n=0;n<65;++n) if(n!=size) CHECK(agxc_state_decode(P,kind,state,n,&s)==AGXC_SIZE);
        for(unsigned r=0;r<256;++r) for(unsigned w=0;w<256;++w) {
            s=(struct agxc_ring_snapshot){r,w}; uint32_t occupied=w>=r?w-r:256-r+w;
            for(int tx=0;tx<2;++tx) {
                struct agxc_ring_candidate c={99,88,77,66,55};
                enum agxc_result result=agxc_ring_candidate(P,kind,s,tx,&c);
                if(tx && occupied==255) CHECK(result==AGXC_FULL && c.index==99 && c.byte_offset==55);
                else if(!tx && !occupied) CHECK(result==AGXC_EMPTY && c.index==99 && c.byte_offset==55);
                else {
                    uint32_t index=tx?w:r;
                    CHECK(result==AGXC_OK); CHECK(c.index==index && c.next_index==(index==255?0:index+1));
                    CHECK(c.occupied==occupied && c.available==255-occupied && c.byte_offset==index*stride);
                }
                CHECK(s.read_index==r && s.write_index==w);
            }
        }
        const uint32_t bad[]={256,257,UINT32_MAX};
        for(unsigned i=0;i<3;++i) for(unsigned which=0;which<2;++which) {
            fill32(state,which?0:bad[i]); fill32(state+write,which?bad[i]:0);
            s=(struct agxc_ring_snapshot){77,88}; CHECK(agxc_state_decode(P,kind,state,size,&s)==AGXC_INDEX); CHECK(s.read_index==77 && s.write_index==88);
            s=(struct agxc_ring_snapshot){which?0:bad[i],which?bad[i]:0}; struct agxc_ring_candidate c;
            CHECK(agxc_ring_candidate(P,kind,s,1,&c)==AGXC_INDEX);
        }
    }
    struct agxc_ring_candidate c; s=(struct agxc_ring_snapshot){0,1};
    CHECK(agxc_state_decode(AGXC_NO_PROFILE,AGXC_DEVCTRL,state,48,&s)==AGXC_PROFILE);
    CHECK(agxc_state_decode(P,(enum agxc_channel)2,state,48,&s)==AGXC_ARGUMENT);
    CHECK(agxc_state_decode(P,AGXC_DEVCTRL,state,48,(struct agxc_ring_snapshot *)state)==AGXC_ARGUMENT);
    CHECK(agxc_ring_candidate(AGXC_NO_PROFILE,AGXC_DEVCTRL,s,1,&c)==AGXC_PROFILE);
    CHECK(agxc_ring_candidate(P,(enum agxc_channel)2,s,1,&c)==AGXC_ARGUMENT);
    CHECK(agxc_ring_candidate(P,AGXC_DEVCTRL,s,2,&c)==AGXC_ARGUMENT);
    CHECK(agxc_ring_candidate(P,AGXC_DEVCTRL,s,1,NULL)==AGXC_ARGUMENT);
}
int main(void) { test_profiles_dc(); test_fw_patterns(); test_ranges(); test_ring(); printf("PASS %u groups %u checks\n",groups,checks); return 0; }
