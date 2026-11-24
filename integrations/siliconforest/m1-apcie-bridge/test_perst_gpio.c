/* Host fake GPIO file only; never a mapped/device address. */
#include "perst_gpio.h"
#include "linkup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "test_perst_gpio requires assertions"
#endif

struct fake_gpio {
    uint32_t pins[212];
    unsigned reads, writes, fail_read, fail_write, apply_before_failure;
    uint32_t readback_xor, last_offset, last_value;
};

static int fake_read(void *context, uint32_t offset, uint32_t *value)
{
    struct fake_gpio *f = context;
    assert(offset == 152u * 4u && offset < sizeof f->pins);
    f->reads++;
    if (f->fail_read == f->reads) { *value = 0xdeadc0de; return -1; }
    *value = f->pins[offset / 4];
    if (f->reads == 2) *value ^= f->readback_xor;
    return 0;
}

static int fake_write(void *context, uint32_t offset, uint32_t value)
{
    struct fake_gpio *f = context;
    assert(offset == 152u * 4u && offset < sizeof f->pins);
    f->writes++; f->last_offset = offset; f->last_value = value;
    if (f->fail_write == f->writes) {
        if (f->apply_before_failure) f->pins[offset / 4] = value;
        return -1;
    }
    f->pins[offset / 4] = value;
    return 0;
}

static void reset(struct fake_gpio *f)
{
    memset(f, 0, sizeof *f);
    for (unsigned i = 0; i < 212; ++i) f->pins[i] = 0x65000000u | (i << 10);
    /* Fixed synthetic output setup. Pull/group/drive/input/reserved bits are
     * sentinels, not an observed or recommended hardware configuration. */
    f->pins[152] = 0x65078683u;
    assert(!(f->pins[152] & ((1u << 21) | (3u << 5))));
    assert((f->pins[152] & (7u << 1)) == (1u << 1));
}

static void invalid_arguments(void)
{
    struct fake_gpio f;
    struct apg_snapshot s;
    struct apg_io io = {&f, fake_read, fake_write};
    reset(&f);
    assert(apg_set_perst(&io, 1, NULL) == APG_ARGUMENT);
    assert(apg_set_perst(&io, -1, &s) == APG_ARGUMENT && s.result == APG_ARGUMENT);
    assert(apg_set_perst(&io, 2, &s) == APG_ARGUMENT && s.result == APG_ARGUMENT);
    assert(f.reads == 0 && f.writes == 0 && s.dirty == 0);
}

#ifdef APG_FAKE_GPIO_FILE
static void assert_release_and_sibling_preservation(void)
{
    struct fake_gpio f;
    struct apg_snapshot s;
    struct apg_io io = {&f, fake_read, fake_write};
    uint32_t saved[212];
    reset(&f); memcpy(saved, f.pins, sizeof saved);
    assert(apg_fake_drive_compiled() == 1);
    assert(apg_set_perst(&io, 1, &s) == APG_OK);
    assert(s.before == saved[152] && s.after == (saved[152] & ~1u));
    assert(s.before_valid == 1 && s.after_valid == 1 && s.dirty == 1);
    assert(s.reads_attempted == 2 && s.writes_attempted == 1 && s.gated == 0);
    assert(f.reads == 2 && f.writes == 1 && f.last_offset == 0x260);
    for (unsigned i = 0; i < 212; ++i) assert(f.pins[i] == (saved[i] & ~(i == 152 ? 1u : 0u)));
    assert(apg_set_perst(&io, 0, &s) == APG_OK);
    assert(s.asserted == 0 && f.pins[152] == saved[152]);
    assert(f.reads == 4 && f.writes == 2);
    assert(!memcmp(f.pins, saved, sizeof saved));
}

static void locked_and_not_output(void)
{
    struct fake_gpio f;
    struct apg_snapshot s;
    struct apg_io io = {&f, fake_read, fake_write};
    reset(&f); f.pins[152] |= 1u << 21;
    assert(apg_set_perst(&io, 1, &s) == APG_LOCKED);
    assert(f.reads == 1 && f.writes == 0 && s.before_valid && !s.after_valid && !s.dirty);
    for (unsigned mode = 0; mode < 8; ++mode) {
        for (unsigned periph = 0; periph < 4; ++periph) {
            if (mode == 1 && periph == 0) continue;
            reset(&f);
            f.pins[152] = (f.pins[152] & ~((7u << 1) | (3u << 5))) | (mode << 1) | (periph << 5);
            uint32_t before = f.pins[152];
            assert(apg_set_perst(&io, 0, &s) == APG_NOT_READY);
            assert(f.reads == 1 && f.writes == 0 && !s.dirty && f.pins[152] == before);
        }
    }
}

static void partial_failures(void)
{
    struct fake_gpio f;
    struct apg_snapshot s;
    struct apg_io io = {&f, fake_read, fake_write};
    assert(apg_set_perst(NULL, 1, &s) == APG_ARGUMENT);
    struct apg_io bad = io; bad.read32 = NULL;
    assert(apg_set_perst(&bad, 1, &s) == APG_ARGUMENT);
    bad = io; bad.write32 = NULL;
    assert(apg_set_perst(&bad, 1, &s) == APG_ARGUMENT);
    reset(&f); f.fail_read = 1;
    assert(apg_set_perst(&io, 1, &s) == APG_IO);
    assert(s.reads_attempted == 1 && !s.before_valid && s.before == 0 && !s.dirty && f.writes == 0);
    for (unsigned applied = 0; applied < 2; ++applied) {
        reset(&f); f.fail_write = 1; f.apply_before_failure = applied;
        uint32_t before = f.pins[152];
        assert(apg_set_perst(&io, 1, &s) == APG_IO);
        assert(s.dirty == 1 && s.writes_attempted == 1 && s.before_valid && !s.after_valid);
        assert(f.reads == 1 && f.writes == 1 && f.pins[152] == (applied ? before & ~1u : before));
    }
    reset(&f); f.fail_read = 2;
    assert(apg_set_perst(&io, 1, &s) == APG_IO);
    assert(s.dirty == 1 && s.reads_attempted == 2 && !s.after_valid && s.after == 0);
    for (unsigned bit = 0; bit < 32; ++bit) {
        if (!((1u << bit) & ((1u << 21) | (3u << 5) | (7u << 1) | 1u))) continue;
        reset(&f); f.readback_xor = 1u << bit;
        assert(apg_set_perst(&io, 1, &s) == APG_VERIFY);
        assert(s.after_valid && s.dirty && s.result == APG_VERIFY && f.writes == 1);
    }
}
#else
static void gated_never_calls_anything(void)
{
    struct fake_gpio f;
    struct apg_snapshot s;
    struct apg_io io = {&f, fake_read, fake_write};
    uint32_t saved[212];
    reset(&f); memcpy(saved, f.pins, sizeof saved);
    assert(apg_fake_drive_compiled() == 0);
    for (int asserted = 0; asserted < 2; ++asserted) {
        assert(apg_set_perst(&io, asserted, &s) == APG_GATED);
        assert(s.result == APG_GATED && s.gated == 1 && s.asserted == (uint32_t)asserted);
        assert(s.reads_attempted == 0 && s.writes_attempted == 0 && s.dirty == 0);
        assert(!s.before_valid && !s.after_valid);
    }
    assert(apg_set_perst(NULL, 1, &s) == APG_GATED);
    assert(f.reads == 0 && f.writes == 0 && !memcmp(saved, f.pins, sizeof saved));
}
#endif

int main(void)
{
    assert(apb_shared_writes_compiled() == 0 && apl_shared_writes_compiled() == 0);
    invalid_arguments();
#ifdef APG_FAKE_GPIO_FILE
    assert_release_and_sibling_preservation();
    locked_and_not_output();
    partial_failures();
    puts("HOST ONLY PASS: GPIO152 fake assert/release, sibling preservation, lock/mux/mode refusal, readback and partial failures");
#else
    gated_never_calls_anything();
    puts("HOST ONLY PASS: default/KERNEL PERST gate, zero GPIO callbacks");
#endif
    return 0;
}
