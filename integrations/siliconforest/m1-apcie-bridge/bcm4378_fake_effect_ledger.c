/* Original synchronous host model. Frozen registry/codec are unchanged. */
#include "bcm4378_fake_effect_ledger.h"
#if defined(KERNEL) && defined(BFL_FAKE_DEVICE)
#error "BFL_FAKE_DEVICE is host-test only; no kernel effect/MMIO path"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "fake ledger requires shared PHY and PMGR writes compiled out"
#endif
#if defined(BFL_FAKE_DEVICE) && (!defined(BFO_FAKE_DEVICE) || !defined(BFD_FAKE_DEVICE) || !defined(BMI_FAKE_DEVICE))
#error "fake ledger requires explicit registry DART and interrupt fake builds"
#endif

#ifdef BFL_FAKE_DEVICE
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
    return (phy & 0x8fu) == 0x0fu ? BFL_FAKE_OK : BFO_PHY_NOT_READY;
}
static int enter(struct bfl_ledger *l)
{
    if (!l) return BFL_STATE;
    if (l->busy) return BFL_BUSY;
    return l->bound == 1 ? BFL_FAKE_OK : BFL_STATE;
}
static int lease_get(struct bfl_ledger *l, struct bfl_token t, struct bfl_lease **out)
{
    int e = enter(l);
    if (e) return e;
    if (t.slot >= BFL_LEASES || !t.generation) return BFL_STALE;
    struct bfl_lease *s = &l->leases[t.slot];
    if (s->state != BFL_ACTIVE || s->generation != t.generation) return BFL_STALE;
    if (s->quarantined) return BFL_QUARANTINED;
    if (l->registry.stream != &s->mapping || s->interrupt.vector >= 8 ||
        l->registry.vectors[s->interrupt.vector] != &s->interrupt) return BFO_OWNER;
    *out = s;
    return BFL_FAKE_OK;
}
static int preflight(struct bfl_ledger *l)
{
    l->busy = 1;
    int e = ready(&l->registry);
    l->busy = 0;
    return e;
}
static struct bfl_record *begin(struct bfl_ledger *l, struct bfl_lease *s, uint32_t slot, unsigned kind)
{
    struct bfl_record *r = &l->records[l->count];
    *r = (struct bfl_record){s->generation, slot, l->count+1, kind, BFL_PENDING, 0};
    l->count++;
    if (kind != BFL_EXPOSE) s->private_started++;
    else s->exposed = 1; /* must precede every producer callback */
    l->busy = 2;
    return r;
}
static int finish(struct bfl_ledger *l, struct bfl_lease *s, struct bfl_record *r, int e)
{
    r->result = e;
    if (e) { r->phase = BFL_FAILED; s->quarantined = 1; }
    else {
        r->phase = BFL_COMPLETE;
        if (r->kind != BFL_EXPOSE) s->private_complete++;
    }
    l->busy = 0;
    return e;
}

int bfl_fake_ledger_compiled(void) { return 1; }
int bfl_open(struct bfl_ledger *l, const struct apb_io *io)
{
    if (!l || !io) return BFL_ARGUMENT;
    if (l->busy) return BFL_BUSY;
    if (l->bound || l->last_generation || l->count || l->registry.bound) return BFL_STATE;
    struct bfo_registry staged = {0};
    l->busy = 1;
    int e = bfo_registry_open(&staged, io);
    l->busy = 0;
    if (e) return e;
    l->registry = staged; l->bound = 1;
    return BFL_FAKE_OK;
}
int bfl_acquire(struct bfl_ledger *l, const struct dld_policy *p, const struct bfd_storage *storage,
                 uint32_t sid, uint32_t vector, struct bfl_token *out)
{
    int e = enter(l);
    if (e) return e;
    if (!out) return BFL_ARGUMENT;
    if (l->count >= BFL_RECORDS || l->last_generation == UINT64_MAX) return BFL_LIMIT;
    unsigned slot;
    for (slot = 0; slot < BFL_LEASES; ++slot) if (l->leases[slot].state != BFL_ACTIVE) break;
    if (slot == BFL_LEASES) return BFL_LIMIT;
    if ((e = preflight(l))) return e;
    /* Read-only admission against private copies: no lease/effect exists yet.
     * Old helpers only validate and initialize private model metadata here. */
    struct bfo_registry reg = l->registry;
    struct bfo_mapping m = {0};
    struct bfo_interrupt q = {0};
    l->busy = 1;
    e = bfo_map_open(&reg, &m, p, storage, sid);
    if (!e) e = bfo_irq_open(&reg, &q, vector);
    if (!e) e = ready(&l->registry);
    l->busy = 0;
    if (e) return e;
    struct bfl_lease *s = &l->leases[slot];
    *s = (struct bfl_lease){0};
    s->generation = ++l->last_generation; s->state = BFL_ACTIVE;
    m.registry = &l->registry; q.registry = &l->registry;
    s->mapping = m; s->interrupt = q;
    struct bfl_record *r = begin(l, s, slot, BFL_ACQUIRE);
    l->registry.stream = &s->mapping;
    l->registry.vectors[vector] = &s->interrupt;
    finish(l, s, r, 0); /* no callback or fallible step after ownership commit */
    *out = (struct bfl_token){s->generation, slot};
    return BFL_FAKE_OK;
}
int bfl_map(struct bfl_ledger *l, struct bfl_token t, struct bfo_workspace *w,
              uint64_t iova, uint64_t pa, uint64_t bytes, unsigned permissions)
{
    struct bfl_lease *s;
    int e = lease_get(l, t, &s);
    if (e) return e;
    if (!w) return BFL_ARGUMENT;
    if (l->count >= BFL_RECORDS) return BFL_LIMIT;
    if ((e = preflight(l))) return e;
    struct bfl_record *r = begin(l, s, t.slot, BFL_PRIVATE_MAP);
    e = bfo_map_pages(&s->mapping, w, iova, pa, bytes, permissions);
    return finish(l, s, r, e);
}
int bfl_expose(struct bfl_ledger *l, struct bfl_token t, bfl_fake_effect callback, void *context)
{
    struct bfl_lease *s;
    int e = lease_get(l, t, &s);
    if (e) return e;
    if (!callback) return BFL_ARGUMENT;
    if (!s->mapping.map.mapped || s->mapping.map.faulted) return BFL_STATE;
    if (l->count >= BFL_RECORDS) return BFL_LIMIT;
    if ((e = preflight(l))) return e;
    struct bfl_record *r = begin(l, s, t.slot, BFL_EXPOSE);
    enum bfl_outcome outcome = callback(context, r);
    if (outcome != BFL_EFFECT_OK) {
        r->result = (int32_t)outcome;
        r->phase = outcome == BFL_EFFECT_FAILED ? BFL_FAILED : BFL_UNKNOWN;
        s->quarantined = 1; l->busy = 0;
        return outcome == BFL_EFFECT_FAILED ? BFL_CALLBACK_FAILED : BFL_UNCERTAIN;
    }
    e = ready(&l->registry); /* still PENDING and exposed during later read callbacks */
    if (e) {
        r->result = e; r->phase = BFL_UNKNOWN;
        s->quarantined = 1; l->busy = 0;
        return e;
    }
    return finish(l, s, r, 0);
}
int bfl_retire(struct bfl_ledger *l, struct bfl_token t, struct bfo_workspace *w)
{
    struct bfl_lease *s;
    int e = lease_get(l, t, &s);
    if (e) return e;
    if (s->exposed) return BFL_EXPOSED;
    if (!w) return BFL_ARGUMENT;
    if (s->private_started != s->private_complete) return BFL_STATE;
    if (l->count >= BFL_RECORDS) return BFL_LIMIT;
    if ((e = preflight(l))) return e;
    struct bfl_record *r = begin(l, s, t.slot, BFL_PRIVATE_RETIRE);
    /* Stage BOTH closes. Even if close1 succeeds then close2 fails, the live
     * registry retains requester, vector, backing and original source state. */
    struct bfo_registry reg = l->registry;
    struct bfo_mapping m = s->mapping;
    struct bfo_interrupt q = s->interrupt;
    m.registry = &reg; q.registry = &reg;
    reg.stream = &m; reg.vectors[q.vector] = &q;
    m.map.storage.root = w->root; m.map.storage.leaf = w->leaf;
    e = bfo_irq_close(&q);
    if (!e) e = bfo_map_close(&m);
    if (!e) e = ready(&l->registry);
    if (e) return finish(l, s, r, e);
    if (s->mapping.map.mapped) {
        memset(s->mapping.map.storage.root, 0, DLD_PAGE);
        memset(s->mapping.map.storage.leaf, 0, DLD_PAGE);
    }
    m.map.storage = s->mapping.map.storage;
    m.registry = &l->registry; q.registry = &l->registry;
    s->mapping = m; s->interrupt = q;
    l->registry.stream = NULL; l->registry.vectors[q.vector] = NULL;
    s->state = BFL_RETIRED;
    return finish(l, s, r, 0); /* no callback after releasing either claim */
}
#else
int bfl_fake_ledger_compiled(void) { return 0; }
int bfl_open(struct bfl_ledger *l, const struct apb_io *io)
{ (void)l;(void)io; return BFL_GATED; }
int bfl_acquire(struct bfl_ledger *l, const struct dld_policy *p, const struct bfd_storage *s,
                 uint32_t sid, uint32_t vector, struct bfl_token *t)
{ (void)l;(void)p;(void)s;(void)sid;(void)vector;(void)t; return BFL_GATED; }
int bfl_map(struct bfl_ledger *l, struct bfl_token t, struct bfo_workspace *w,
              uint64_t i, uint64_t p, uint64_t n, unsigned a)
{ (void)l;(void)t;(void)w;(void)i;(void)p;(void)n;(void)a; return BFL_GATED; }
int bfl_expose(struct bfl_ledger *l, struct bfl_token t, bfl_fake_effect c, void *x)
{ (void)l;(void)t;(void)c;(void)x; return BFL_GATED; }
int bfl_retire(struct bfl_ledger *l, struct bfl_token t, struct bfo_workspace *w)
{ (void)l;(void)t;(void)w; return BFL_GATED; }
#endif
