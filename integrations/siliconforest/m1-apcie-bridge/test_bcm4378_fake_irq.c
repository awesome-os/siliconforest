#include "bcm4378_fake_irq.h"
#include "linkup.h"
#include "perst_gpio.h"
#include "radio_enable.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "test_bcm4378_fake_irq requires assertions"
#endif

static const struct bmi_profile PROFILE = {UINT64_C(0xfffff000), 0x100, 0, 8, 704};
static struct bmi_source S, SAVED;
static struct { uint32_t phy, run, link, pci; unsigned reads, writes, fail_at, reset_after_read; } F;
static int fake_read(void *context, unsigned r, uint32_t off, uint32_t *out)
{
    (void)context; F.reads++;
    if (F.reads == F.fail_at) { *out = UINT32_MAX; return -1; }
    if (r == APB_PHY && off == 0) *out = F.phy;
    else if (r == APB_PORT0 && off == 0x804) *out = F.run;
    else if (r == APB_PORT0 && off == 0x208) *out = F.link;
    else { assert(r == 9 && off == 0); *out = F.pci; }
    if (F.reads == F.reset_after_read) F.phy |= 0x80;
    return 0;
}
static int fake_write(void *context, unsigned r, uint32_t off, uint32_t value)
{
    (void)context;(void)r;(void)off;(void)value;
    F.writes++;
    assert(!"fake interrupt source must never write hardware registers");
    return -1;
}
static void reset(void)
{
    memset(&F, 0, sizeof F); memset(&S, 0, sizeof S);
    F.phy = 0xf; F.run = F.link = 1; F.pci = 0x442514e4;
}
static void save(void) { memcpy(&SAVED, &S, sizeof S); }
static void unchanged(void) { assert(!memcmp(&SAVED, &S, sizeof S) && F.writes == 0); }

#ifdef BMI_FAKE_DEVICE
#include "register_accessor.h"
static struct apr_accessor A;
static struct apb_io IO;
static void bind(void)
{
    struct apb_io raw = {NULL, fake_read, fake_write, NULL, NULL, NULL};
    assert(apr_bind_bcm_fake(&A, &raw, &IO) == APB_OK);
}
static void prepare(unsigned operation)
{
    reset(); bind();
    if (operation) assert(bmi_open(&S, &IO, &PROFILE) == BMI_FAKE_OK);
    if (operation >= 2) assert(bmi_arm(&S, 0x85) == BMI_FAKE_OK);
    if (operation == 4) {
        assert(bmi_set_mask(&S, 0, 0) == BMI_FAKE_OK);
        assert(bmi_receive(&S, PROFILE.address, 0) == BMI_FAKE_OK);
    }
}
static int operation(unsigned op, uint32_t *v, uint32_t *irq)
{
    switch (op) {
    case 0: return bmi_open(&S, &IO, &PROFILE);
    case 1: return bmi_arm(&S, 0x85);
    case 2: return bmi_set_mask(&S, 0, 0);
    case 3: return bmi_receive(&S, PROFILE.address, 0);
    case 4: return bmi_dispatch_one(&S, v, irq);
    default: return bmi_close(&S);
    }
}
static void source_lifecycle(void)
{
    uint32_t v = 0xdead, irq = 0xbeef;
    prepare(2);
    assert(S.allocated == 0x85 && S.masked == 0x85 && S.pending == 0);
    assert(bmi_receive(&S, PROFILE.address, 7) == BMI_FAKE_OK);
    assert(bmi_receive(&S, PROFILE.address, 7) == BMI_FAKE_OK);
    assert(bmi_receive(&S, PROFILE.address, 0) == BMI_FAKE_OK);
    assert(bmi_receive(&S, PROFILE.address, 2) == BMI_FAKE_OK);
    assert(S.received == 4 && S.pending == 0x85); /* coalesced, not duplicated */
    save(); assert(bmi_dispatch_one(&S, &v, &irq) == BMI_EMPTY && v == 0xdead && irq == 0xbeef); unchanged();
    assert(bmi_set_mask(&S, 7, 0) == BMI_FAKE_OK);
    assert(bmi_set_mask(&S, 0, 0) == BMI_FAKE_OK);
    assert(bmi_dispatch_one(&S, &v, &irq) == BMI_FAKE_OK && v == 0 && irq == 704);
    assert(S.pending == 0x84 && S.delivered == 1);
    assert(bmi_dispatch_one(&S, &v, &irq) == BMI_FAKE_OK && v == 7 && irq == 711);
    assert(S.pending == 4 && S.delivered == 2);
    save(); assert(bmi_close(&S) == BMI_BUSY); unchanged();
    assert(bmi_set_mask(&S, 2, 0) == BMI_FAKE_OK);
    assert(bmi_dispatch_one(&S, &v, &irq) == BMI_FAKE_OK && v == 2 && irq == 706);
    save(); assert(bmi_close(&S) == BMI_BUSY); unchanged(); /* unmasked even though empty */
    for (unsigned n = 0; n < 8; ++n) if (S.allocated & (1u << n)) assert(bmi_set_mask(&S, n, 1) == BMI_FAKE_OK);
    assert(bmi_close(&S) == BMI_FAKE_OK && S.closed && !S.armed && !S.pending);
    save(); assert(bmi_open(&S, &IO, &PROFILE) == BMI_STATE); unchanged();
    assert(bmi_receive(&S, PROFILE.address, 0) == BMI_STATE && F.writes == 0);
}
static void all_unsafe_states_refuse_without_changes(void)
{
    for (unsigned op = 0; op < 6; ++op) {
        for (unsigned bad = 0; bad < 7; ++bad) {
            prepare(op); save();
            if (bad == 0) F.phy = 0x8f;
            else if (bad == 1) F.phy = 0x80;
            else if (bad == 2) F.link = 0;
            else if (bad == 3) F.run = 0;
            else if (bad == 4) F.link = 5;
            else if (bad == 5) F.link = 0x41;
            else F.phy = 0xb;
            unsigned reads = F.reads; uint32_t v = 0xdead, irq = 0xbeef;
            assert(operation(op, &v, &irq) == (bad == 0 || bad == 1 || bad == 6 ? BMI_PHY_NOT_READY : BMI_LINK_DOWN));
            assert(v == 0xdead && irq == 0xbeef);
            if (bad == 0 || bad == 1 || bad == 6) assert(F.reads == reads + 1); /* no port read while PHY refused */
            unchanged();
        }
        prepare(op); save(); F.reset_after_read = F.reads + 2;
        uint32_t v = 0xdead, irq = 0xbeef;
        assert(operation(op, &v, &irq) == BMI_PHY_NOT_READY);
        assert(v == 0xdead && irq == 0xbeef); unchanged();
    }
}
static void every_read_error_and_owner_rejection(void)
{
    for (unsigned op = 0; op < 6; ++op) {
        for (unsigned n = 1; n <= 5; ++n) {
            prepare(op); save(); F.fail_at = F.reads + n;
            uint32_t v = 0xdead, irq = 0xbeef;
            assert(operation(op, &v, &irq) == BMI_IO && v == 0xdead && irq == 0xbeef);
            unchanged();
        }
        prepare(op); save(); F.pci = 0x5f6914e4;
        uint32_t v = 0xdead, irq = 0xbeef;
        assert(operation(op, &v, &irq) == BMI_OWNER); unchanged();
    }
}
static void invalid_vectors_and_state(void)
{
    uint32_t v = 0xdead, irq = 0xbeef;
    prepare(1); save();
    assert(bmi_arm(&S, 0) == BMI_OWNER); unchanged();
    assert(bmi_arm(&S, 0x100) == BMI_OWNER); unchanged();
    prepare(2); save();
    assert(bmi_arm(&S, 1) == BMI_STATE); unchanged();
    uint32_t bad[] = {1, 8, 31, 32, UINT32_MAX};
    for (unsigned n = 0; n < sizeof bad/sizeof bad[0]; ++n) {
        assert(bmi_set_mask(&S, bad[n], 0) == BMI_OWNER); unchanged();
        assert(bmi_receive(&S, PROFILE.address, bad[n]) == BMI_OWNER); unchanged();
    }
    assert(bmi_set_mask(&S, 0, 2) == BMI_ARGUMENT); unchanged();
    assert(bmi_receive(&S, UINT64_C(0x1fffff000), 0) == BMI_OWNER); unchanged();
    assert(bmi_receive(&S, UINT64_C(0xfee00000), 0) == BMI_OWNER); unchanged();
    assert(bmi_dispatch_one(&S, NULL, &irq) == BMI_ARGUMENT); unchanged();
    assert(bmi_dispatch_one(&S, &v, &v) == BMI_ARGUMENT); unchanged();
    S.received = UINT32_MAX; save();
    assert(bmi_receive(&S, PROFILE.address, 0) == BMI_LIMIT); unchanged();
    prepare(4); S.delivered = UINT32_MAX; save();
    assert(bmi_dispatch_one(&S, &v, &irq) == BMI_LIMIT); unchanged();
    prepare(2); S.commits = UINT32_MAX; save();
    assert(bmi_set_mask(&S, 0, 0) == BMI_LIMIT); unchanged();
    prepare(0); struct bmi_profile p = PROFILE; p.vector_count = 32; save();
    assert(bmi_open(&S, &IO, &p) == BMI_ARGUMENT); unchanged();
    p = PROFILE; p.vector_base = 8;
    assert(bmi_open(&S, &IO, &p) == BMI_ARGUMENT); unchanged();
    p = PROFILE; p.rid = 0x101;
    assert(bmi_open(&S, &IO, &p) == BMI_ARGUMENT); unchanged();
}
#else
static void default_zero_io(void)
{
    reset(); save();
    struct apb_io io = {NULL, fake_read, fake_write, NULL, NULL, NULL};
    uint32_t v = 0xdead, irq = 0xbeef;
    assert(!bmi_fake_source_compiled());
    assert(bmi_open(&S, &io, &PROFILE) == BMI_GATED);
    assert(bmi_arm(&S, 1) == BMI_GATED);
    assert(bmi_set_mask(&S, 0, 0) == BMI_GATED);
    assert(bmi_receive(&S, PROFILE.address, 0) == BMI_GATED);
    assert(bmi_dispatch_one(&S, &v, &irq) == BMI_GATED);
    assert(bmi_close(&S) == BMI_GATED);
    assert(v == 0xdead && irq == 0xbeef && F.reads == 0); unchanged();
}
#endif
int main(void)
{
    assert(!apb_shared_writes_compiled() && !apl_shared_writes_compiled());
    assert(!apg_fake_drive_compiled() && !ars_fake_enable_compiled());
#ifdef BMI_FAKE_DEVICE
    assert(bmi_fake_source_compiled());
    source_lifecycle(); all_unsafe_states_refuse_without_changes(); every_read_error_and_owner_rejection(); invalid_vectors_and_state();
#else
    default_zero_io();
#endif
    puts("HOST ONLY PASS: fake interrupt source, bounded vector routing and acknowledgement, reset/down zero changes");
    return 0;
}
