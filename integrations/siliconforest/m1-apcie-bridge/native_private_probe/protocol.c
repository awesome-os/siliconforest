/* Original wire validation; integers are explicitly little endian. */
#include "protocol.h"
#include <string.h>
/* Frozen host-context labels: reader e892, NPP7d41, control9844,owner0ee,corec585.
 * These are not self-authenticating hashes of the running code. */
const uint8_t bcp_components[160]={
 0xe8,0x92,0xe8,0xe3,0x1f,0x0b,0xa8,0x62,0x6d,0x4e,0x8a,0x83,0x18,0x8b,0x6f,0xe9,0x46,0x21,0x63,0x39,0x4b,0x16,0x85,0xe5,0xb1,0x09,0x44,0x86,0xfc,0xae,0xa9,0x3c,
 0x7d,0x41,0xf3,0x94,0xd7,0x7e,0x20,0xbd,0x57,0x76,0x96,0x4c,0x5a,0x46,0x19,0x4b,0x12,0x0a,0x3d,0xf7,0x05,0xcc,0xb9,0x96,0x88,0x9c,0xb6,0xd8,0x52,0xa0,0x9c,0x72,
 0x98,0x44,0xeb,0x28,0xef,0xb1,0x19,0x00,0x97,0x36,0x9b,0x41,0x9b,0x90,0xae,0xee,0xbb,0x60,0x5f,0x4a,0xaf,0xa1,0x47,0xdb,0xd9,0x08,0xa7,0x84,0x1b,0x3d,0x2c,0x57,
 0x0e,0xe2,0x10,0x1c,0x9c,0xda,0x5c,0xf2,0x82,0xc3,0xdb,0x48,0xb7,0x64,0x0c,0xef,0x98,0x14,0x6c,0x3b,0x04,0x72,0x4e,0x3f,0xe5,0x91,0x38,0xaa,0xad,0x77,0x24,0x34,
 0xc5,0x85,0xfd,0x8b,0x80,0x47,0x99,0x75,0xa4,0x6a,0xd1,0x9c,0x83,0xb7,0x61,0x0a,0x53,0xf3,0xa4,0xa1,0xa4,0x20,0x24,0x1c,0xbe,0x00,0x22,0xa2,0xd1,0xfb,0x82,0xfb
};
uint32_t bcp_word(const struct bcp_record *r,unsigned at)
{ const uint8_t *p=r->bytes+at;return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
void bcp_put(struct bcp_record *r,unsigned at,uint32_t v)
{ for(unsigned i=0;i<4;i++)r->bytes[at+i]=(uint8_t)(v>>(i*8)); }
void bcp_init(struct bcp_record *r,int enabled)
{
    memset(r,0,sizeof *r);memcpy(r->bytes,"BCMPRV01",8);bcp_put(r,8,1);bcp_put(r,12,BCP_BYTES);
    bcp_put(r,BCP_RESULT_AT,enabled?BCP_PENDING:BCP_GATED);bcp_put(r,BCP_STAGE_AT,enabled?BCP_CLAIM:BCP_GATE);
    bcp_put(r,BCP_LABEL_CONTEXT_AT,1);bcp_put(r,BCP_SYNTHETIC_AT,1);bcp_put(r,BCP_REQUEST_PROFILE_AT,1);
    memcpy(r->bytes+BCP_COMPONENTS_AT,bcp_components,sizeof bcp_components);
    for(unsigned i=0;i<BCP_OPS;i++)bcp_put(r,BCP_STATUS_AT+4*i,BCP_SENTINEL);
}
static int zero(const uint8_t *p,size_t n)
{ for(size_t i=0;i<n;i++)if(p[i])return 0;return 1; }
int bcp_snapshot_ok(const struct bcp_record *r,unsigned at,int retired)
{
    /* Actual bno_inspect keeps historical owns_pair=3 after successful complete.
     * Identity is cleared only by successful RESET_REF; balanced/resets become3. */
    const uint32_t wanted[14]={retired?6u:3u,0,0,1,retired?1u:0u,0,retired?0u:3u,
                              3,retired?3u:0u,retired?3u:0u,0,0,0,0};
    for(unsigned i=0;i<14;i++)if(bcp_word(r,at+4*i)!=wanted[i])return 0;
    return 1;
}
int bcp_validate(const uint8_t *raw,size_t n,const uint8_t nonce[24])
{
    if(!raw || !nonce || n!=BCP_BYTES)return 1;
    const struct bcp_record *r=(const struct bcp_record *)(const void *)raw;
    if(memcmp(raw,"BCMPRV01",8) || bcp_word(r,8)!=1 || bcp_word(r,12)!=BCP_BYTES ||
       bcp_word(r,BCP_LABEL_CONTEXT_AT)!=1 || bcp_word(r,BCP_SYNTHETIC_AT)!=1 ||
       bcp_word(r,BCP_READBACK_AT) || bcp_word(r,BCP_EFFECT_AT) ||
       bcp_word(r,BCP_REQUEST_PROFILE_AT)!=1 || memcmp(raw+BCP_COMPONENTS_AT,bcp_components,160) ||
       !zero(raw+488,8) || !zero(raw+BCP_RESERVED_AT,BCP_BYTES-BCP_RESERVED_AT))return 1;
    uint32_t result=bcp_word(r,BCP_RESULT_AT),attempts=bcp_word(r,BCP_ATTEMPTS_AT);
    if(result==BCP_GATED) {
        if(bcp_word(r,BCP_STAGE_AT) || attempts || bcp_word(r,BCP_ERROR_AT) ||
           !zero(raw+BCP_IDENTITY_AT,216) || !zero(raw+BCP_CONFIG_AT,28) ||
           !zero(raw+BCP_PRE_AT,112))return 1;
        for(unsigned i=0;i<BCP_OPS;i++)if(bcp_word(r,BCP_STATUS_AT+4*i)!=BCP_SENTINEL)return 1;
        return 77; /* outer nonce is caller context, record_nonce_bound=false */
    }
    if(result!=BCP_SELF_REPORT || bcp_word(r,BCP_STAGE_AT)!=BCP_DONE || attempts!=255 ||
       bcp_word(r,BCP_ERROR_AT) || zero(nonce,24) || memcmp(raw+BCP_IDENTITY_AT,nonce,24) ||
       bcp_word(r,BCP_COPY_BYTES_AT)!=16)return 1;
    for(unsigned i=0;i<6;i++)if(zero(raw+BCP_IDENTITY_AT+24+32*i,32))return 1;
    for(unsigned i=0;i<BCP_OPS;i++)if(bcp_word(r,BCP_STATUS_AT+4*i))return 1;
    uint32_t ram=bcp_word(r,BCP_CONFIG_AT+8),reserved=bcp_word(r,BCP_CONFIG_AT+12);
    uint32_t pages=bcp_word(r,BCP_CONFIG_AT+16),queries=bcp_word(r,BCP_CONFIG_AT+20);
    if(bcp_word(r,BCP_CONFIG_AT)!=1 || bcp_word(r,BCP_CONFIG_AT+4)!=1 ||
       !ram || ram>8 || !reserved || reserved>8 || !pages || pages>8 || queries!=pages*2 ||
       !bcp_snapshot_ok(r,BCP_PRE_AT,0) || !bcp_snapshot_ok(r,BCP_POST_AT,1))return 1;
    return 0;
}
