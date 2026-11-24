/* Original implementation; exact command bytes from pinned MIT evidence.
 * The only writer is compiled exclusively for the host fake GPIO file. */
#include "radio_enable.h"

#if defined(KERNEL) && defined(ARS_FAKE_GPIO_FILE)
#error "ARS_FAKE_GPIO_FILE is host-test only; no kernel radio-enable path"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "radio slice requires shared PHY and PMGR writes compiled out"
#endif

int ars_fake_enable_compiled(void)
{
#ifdef ARS_FAKE_GPIO_FILE
    return 1;
#else
    return 0;
#endif
}

int ars_enable_radio(const struct ars_io *io, struct ars_snapshot *s)
{
    if (!s) return ARS_ARGUMENT;
    *s = (struct ars_snapshot){0};
    s->version = 1;
    s->result = ARS_ARGUMENT;
    s->pin = ARS_SMC_GPIO_PIN;
    s->requested_command = ARS_ENABLE_COMMAND;
#ifdef ARS_FAKE_GPIO_FILE
    {
        const char key[4] = {'g', 'P', '0', 'd'};
        const uint8_t value[4] = {1, 0, 0x80, 0};
        if (!io || !io->write_key) return ARS_ARGUMENT;
        s->dirty = 1;
        s->writes_attempted = 1;
        if (io->write_key(io->context, key, value, 4)) {
            s->result = ARS_IO;
            return ARS_IO;
        }
        s->command_accepted = 1;
        s->result = ARS_FAKE_ACCEPTED;
        return ARS_FAKE_ACCEPTED;
    }
#else
    (void)io;
    s->gated = 1;
    s->result = ARS_GATED;
    return ARS_GATED;
#endif
}
