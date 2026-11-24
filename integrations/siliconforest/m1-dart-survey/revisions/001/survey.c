/* Original strictly read-only sampler; all sampled offsets have source evidence. */
#include "survey.h"
#include <string.h>
static const uint32_t ps[4]={0x1b8,0x348,0x3e8,0x1a0};
static const unsigned cr[5]={DLS_PHY,DLS_RC,DLS_PORT,DLS_PORT,DLS_PORT};
static const uint32_t co[5]={0,0x24,0x800,0x814,0x804};

int dls_read_allowed(unsigned region,uint32_t off)
{
    if (off&3) return 0;
    if (region==DLS_PMGR) {
        for (unsigned i=0;i<4;i++) if (off==ps[i]) return 1;
        return 0;
    }
    for (unsigned i=0;i<5;i++) if (region==cr[i]&&off==co[i]) return 1;
    if (region!=DLS_DART) return 0;
    return off==0||off==4||off==0x60||off==0xfc||
        (off>=0x80&&off<=0x8c)||(off>=0x100&&off<=0x13c)||(off>=0x200&&off<=0x2fc);
}
static int read_one(const struct dls_io *io,struct dls_snapshot *s,unsigned r,uint32_t off,uint32_t *value)
{
    s->failure_region=r;s->failure_offset=off;
    if (!dls_read_allowed(r,off)) return DLS_ARGUMENT;
    if (s->reads_attempted>=DLS_MAX_READS) return DLS_LIMIT;
    s->reads_attempted++;
    return io->read32(io->context,r,off,value)?DLS_IO:DLS_SAMPLED;
}
static int guard(const struct dls_io *io,struct dls_snapshot *s)
{
    int e;
    for (unsigned i=0;i<4;i++) {
        e=read_one(io,s,DLS_PMGR,ps[i],&s->power[i]);if (e) return e;
        /* Reject reset, AUTO, PS-reset, BUSY/parent-off and device-disable.
         * Historical WAS-gated bits8/9 are observations, not current state.
         */
        if ((s->power[i]&UINT32_C(0x90001c00))||(s->power[i]&0xff)!=0xff) return DLS_NOT_READY;
    }
    for (unsigned i=0;i<5;i++) {
        e=read_one(io,s,cr[i],co[i],&s->clocks[i]);if (e) return e;
        if (i==0) {
            if ((s->clocks[i]&0x8f)!=0x0f) return DLS_NOT_READY;
        } else if (!(s->clocks[i]&1)) return DLS_NOT_READY;
    }
    return DLS_SAMPLED;
}
static int sample_one(const struct dls_io *io,struct dls_snapshot *s,uint32_t off)
{
    uint32_t value;int e=guard(io,s);if (e) return e;
    if (s->sample_count>=DLS_SAMPLES) return DLS_LIMIT;
    e=read_one(io,s,DLS_DART,off,&value);if (e) return e;
    s->samples[s->sample_count++]=(struct dls_sample){off,value};
    return DLS_SAMPLED;
}
int dls_sample(const struct dls_board *b,const struct dls_io *io,struct dls_snapshot *s)
{
    static const uint64_t base[DLS_REGIONS]={UINT64_C(0x23b700000),UINT64_C(0x680000000),UINT64_C(0x680080000),UINT64_C(0x681000000),UINT64_C(0x681008000)};
    static const uint32_t size[DLS_REGIONS]={0x4000,0x4000,0x4000,0x4000,0x4000};
    int e=DLS_ARGUMENT;
    if (!s) return e;
    memset(s,0,sizeof(*s));s->version=1;s->failure_region=DLS_REGIONS;s->result=e;
    if (!b||!io||!io->read32) return e;
    for (unsigned i=0;i<DLS_REGIONS;i++) if (b->base[i]!=base[i]||b->size[i]!=size[i]) {
        s->result=DLS_BOARD;return DLS_BOARD;
    }
    e=sample_one(io,s,0);if (e) goto done;
    if (((s->samples[0].value>>24)&15)!=14) { e=DLS_UNSUPPORTED;goto done; }
    e=sample_one(io,s,4);if (e) goto done;
    e=sample_one(io,s,0x60);if (e) goto done;
    if (s->samples[2].value&(1u<<15)) { e=DLS_UNSUPPORTED;goto done; }
    e=sample_one(io,s,0xfc);if (e) goto done;
    if (s->samples[3].value&0xffff0000u) { e=DLS_UNSUPPORTED;goto done; }
    for (unsigned i=0;i<4;i++) { e=sample_one(io,s,0x80+4*i);if (e) goto done; }
    for (unsigned sid=0;sid<16;sid++) {
        e=sample_one(io,s,0x100+4*sid);if (e) goto done;
        for (unsigned root=0;root<4;root++) {
            e=sample_one(io,s,0x200+16*sid+4*root);if (e) goto done;
        }
    }
    e=guard(io,s); /* Do not label a sample complete after observed power loss. */
done:
    s->result=e;
    if (!e) { s->failure_region=DLS_REGIONS;s->failure_offset=0; }
    return e;
}
