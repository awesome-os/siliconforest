/* Host fake SMC GPIO file only, not main AP GPIO MMIO. */
#include "radio_enable.h"
#include "linkup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "test_radio_enable requires assertions"
#endif

struct fake_smc_gpio {
    uint8_t pins[16][4];
    unsigned calls, fail, apply_before_failure;
};

static int fake_write_key(void *context, const char key[4], const uint8_t *value, uint32_t length)
{
    struct fake_smc_gpio *f = context;
    static const uint8_t expected[4] = {1, 0, 0x80, 0};
    assert(!memcmp(key, "gP0d", 4));
    assert(length == 4 && !memcmp(value, expected, 4));
    f->calls++;
    if (f->fail) {
        if (f->apply_before_failure) memcpy(f->pins[13], value, 4);
        return -1;
    }
    memcpy(f->pins[13], value, 4);
    return 0;
}

static void reset(struct fake_smc_gpio *f)
{
    memset(f, 0, sizeof *f);
    for (unsigned pin = 0; pin < 16; ++pin)
        for (unsigned byte = 0; byte < 4; ++byte) f->pins[pin][byte] = (uint8_t)(pin * 7u + byte);
}

static void siblings_unchanged(const struct fake_smc_gpio *f, const uint8_t *before)
{
    for (unsigned pin = 0; pin < 16; ++pin)
        if (pin != 13) assert(!memcmp(f->pins[pin], before + pin * 4u, 4));
}

static void invalid_arguments(void)
{
    struct fake_smc_gpio f;
    struct ars_io io = {&f, fake_write_key};
    reset(&f);
    assert(ars_enable_radio(&io, NULL) == ARS_ARGUMENT && f.calls == 0);
}

#ifdef ARS_FAKE_GPIO_FILE
static void exact_command_and_unknown_state(void)
{
    struct fake_smc_gpio f;
    struct ars_snapshot s;
    struct ars_io io = {&f, fake_write_key};
    uint8_t saved[16][4];
    reset(&f); memcpy(saved, f.pins, sizeof saved);
    assert(ars_fake_enable_compiled() == 1);
    assert(ars_enable_radio(&io, &s) == ARS_FAKE_ACCEPTED);
    assert(f.calls == 1 && s.version == 1 && s.result == ARS_FAKE_ACCEPTED);
    assert(s.pin == 13 && s.requested_command == 0x00800001);
    assert(s.writes_attempted == 1 && s.dirty == 1 && s.command_accepted == 1 && s.gated == 0);
    assert(s.state_known == 0);
    assert(!memcmp(f.pins[13], "\x01\x00\x80\x00", 4));
    siblings_unchanged(&f, saved[0]);
    /* Even a fake file already containing the command is not assumed live-on:
     * the next explicit call still performs exactly one enable, never a toggle. */
    assert(ars_enable_radio(&io, &s) == ARS_FAKE_ACCEPTED);
    assert(f.calls == 2 && s.state_known == 0);
    siblings_unchanged(&f, saved[0]);
}

static void partial_failures(void)
{
    struct fake_smc_gpio f;
    struct ars_snapshot s;
    struct ars_io io = {&f, fake_write_key}, missing = {&f, NULL};
    uint8_t saved[16][4];
    reset(&f);
    assert(ars_enable_radio(NULL, &s) == ARS_ARGUMENT && s.result == ARS_ARGUMENT);
    assert(ars_enable_radio(&missing, &s) == ARS_ARGUMENT && f.calls == 0 && s.dirty == 0);
    for (unsigned applied = 0; applied < 2; ++applied) {
        reset(&f); memcpy(saved, f.pins, sizeof saved);
        f.fail = 1; f.apply_before_failure = applied;
        assert(ars_enable_radio(&io, &s) == ARS_IO && s.result == ARS_IO);
        assert(f.calls == 1 && s.writes_attempted == 1 && s.dirty == 1);
        assert(s.command_accepted == 0 && s.state_known == 0 && s.gated == 0);
        assert(!memcmp(f.pins[13], applied ? (const uint8_t *)"\x01\x00\x80\x00" : saved[13], 4));
        siblings_unchanged(&f, saved[0]);
    }
}
#else
static void gated_no_callback(void)
{
    struct fake_smc_gpio f;
    struct ars_snapshot s;
    struct ars_io io = {&f, fake_write_key};
    uint8_t saved[16][4];
    reset(&f); memcpy(saved, f.pins, sizeof saved);
    assert(ars_fake_enable_compiled() == 0);
    assert(ars_enable_radio(&io, &s) == ARS_GATED && s.result == ARS_GATED);
    assert(s.gated == 1 && s.state_known == 0 && s.dirty == 0 && s.writes_attempted == 0 && s.command_accepted == 0);
    assert(ars_enable_radio(NULL, &s) == ARS_GATED);
    assert(f.calls == 0 && !memcmp(saved, f.pins, sizeof saved));
    siblings_unchanged(&f, saved[0]);
}
#endif

int main(void)
{
    assert(apb_shared_writes_compiled() == 0 && apl_shared_writes_compiled() == 0);
    invalid_arguments();
#ifdef ARS_FAKE_GPIO_FILE
    exact_command_and_unknown_state();
    partial_failures();
    puts("HOST ONLY PASS: exact gP0d enable, fake GPIO13, unknown radio state, sibling preservation and partial errors");
#else
    gated_no_callback();
    puts("HOST ONLY PASS: default/KERNEL radio gate, zero callbacks, radio state unknown");
#endif
    return 0;
}
