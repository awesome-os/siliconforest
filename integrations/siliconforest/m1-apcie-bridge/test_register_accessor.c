/* Reuse the exact existing simulated register file, callbacks and plans.
 * The old test entry is compiled but our tests below select the new path. */
#ifdef NDEBUG
#error "test_register_accessor requires assertions"
#endif
#define main existing_linkup_test_main
#include "test_linkup.c"
#undef main
#include "register_accessor.h"

static int clobber32(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    (void)ctx; (void)r; (void)off;
    *out = 0;
    return -1;
}
static int clobber16(void *ctx, unsigned r, uint32_t off, uint16_t *out)
{
    (void)ctx; (void)r; (void)off;
    *out = 0;
    return -1;
}

static void boundaries_and_gates(void)
{
    struct apr_accessor a = {0};
    struct apb_io io;
    uint32_t value = 0x12345678;
    uint16_t half = 0x1234;
    reset_fake();
    assert(apr_bind_fake(&a, &IO, &io) == APB_OK);
    for (unsigned r = 0; r < APL_REGIONS; ++r) {
        unsigned before = F.reads, writes = F.write_calls;
        assert(io.read32(io.context, r, apl_region_size(r), &value) == APB_ARGUMENT);
        assert(io.read32(io.context, r, UINT32_MAX - 3u, &value) == APB_ARGUMENT);
        assert(io.read32(io.context, r, 1, &value) == APB_ARGUMENT);
        assert(io.write32(io.context, r, apl_region_size(r), 1) == APB_ARGUMENT);
        assert(io.write32(io.context, r, 1, 1) == APB_ARGUMENT);
        assert(F.reads == before && F.write_calls == writes && value == 0x12345678);
        if (apl_region_shared(r)) {
            assert(io.write32(io.context, r, 0, 1) == APB_GATED);
            assert(F.write_calls == writes);
        }
    }
    assert(io.read32(io.context, APL_REGIONS, 0, &value) == APB_ARGUMENT);
    assert(io.write32(io.context, UINT32_MAX, 0, 1) == APB_ARGUMENT);
    assert(io.read32(io.context, APB_PORT0, 0, NULL) == APB_ARGUMENT);
    assert(io.read32(NULL, APB_PORT0, 0, &value) == APB_ARGUMENT);
    assert(io.write32(NULL, APB_PORT0, 0, 1) == APB_ARGUMENT);
    unsigned before = F.reads;
    assert(io.read32(io.context, APB_FUSE, 0, &value) == APB_ARGUMENT);
    assert(F.reads == before);
    mem_fuse[0x84 / 4] = 0xabcdef01;
    assert(io.read32(io.context, APB_FUSE, 0x84, &value) == APB_OK && value == 0xabcdef01);
    assert(io.read16(io.context, APB_PORT0, 0xa0, &half) == APB_ARGUMENT);
    assert(io.read16(io.context, APB_ECAM0, 0xa2, &half) == APB_ARGUMENT);
    assert(io.read16(io.context, APB_ECAM0, 0xa0, NULL) == APB_ARGUMENT);
    assert(io.write16(io.context, APB_PMGR, 0xa0, 1) == APB_ARGUMENT);
    assert(io.write16(io.context, APB_ECAM0, 0xa2, 1) == APB_ARGUMENT);
    assert(io.write32(io.context, APB_ECAM0, 0xa0, 1) == APB_ARGUMENT);
    assert(half == 0x1234);

    for (unsigned r = APB_PORT0; r <= APB_ECAM0; ++r) {
        uint32_t last = apl_region_size(r) - 4;
        assert(io.write32(io.context, r, last, 0xdeadbeef) == APB_OK);
        assert(io.read32(io.context, r, last, &value) == APB_OK && value == 0xdeadbeef);
    }
    assert(io.write16(io.context, APB_ECAM0, 0xa0, 3) == APB_OK);
    assert(io.read16(io.context, APB_ECAM0, 0xa0, &half) == APB_OK && half == 3);
    assert(mem_ecam0[0xa0 / 4] == 0xabcd0003u);
    io.delay_us(io.context, 123);
    assert(F.delayed == 123);
}

static void failures_and_binding(void)
{
    struct apr_accessor a = {0}, original = a;
    struct apb_io io = IO, saved = io, bad = IO;
    uint32_t value = 0xfeedface;
    uint16_t half = 0xabcd;
    reset_fake();
    bad.read32 = NULL;
    assert(apr_bind_fake(&a, &bad, &io) == APB_ARGUMENT);
    assert(!memcmp(&a, &original, sizeof a) && !memcmp(&io, &saved, sizeof io));
    assert(apr_bind_fake(NULL, &IO, &io) == APB_ARGUMENT);
    assert(apr_bind_fake(&a, NULL, &io) == APB_ARGUMENT);
    assert(apr_bind_fake(&a, &IO, NULL) == APB_ARGUMENT);
    assert(apr_bind_fake(&a, &io, &io) == APB_ARGUMENT);
    bad = IO; bad.read32 = clobber32; bad.read16 = clobber16;
    assert(apr_bind_fake(&a, &bad, &io) == APB_OK);
    assert(io.read32(io.context, APB_PORT0, 0, &value) == APB_IO && value == 0xfeedface);
    assert(io.read16(io.context, APB_ECAM0, 0xa0, &half) == APB_IO && half == 0xabcd);
    assert(apr_bind_fake(&a, &io, &saved) == APB_ARGUMENT); /* recursive binding */
    assert(apr_bind_fake(&a, &a.fake, &saved) == APB_ARGUMENT);
    assert(apr_bind_fake(&a, &IO, &a.fake) == APB_ARGUMENT);
    bad = IO; bad.write32 = NULL; bad.write16 = NULL; bad.read16 = NULL; bad.delay_us = NULL;
    assert(apr_bind_fake(&a, &bad, &io) == APB_OK);
    assert(io.write32(io.context, APB_PORT0, 0, 1) == APB_IO);
    assert(io.write16(io.context, APB_ECAM0, 0xa0, 1) == APB_IO);
    assert(io.read16(io.context, APB_ECAM0, 0xa0, &half) == APB_IO && half == 0xabcd);
    io.delay_us(io.context, 1);
    assert(F.delayed == 0);
    assert(apr_bind_fake(&a, &IO, &io) == APB_OK);
    F.fail_write_at = 1;
    assert(io.write32(io.context, APB_PORT0, 0x800, 1) == APB_IO);
    assert(F.write_calls == 1 && F.nev == 0 && mem_port0[0x800 / 4] == 0);
    a.bound = 0;
    assert(io.read32(io.context, APB_PORT0, 0, &value) == APB_ARGUMENT);
}

static void existing_slices_use_accessor(void)
{
    struct apr_accessor a = {0};
    struct apb_io io;
    struct apb_plan bp = bplan();
    struct apl_plan lp = lplan();
    struct apb_record b = {0};
    struct apl_record l = {0};
    reset_fake();
    assert(apr_bind_fake(&a, &IO, &io) == APB_OK);
    assert(apb_shared_writes_compiled() == 0 && apl_shared_writes_compiled() == 0);
    assert(apb_bringup_port0(&bp, &io, &b) == APB_GATED);
    assert(b.step == APB_STEP_POWER && b.reads == 1 && F.reads == 1);
    assert(b.shared_writes == 0 && b.dirty == 0 && F.write_calls == 0);
    assert(apl_link_port0(&lp, &io, &PINS, &b, &l) == APL_STATE);
    assert(F.reads == 1 && F.write_calls == 0 && F.pin_calls == 0);
    /* A synthetic precondition exercises slice-3 reads via the accessor.
     * It is not a completed hardware bring-up or authorization to write. */
    memset(&b, 0, sizeof b);
    b.version = 1; b.result = APB_OK; b.steps_completed = 23;
    b.shared_writes = 100; b.dirty = 1;
    mem_port0[0x800 / 4] = 1;
    memset(&l, 0, sizeof l);
    assert(apl_link_port0(&lp, &io, &PINS, &b, &l) == APB_GATED);
    assert(l.step == APL_STEP_PERST_ASSERT && l.reads == 2 && F.reads == 3);
    assert(l.dirty == 0 && F.write_calls == 0 && F.pin_calls == 0);
}

int main(void)
{
    boundaries_and_gates();
    failures_and_binding();
    existing_slices_use_accessor();
    puts("HOST ONLY PASS: checked fake accessor, boundaries, failure outputs, slice gates, zero shared writes");
    return 0;
}
