/* Original bounded state machine, rewritten from protocol facts only. */
#include "bcm4378_transport.h"

#if defined(KERNEL) && defined(BCT_FAKE_BAR_FILE)
#error "BCT_FAKE_BAR_FILE is host-test only; no kernel transport/MMIO path"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "BCM transport requires shared PHY and PMGR writes compiled out"
#endif

#ifdef BCT_FAKE_BAR_FILE
#include "register_accessor.h"

static int finish(struct bct_session *s, int result)
{
    s->result = result;
    if (result == BCT_IO || result == BCT_VERIFY || (result && s->pending_writes)) s->faulted = 1;
    if (!result) s->pending_writes = 0;
    return result;
}

static int link_ready(struct bct_session *s)
{
    uint32_t status = 0, link = 0;
    s->reads_attempted++;
    if (s->io.read32(s->io.context, APB_PORT0, 0x804, &status)) return BCT_IO;
    if (!(status & 1u)) return BCT_LINK_DOWN;
    s->reads_attempted++;
    if (s->io.read32(s->io.context, APB_PORT0, 0x208, &link)) return BCT_IO;
    return (link & 0x45u) == 1u ? BCT_FAKE_OK : BCT_LINK_DOWN;
}

static int enter(struct bct_session *s, uint32_t phase)
{
    int e;
    if (!s || s->phase == BCT_EMPTY) return BCT_ARGUMENT;
    if (s->faulted) return finish(s, BCT_FAULTED);
    if ((e = link_ready(s))) return finish(s, e);
    return s->phase == phase ? BCT_FAKE_OK : finish(s, BCT_ORDER);
}

static int read_word(struct bct_session *s, unsigned space, uint32_t offset, uint32_t *value)
{
    int e = link_ready(s);
    if (e) return e;
    s->reads_attempted++;
    return s->io.read32(s->io.context, space, offset, value) ? BCT_IO : BCT_FAKE_OK;
}

static int write_word(struct bct_session *s, unsigned space, uint32_t offset, uint32_t value)
{
    int e = link_ready(s);
    if (e) return e;
    s->dirty = 1;
    s->writes_attempted++;
    s->pending_writes++;
    return s->io.write32(s->io.context, space, offset, value) ? BCT_IO : BCT_FAKE_OK;
}

static int ram_span(const struct bct_session *s, uint32_t address, uint32_t bytes)
{
    if (address < BCT_RAM_BASE || (address & 3u) || bytes > s->plan.ram_bytes) return 0;
    return address - BCT_RAM_BASE <= s->plan.ram_bytes - bytes;
}

static int overlaps(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{
    /* All spans are already checked within <=8MiB BAR2; sums cannot wrap. */
    return a < b + m && b < a + n;
}

int bct_fake_transport_compiled(void) { return 1; }

int bct_init(struct bct_session *s, const struct apb_io *io, const struct bct_plan *p)
{
    if (!s || !io || !p) return BCT_ARGUMENT;
    if (s->phase != BCT_EMPTY || s->faulted || s->dirty) return BCT_ORDER;
    if (!apr_bcm_io_bound(io)) return BCT_ARGUMENT;
    if (p->ram_bytes < 56 || p->ram_bytes > BCT_MAX_RAM || (p->ram_bytes & 3u) ||
        !p->window_page || (p->window_page & 0xfffu)) return BCT_RANGE;
    if (p->pcie_core_revision > 255) return BCT_UNSUPPORTED;
    *s = (struct bct_session){0};
    s->io = *io; s->plan = *p; s->phase = BCT_BOUND;
    return finish(s, BCT_FAKE_OK);
}

int bct_identify(struct bct_session *s)
{
    uint32_t word = 0;
    int e = enter(s, BCT_BOUND);
    if (e) return e;
    if ((e = read_word(s, APR_BCM_CONFIG, 0, &word))) return finish(s, e);
    s->pci_identity = word;
    /* Target identity comes from retained Linux inventory, not macro values
     * guessed from headers excluded from the three-file protocol map. */
    if (word != 0x442514e4u) return finish(s, BCT_IDENTITY);
    s->phase = BCT_IDENTIFIED;
    return finish(s, BCT_FAKE_OK);
}

int bct_select_window(struct bct_session *s)
{
    uint32_t word = 0;
    int e = enter(s, BCT_IDENTIFIED);
    if (e) return e;
    if ((e = write_word(s, APR_BCM_CONFIG, 0x80, s->plan.window_page))) return finish(s, e);
    if ((e = read_word(s, APR_BCM_CONFIG, 0x80, &word))) return finish(s, e);
    s->window_word = word;
    if (word != s->plan.window_page) return finish(s, BCT_VERIFY);
    if ((e = read_word(s, APR_BCM_BAR0, 0, &word))) return finish(s, e);
    s->chip_word = word; /* raw only: excluded chip-ID masks are not invented */
    if (word == UINT32_MAX) return finish(s, BCT_IDENTITY);
    s->phase = BCT_WINDOW;
    return finish(s, BCT_FAKE_OK);
}

int bct_discover_shared(struct bct_session *s)
{
    uint32_t address = 0, words[4] = {0}, tail, version;
    const uint32_t fields[4] = {0, 40, 44, 48};
    int e = enter(s, BCT_WINDOW);
    if (e) return e;
    tail = BCT_RAM_BASE + s->plan.ram_bytes - 4;
    if ((e = read_word(s, APR_BCM_BAR2, tail, &address))) return finish(s, e);
    if (!address || address == s->plan.initial_tail) return finish(s, BCT_WAIT_FIRMWARE);
    if (!ram_span(s, address, 52) || overlaps(address, 52, tail, 4)) return finish(s, BCT_RANGE);
    for (unsigned i = 0; i < 4; ++i)
        if ((e = read_word(s, APR_BCM_BAR2, address + fields[i], &words[i]))) return finish(s, e);
    version = words[0] & 255u;
    if (version < 5 || version > 7 || (version >= 6 && !(words[0] & 0x02000000u)))
        return finish(s, BCT_UNSUPPORTED);
    for (unsigned i = 1; i < 4; ++i) {
        uint32_t bytes = i == 3 ? 20u : 4u;
        if (!ram_span(s, words[i], bytes) || overlaps(words[i], bytes, address, 52) ||
            overlaps(words[i], bytes, tail, 4)) return finish(s, BCT_RANGE);
        for (unsigned j = 1; j < i; ++j)
            if (overlaps(words[i], bytes, words[j], j == 3 ? 20u : 4u)) return finish(s, BCT_RANGE);
    }
    s->shared_address = address; s->shared_flags = words[0];
    s->h2d_address = words[1]; s->d2h_address = words[2]; s->ring_info_address = words[3];
    s->phase = BCT_SHARED;
    return finish(s, BCT_FAKE_OK);
}

int bct_setup_mailbox(struct bct_session *s)
{
    uint32_t pending = 0, readback = 0;
    int e = enter(s, BCT_SHARED);
    if (e) return e;
    if ((e = read_word(s, APR_BCM_BAR2, s->h2d_address, &pending))) return finish(s, e);
    if (pending) return finish(s, BCT_BUSY);
    if ((e = read_word(s, APR_BCM_BAR2, s->d2h_address, &pending))) return finish(s, e);
    s->d2h_word = pending; /* do not acknowledge or destroy device-to-host data */
    s->mailbox_mask_offset = 0x2000u + (s->plan.pcie_core_revision >= 64 ? 0xc34u : 0x4cu);
    if ((e = write_word(s, APR_BCM_BAR0, s->mailbox_mask_offset, 0))) return finish(s, e);
    if ((e = read_word(s, APR_BCM_BAR0, s->mailbox_mask_offset, &readback))) return finish(s, e);
    if (readback) return finish(s, BCT_VERIFY);
    s->phase = BCT_MAILBOX;
    return finish(s, BCT_FAKE_OK);
}

int bct_post_mailbox(struct bct_session *s, uint32_t word)
{
    uint32_t pending = 0;
    int e = enter(s, BCT_MAILBOX);
    if (e) return e;
    if (!word) return finish(s, BCT_ARGUMENT);
    if ((e = read_word(s, APR_BCM_BAR2, s->h2d_address, &pending))) return finish(s, e);
    if (pending) return finish(s, BCT_BUSY);
    if ((e = write_word(s, APR_BCM_BAR2, s->h2d_address, word))) return finish(s, e);
    for (unsigned n = 0; n < (s->plan.pcie_core_revision <= 13 ? 2u : 1u); ++n)
        if ((e = write_word(s, APR_BCM_CONFIG, 0x98, 1))) { s->faulted = 1; return finish(s, e); }
    s->phase = BCT_POSTED;
    return finish(s, BCT_FAKE_OK);
}

#else
/* Deliberately no driver backend. Every default/kernel entry is a refusal,
 * never simulated chip/firmware success and never an I/O/GPIO/SMC operation. */
int bct_fake_transport_compiled(void) { return 0; }
static int gated(struct bct_session *s)
{
    if (!s) return BCT_ARGUMENT;
    s->result = BCT_GATED;
    return BCT_GATED;
}
int bct_init(struct bct_session *s, const struct apb_io *io, const struct bct_plan *p)
{ (void)io; (void)p; return gated(s); }
int bct_identify(struct bct_session *s) { return gated(s); }
int bct_select_window(struct bct_session *s) { return gated(s); }
int bct_discover_shared(struct bct_session *s) { return gated(s); }
int bct_setup_mailbox(struct bct_session *s) { return gated(s); }
int bct_post_mailbox(struct bct_session *s, uint32_t word) { (void)word; return gated(s); }
#endif
