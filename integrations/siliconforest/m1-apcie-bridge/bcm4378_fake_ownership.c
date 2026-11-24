/* Original software-only ownership layer over unchanged fake DART and IRQ. */
#include "bcm4378_fake_ownership.h"
#if defined(KERNEL) && defined(BFO_FAKE_DEVICE)
#error "BFO_FAKE_DEVICE is host-test only; no kernel ownership/MMIO path"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "fake ownership requires shared PHY and PMGR writes compiled out"
#endif
#if defined(BFO_FAKE_DEVICE) && (!defined(BFD_FAKE_DEVICE) || !defined(BMI_FAKE_DEVICE))
#error "fake ownership requires explicit fake DART and interrupt builds"
#endif

#ifdef BFO_FAKE_DEVICE
#include "register_accessor.h"
#include <string.h>

static int ready(const struct bfo_registry *r)
{
    uint32_t phy = 0, run = 0, link = 0, pci = 0;
    if (!r || r->bound != 1 || r->closed || !apr_bcm_io_bound(&r->io)) return BFO_STATE;
    const struct apb_io *io = &r->io;
    if (io->read32(io->context, APB_PHY, 0, &phy)) return BFO_IO;
    if ((phy & 0x8fu) != 0x0fu) return BFO_PHY_NOT_READY;
    if (io->read32(io->context, APB_PORT0, 0x804, &run)) return BFO_IO;
    if (!(run & 1)) return BFO_LINK_DOWN;
    if (io->read32(io->context, APB_PORT0, 0x208, &link)) return BFO_IO;
    if ((link & 0x45u) != 1) return BFO_LINK_DOWN;
    if (io->read32(io->context, APR_BCM_CONFIG, 0, &pci)) return BFO_IO;
    if (pci != 0x442514e4u) return BFO_OWNER;
    if (io->read32(io->context, APB_PHY, 0, &phy)) return BFO_IO;
    return (phy & 0x8fu) == 0x0fu ? BFO_FAKE_OK : BFO_PHY_NOT_READY;
}
static int map_error(struct bfo_mapping *m, int e)
{
    if (e == BFO_IO || e == BFD_IO || e == DLD_ENCODING || e == DLD_TOPOLOGY)
        m->map.faulted = 1;
    return e;
}
static int map_enter(struct bfo_mapping *m)
{
    if (!m || !m->claimed || m->closed || !m->registry || m->registry->stream != m ||
        m->map.bound != 1 || m->map.binding.rid != 0x100 || m->map.binding.sid >= 16)
        return BFO_OWNER;
    if (m->map.faulted) return BFO_FAULTED;
    return map_error(m, ready(m->registry));
}
static int irq_enter(struct bfo_interrupt *s)
{
    if (!s || !s->claimed || s->closed || !s->registry || s->vector >= 8 ||
        s->registry->vectors[s->vector] != s || !s->source.armed ||
        s->source.allocated != (1u << s->vector)) return BFO_OWNER;
    return ready(s->registry);
}
static int irq_commit(struct bfo_interrupt *s, const struct bmi_source *candidate)
{
    int e = ready(s->registry);
    if (e) return e;
    s->source = *candidate;
    return BFO_FAKE_OK;
}

int bfo_fake_registry_compiled(void) { return 1; }
int bfo_registry_open(struct bfo_registry *r, const struct apb_io *io)
{
    if (!r || !io || io == &r->io) return BFO_ARGUMENT;
    if (r->bound || r->closed || r->stream) return BFO_STATE;
    for (unsigned i = 0; i < 8; ++i) if (r->vectors[i]) return BFO_STATE;
    struct bfo_registry candidate = {0}; candidate.io = *io; candidate.bound = 1;
    int e = ready(&candidate);
    if (e) return e;
    *r = candidate;
    return BFO_FAKE_OK;
}
int bfo_registry_close(struct bfo_registry *r)
{
    int e = ready(r);
    if (e) return e;
    if (r->stream) return BFO_BUSY;
    for (unsigned i = 0; i < 8; ++i) if (r->vectors[i]) return BFO_BUSY;
    r->closed = 1;
    return BFO_FAKE_OK;
}
int bfo_map_open(struct bfo_registry *r, struct bfo_mapping *m,
                 const struct dld_policy *p, const struct bfd_storage *storage, uint32_t sid)
{
    int e = ready(r);
    if (e) return e;
    if (!m || m->claimed || m->closed || m->map.bound || m->map.faulted) return BFO_STATE;
    if (r->stream) return BFO_BUSY;
    struct bfd_map candidate = {0};
    if ((e = bfd_init(&candidate, &r->io, p, storage, 0x100, sid))) return e;
    if ((e = ready(r))) return e;
    m->map = candidate; m->registry = r; m->claimed = 1;
    r->stream = m;
    return BFO_FAKE_OK;
}
int bfo_map_pages(struct bfo_mapping *m, struct bfo_workspace *w, uint64_t iova,
                  uint64_t pa, uint64_t bytes, unsigned permissions)
{
    int e = map_enter(m);
    if (e) return e;
    if (!w || m->map.mapped) return BFO_STATE;
    /* Never let the older link-only helper commit the owned output before
     * our final PHY guard. Its output here is inactive caller scratch. */
    struct bfd_map candidate = m->map;
    candidate.storage.root = w->root; candidate.storage.leaf = w->leaf;
    e = bfd_map_pages(&candidate, &w->codec, iova, pa, bytes, permissions);
    if (e) return map_error(m, e);
    if ((e = map_error(m, ready(m->registry)))) return e;
    memcpy(m->map.storage.root, w->root, DLD_PAGE);
    memcpy(m->map.storage.leaf, w->leaf, DLD_PAGE);
    candidate.storage = m->map.storage;
    candidate.tables[0].bytes = candidate.storage.root;
    candidate.tables[1].bytes = candidate.storage.leaf;
    candidate.image.tables = m->map.tables; /* destination, never stack */
    m->map = candidate;
    return BFO_FAKE_OK;
}
int bfo_translate(struct bfo_mapping *m, struct bfo_workspace *w, uint32_t rid, uint32_t sid,
                  uint64_t iova, unsigned access, uint64_t *pa)
{
    int e = map_enter(m);
    if (e) return e;
    if (!w || !pa) return BFO_ARGUMENT;
    uint64_t value;
    e = bfd_translate(&m->map, &w->codec, rid, sid, iova, access, &value);
    if (e) return map_error(m, e);
    if ((e = map_error(m, ready(m->registry)))) return e;
    *pa = value;
    return BFO_FAKE_OK;
}
int bfo_map_close(struct bfo_mapping *m)
{
    int e = map_enter(m);
    if (e) return e;
    if (m->map.mapped) {
        memset(m->map.storage.root, 0, DLD_PAGE);
        memset(m->map.storage.leaf, 0, DLD_PAGE);
    }
    memset(&m->map.image, 0, sizeof m->map.image);
    memset(m->map.tables, 0, sizeof m->map.tables);
    m->map.mapped = 0; m->map.mapped_pages = 0;
    m->registry->stream = NULL; m->claimed = 0; m->closed = 1;
    return BFO_FAKE_OK;
}
int bfo_irq_open(struct bfo_registry *r, struct bfo_interrupt *s, uint32_t vector)
{
    int e = ready(r);
    if (e) return e;
    if (!s || s->claimed || s->closed || s->source.bound) return BFO_STATE;
    if (vector >= 8) return BFO_ARGUMENT;
    if (r->vectors[vector]) return BFO_BUSY;
    const struct bmi_profile p = {UINT64_C(0xfffff000), 0x100, 0, 8, 704};
    struct bmi_source candidate = {0};
    if ((e = bmi_open(&candidate, &r->io, &p))) return e;
    if ((e = bmi_arm(&candidate, 1u << vector))) return e;
    if ((e = ready(r))) return e;
    s->source = candidate; s->registry = r; s->vector = vector; s->claimed = 1;
    r->vectors[vector] = s;
    return BFO_FAKE_OK;
}
int bfo_irq_mask(struct bfo_interrupt *s, int masked)
{
    int e = irq_enter(s);
    if (e) return e;
    struct bmi_source candidate = s->source;
    if ((e = bmi_set_mask(&candidate, s->vector, masked))) return e;
    return irq_commit(s, &candidate);
}
int bfo_irq_receive(struct bfo_interrupt *s, uint64_t address, uint32_t data)
{
    int e = irq_enter(s);
    if (e) return e;
    struct bmi_source candidate = s->source;
    if ((e = bmi_receive(&candidate, address, data))) return e;
    return irq_commit(s, &candidate);
}
int bfo_irq_dispatch(struct bfo_interrupt *s, uint32_t *vector, uint32_t *irq)
{
    int e = irq_enter(s);
    if (e) return e;
    if (!vector || !irq || vector == irq) return BFO_ARGUMENT;
    uint32_t v, a;
    struct bmi_source candidate = s->source;
    if ((e = bmi_dispatch_one(&candidate, &v, &a))) return e;
    if ((e = irq_commit(s, &candidate))) return e;
    *vector = v; *irq = a;
    return BFO_FAKE_OK;
}
int bfo_irq_close(struct bfo_interrupt *s)
{
    int e = irq_enter(s);
    if (e) return e;
    struct bmi_source candidate = s->source;
    if ((e = bmi_close(&candidate))) return e;
    if ((e = irq_commit(s, &candidate))) return e;
    s->registry->vectors[s->vector] = NULL; s->claimed = 0; s->closed = 1;
    return BFO_FAKE_OK;
}
#else
int bfo_fake_registry_compiled(void) { return 0; }
int bfo_registry_open(struct bfo_registry *r, const struct apb_io *io)
{ (void)r;(void)io; return BFO_GATED; }
int bfo_registry_close(struct bfo_registry *r) { (void)r; return BFO_GATED; }
int bfo_map_open(struct bfo_registry *r, struct bfo_mapping *m, const struct dld_policy *p,
                 const struct bfd_storage *s, uint32_t sid)
{ (void)r;(void)m;(void)p;(void)s;(void)sid; return BFO_GATED; }
int bfo_map_pages(struct bfo_mapping *m, struct bfo_workspace *w, uint64_t i, uint64_t p, uint64_t n, unsigned a)
{ (void)m;(void)w;(void)i;(void)p;(void)n;(void)a; return BFO_GATED; }
int bfo_translate(struct bfo_mapping *m, struct bfo_workspace *w, uint32_t r, uint32_t s,
                  uint64_t i, unsigned a, uint64_t *p)
{ (void)m;(void)w;(void)r;(void)s;(void)i;(void)a;(void)p; return BFO_GATED; }
int bfo_map_close(struct bfo_mapping *m) { (void)m; return BFO_GATED; }
int bfo_irq_open(struct bfo_registry *r, struct bfo_interrupt *s, uint32_t v)
{ (void)r;(void)s;(void)v; return BFO_GATED; }
int bfo_irq_mask(struct bfo_interrupt *s, int m) { (void)s;(void)m; return BFO_GATED; }
int bfo_irq_receive(struct bfo_interrupt *s, uint64_t a, uint32_t d)
{ (void)s;(void)a;(void)d; return BFO_GATED; }
int bfo_irq_dispatch(struct bfo_interrupt *s, uint32_t *v, uint32_t *i)
{ (void)s;(void)v;(void)i; return BFO_GATED; }
int bfo_irq_close(struct bfo_interrupt *s) { (void)s; return BFO_GATED; }
#endif
