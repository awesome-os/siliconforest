#include "preflight.h"
#include <string.h>
int gpf_observe(const struct gpf_board *b,
                int (*read32)(void *, uint32_t, uint32_t *), void *ctx,
                struct gpf_snapshot *s)
{
    uint32_t value=0;
    if (!s) return GPF_BOARD_REFUSED;
    memset(s,0,sizeof(*s));s->version=1;s->byte_size=sizeof(*s);
    s->result=GPF_BOARD_REFUSED;
    if (!b || !read32 || b->pmgr_base!=UINT64_C(0x23b700000) ||
        b->pmgr_size!=0x4000 || b->gfx_offset!=0x3f8) return GPF_BOARD_REFUSED;
    s->board_valid=1;s->clock_ids[0]=314;s->clock_ids[1]=315;s->clock_ids[2]=316;
    s->virtual_mask=7;s->physical_domain=99;s->pmgr_offset=b->gfx_offset;
    s->reads_attempted=1;s->result=GPF_READ_FAILED;
    if (read32(ctx,b->gfx_offset,&value)) return GPF_READ_FAILED;
    s->read_completed=1;s->gfx_power=value;s->result=GPF_OBSERVED;
    /* OBSERVED means exactly one PMGR read. Even an active value does not
     * authorize GPU/ASC/UAT reads or infer their clocks, resets or ownership. */
    return GPF_OBSERVED;
}
