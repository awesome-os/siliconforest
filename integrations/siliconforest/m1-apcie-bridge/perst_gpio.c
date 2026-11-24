/* Original implementation. Only the explicitly selected host fake path can
 * call GPIO operations; the default path is an honest no-I/O refusal. */
#include "perst_gpio.h"

#if defined(KERNEL) && defined(APG_FAKE_GPIO_FILE)
#error "APG_FAKE_GPIO_FILE is host-test only; no kernel GPIO drive path"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "PERST slice requires shared PHY and PMGR writes compiled out"
#endif

int apg_fake_drive_compiled(void)
{
#ifdef APG_FAKE_GPIO_FILE
    return 1;
#else
    return 0;
#endif
}

int apg_set_perst(const struct apg_io *io, int asserted, struct apg_snapshot *s)
{
    if (!s) return APG_ARGUMENT;
    *s = (struct apg_snapshot){0};
    s->version = 1;
    s->result = APG_ARGUMENT;
    if (asserted != 0 && asserted != 1) return APG_ARGUMENT;
    s->asserted = (uint32_t)asserted;
#ifdef APG_FAKE_GPIO_FILE
    {
        const uint32_t lock = 1u << 21, periph = 3u << 5, mode = 7u << 1;
        const uint32_t data = 1u, output_mode = 1u << 1;
        uint32_t before = 0, after = 0, wanted;
        if (!io || !io->read32 || !io->write32) return APG_ARGUMENT;
        s->reads_attempted++;
        if (io->read32(io->context, APG_PERST_OFFSET, &before)) {
            s->result = APG_IO; return APG_IO;
        }
        s->before = before; s->before_valid = 1;
        if (before & lock) { s->result = APG_LOCKED; return APG_LOCKED; }
        if ((before & periph) || (before & mode) != output_mode) {
            s->result = APG_NOT_READY; return APG_NOT_READY;
        }
        wanted = (before & ~data) | (asserted ? 0u : data);
        s->dirty = 1;
        s->writes_attempted++;
        if (io->write32(io->context, APG_PERST_OFFSET, wanted)) {
            s->result = APG_IO; return APG_IO;
        }
        s->reads_attempted++;
        if (io->read32(io->context, APG_PERST_OFFSET, &after)) {
            s->result = APG_IO; return APG_IO;
        }
        s->after = after; s->after_valid = 1;
        if ((after ^ wanted) & (data | lock | periph | mode)) {
            s->result = APG_VERIFY; return APG_VERIFY;
        }
        s->result = APG_OK;
        return APG_OK;
    }
#else
    (void)io;
    s->gated = 1;
    s->result = APG_GATED;
    return APG_GATED;
#endif
}
