#ifdef NDEBUG
#error "NDEBUG forbidden: preflight tests require assertions"
#endif
#include "preflight.h"
#include <assert.h>
#include <stdio.h>
static unsigned calls;
static int fail_read;
static int read32(void *ctx,uint32_t off,uint32_t *out)
{
    assert(ctx==&calls && off==0x3f8);calls++;*out=0xff;
    return fail_read;
}
int main(void)
{
    struct gpf_board b={UINT64_C(0x23b700000),0x4000,0x3f8};
    struct gpf_snapshot s;
    assert(gpf_observe(NULL,read32,&calls,&s)==GPF_BOARD_REFUSED && !calls);
    for (unsigned k=0;k<3;k++) {
        struct gpf_board wrong=b;
        if (k==0) wrong.pmgr_base++;
        if (k==1) wrong.pmgr_size--;
        if (k==2) wrong.gfx_offset+=4;
        assert(gpf_observe(&wrong,read32,&calls,&s)==GPF_BOARD_REFUSED && !calls);
    }
    assert(gpf_observe(&b,NULL,&calls,&s)==GPF_BOARD_REFUSED && !calls);
    assert(gpf_observe(&b,read32,&calls,&s)==GPF_OBSERVED && calls==1);
    assert(s.gfx_power==0xff && s.read_completed==1 && !s.gpu_mmio_authorized && !s.gpu_mmio_reads);
    fail_read=1;assert(gpf_observe(&b,read32,&calls,&s)==GPF_READ_FAILED && calls==2);
    assert(!s.gfx_power && !s.read_completed && !s.gpu_mmio_authorized);
    puts("HOST ONLY PASS: exact PMGR read, no reads on bad board, fail-closed output");
    return 0;
}
