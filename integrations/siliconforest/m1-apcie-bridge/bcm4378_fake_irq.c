/* Original software event policy. MIT readiness facts + observed ADT bounds;
 * no T8103 MSI register initialization or acknowledgement is invented. */
#include "bcm4378_fake_irq.h"

#if defined(KERNEL) && defined(BMI_FAKE_DEVICE)
#error "BMI_FAKE_DEVICE is host-test only; no kernel interrupt/MMIO path"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "fake interrupt slice requires shared PHY and PMGR writes compiled out"
#endif

#ifdef BMI_FAKE_DEVICE
#include "register_accessor.h"

static int profile_ok(const struct bmi_profile *p)
{
    return p && p->address == UINT64_C(0xfffff000) && p->rid == 0x100 &&
        p->vector_base == 0 && p->vector_count == 8 && p->aic_base == 704;
}

static int ready(const struct apb_io *io)
{
    uint32_t phy = 0, run = 0, link = 0, pci = 0;
    if (!apr_bcm_io_bound(io)) return BMI_ARGUMENT;
    if (io->read32(io->context, APB_PHY, 0, &phy)) return BMI_IO;
    if ((phy & 0x8fu) != 0x0fu) return BMI_PHY_NOT_READY;
    if (io->read32(io->context, APB_PORT0, 0x804, &run)) return BMI_IO;
    if (!(run & 1)) return BMI_LINK_DOWN;
    if (io->read32(io->context, APB_PORT0, 0x208, &link)) return BMI_IO;
    if ((link & 0x45u) != 1) return BMI_LINK_DOWN;
    if (io->read32(io->context, APR_BCM_CONFIG, 0, &pci)) return BMI_IO;
    if (pci != 0x442514e4u) return BMI_OWNER;
    /* Catch reset changes during the fake register-read sequence. Caller
     * serialization keeps the fake stable between this read and CPU commit. */
    if (io->read32(io->context, APB_PHY, 0, &phy)) return BMI_IO;
    return (phy & 0x8fu) == 0x0fu ? BMI_FAKE_OK : BMI_PHY_NOT_READY;
}

static int enter(struct bmi_source *s)
{
    if (!s || s->bound != 1 || s->closed || !profile_ok(&s->profile) ||
        (s->allocated & ~0xffu) || (s->masked & ~s->allocated) || (s->pending & ~s->allocated))
        return BMI_STATE;
    if (s->commits == UINT32_MAX) return BMI_LIMIT;
    return ready(&s->io);
}

static int vector_bit(const struct bmi_source *s, uint32_t vector, uint32_t *bit)
{
    if (vector >= s->profile.vector_count) return BMI_OWNER;
    *bit = 1u << vector;
    return (s->allocated & *bit) ? BMI_FAKE_OK : BMI_OWNER;
}

int bmi_fake_source_compiled(void) { return 1; }

int bmi_open(struct bmi_source *s, const struct apb_io *io, const struct bmi_profile *p)
{
    int e;
    if (!s || !profile_ok(p)) return BMI_ARGUMENT;
    if (io == &s->io || p == &s->profile) return BMI_ARGUMENT;
    if (s->bound || s->armed || s->closed) return BMI_STATE;
    if ((e = ready(io))) return e;
    *s = (struct bmi_source){0};
    s->io = *io; s->profile = *p; s->bound = 1; s->commits = 1;
    return BMI_FAKE_OK;
}

int bmi_arm(struct bmi_source *s, uint32_t allocated)
{
    int e = enter(s);
    if (e) return e;
    if (s->armed) return BMI_STATE;
    if (!allocated || (allocated & ~0xffu)) return BMI_OWNER;
    s->allocated = s->masked = allocated;
    s->armed = 1; s->commits++;
    return BMI_FAKE_OK;
}

int bmi_set_mask(struct bmi_source *s, uint32_t vector, int masked)
{
    uint32_t bit;
    int e = enter(s);
    if (e) return e;
    if (!s->armed) return BMI_STATE;
    if (masked != 0 && masked != 1) return BMI_ARGUMENT;
    if ((e = vector_bit(s, vector, &bit))) return e;
    s->masked = masked ? s->masked | bit : s->masked & ~bit;
    s->commits++;
    return BMI_FAKE_OK;
}

int bmi_receive(struct bmi_source *s, uint64_t address, uint32_t data)
{
    uint32_t bit;
    int e = enter(s);
    if (e) return e;
    if (!s->armed) return BMI_STATE;
    if (address != s->profile.address) return BMI_OWNER;
    if ((e = vector_bit(s, data, &bit))) return e;
    if (s->received == UINT32_MAX) return BMI_LIMIT;
    s->pending |= bit; /* repeated messages coalesce one pending bit */
    s->received++; s->commits++;
    return BMI_FAKE_OK;
}

int bmi_dispatch_one(struct bmi_source *s, uint32_t *vector, uint32_t *aic_irq)
{
    uint32_t eligible, selected;
    int e = enter(s);
    if (e) return e;
    if (!vector || !aic_irq || vector == aic_irq) return BMI_ARGUMENT;
    if (!s->armed) return BMI_STATE;
    eligible = s->pending & s->allocated & ~s->masked;
    if (!eligible) return BMI_EMPTY;
    if (s->delivered == UINT32_MAX) return BMI_LIMIT;
    for (selected = 0; selected < 8; ++selected) if (eligible & (1u << selected)) break;
    if (selected == 8) return BMI_STATE;
    s->pending &= ~(1u << selected);
    s->delivered++; s->commits++;
    *vector = selected;
    *aic_irq = s->profile.aic_base + selected;
    return BMI_FAKE_OK;
}

int bmi_close(struct bmi_source *s)
{
    int e = enter(s);
    if (e) return e;
    if (!s->armed) return BMI_STATE;
    if (s->pending || s->masked != s->allocated) return BMI_BUSY;
    s->armed = 0; s->closed = 1; s->commits++;
    return BMI_FAKE_OK;
}
#else
int bmi_fake_source_compiled(void) { return 0; }
int bmi_open(struct bmi_source *s, const struct apb_io *io, const struct bmi_profile *p)
{ (void)s;(void)io;(void)p; return BMI_GATED; }
int bmi_arm(struct bmi_source *s, uint32_t mask) { (void)s;(void)mask; return BMI_GATED; }
int bmi_set_mask(struct bmi_source *s, uint32_t v, int masked)
{ (void)s;(void)v;(void)masked; return BMI_GATED; }
int bmi_receive(struct bmi_source *s, uint64_t address, uint32_t data)
{ (void)s;(void)address;(void)data; return BMI_GATED; }
int bmi_dispatch_one(struct bmi_source *s, uint32_t *v, uint32_t *aic_irq)
{ (void)s;(void)v;(void)aic_irq; return BMI_GATED; }
int bmi_close(struct bmi_source *s) { (void)s; return BMI_GATED; }
#endif
