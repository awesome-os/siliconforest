#ifdef NDEBUG
#error "test_bcm4378_fake_effect_ledger requires assertions"
#endif
/* Reuse the frozen read-only fake file, never run its old main. */
#define main previous_fake_irq_test_main
#define unchanged previous_irq_unchanged
#include "test_bcm4378_fake_irq.c"
#undef main
#undef unchanged
#include "bcm4378_fake_effect_ledger.h"

static struct bfl_ledger L, SL;
static struct bfo_workspace W, SW;
static struct bfl_token T;
static uint8_t root[DLD_PAGE], leaf[DLD_PAGE], sr[DLD_PAGE], sl[DLD_PAGE];
static const struct dld_range RAM[] = {{UINT64_C(0x800000000), 0x2000000}};
static const struct dld_policy POLICY = {{DLD_APCIE_BASE, DLD_APCIE_SIZE}, RAM, 1, NULL, 0};
static const struct bfd_storage STORAGE = {root, leaf, DLD_PAGE, DLD_PAGE,
    UINT64_C(0x800000000), UINT64_C(0x800004000)};
static const uint64_t DATA = UINT64_C(0x800100000);
static unsigned producer_calls;
static enum bfl_outcome producer_outcome;

static void snapshots(void)
{ SL = L; SW = W; memcpy(sr, root, sizeof root); memcpy(sl, leaf, sizeof leaf); }
static void backing_unchanged(void)
{ assert(!memcmp(sr, root, sizeof root) && !memcmp(sl, leaf, sizeof leaf) && !F.writes); }
static void unchanged(void)
{ backing_unchanged(); assert(!memcmp(&SL, &L, sizeof L) && !memcmp(&SW, &W, sizeof W)); }
static void reset_objects(void)
{
    reset(); memset(&L, 0, sizeof L); memset(&W, 0xcc, sizeof W);
    memset(root, 0xa5, sizeof root); memset(leaf, 0x5a, sizeof leaf);
    T = (struct bfl_token){0, 0}; producer_calls = 0; producer_outcome = BFL_EFFECT_OK;
}

#ifdef BFL_FAKE_DEVICE
static unsigned recorded_reads, guard_reads, reentries;
static int observing_read(void *context, unsigned region, uint32_t offset, uint32_t *value)
{
    unsigned before = F.reads;
    /* Every read callback after an effect starts must see its intent BEFORE
     * doing even the fake read. Reentry is refused without another callback. */
    assert(L.busy == 1 || L.busy == 2);
    if (L.busy == 2) {
        assert(L.count && L.count <= BFL_RECORDS);
        const struct bfl_record *r = &L.records[L.count-1];
        assert(r->phase == BFL_PENDING && r->generation == L.leases[r->slot].generation);
        if (r->kind != BFL_EXPOSE)
            assert(L.leases[r->slot].private_started == L.leases[r->slot].private_complete+1);
        else assert(L.leases[r->slot].exposed);
        assert(bfl_retire(&L, T, &W) == BFL_BUSY);
        assert(bfl_expose(&L, T, NULL, NULL) == BFL_BUSY);
        ++recorded_reads; ++reentries;
    } else {
        assert(bfl_open(&L, &IO) == BFL_BUSY);
        ++guard_reads;
    }
    assert(F.reads == before);
    return fake_read(context, region, offset, value);
}
static enum bfl_outcome producer(void *ctx, const struct bfl_record *r)
{
    assert(ctx == &producer_calls && L.busy == 2 && r == &L.records[L.count-1]);
    assert(r->kind == BFL_EXPOSE && r->phase == BFL_PENDING && r->generation == T.generation);
    struct bfl_lease *s = &L.leases[T.slot];
    assert(s->exposed && L.registry.stream == &s->mapping && L.registry.vectors[3] == &s->interrupt);
    assert(s->mapping.map.mapped && !s->interrupt.source.pending);
    assert(bfl_retire(&L, T, &W) == BFL_BUSY);
    assert(bfl_expose(&L, T, producer, ctx) == BFL_BUSY);
    producer_calls++;
    return producer_outcome;
}
static void setup(unsigned op)
{
    reset_objects(); recorded_reads = guard_reads = reentries = 0;
    struct apb_io raw = {NULL, observing_read, fake_write, NULL, NULL, NULL};
    assert(apr_bind_bcm_fake(&A, &raw, &IO) == APB_OK);
    if (op) assert(bfl_open(&L, &IO) == BFL_FAKE_OK);
    if (op >= 2) assert(bfl_acquire(&L, &POLICY, &STORAGE, 3, 3, &T) == BFL_FAKE_OK);
    if (op >= 3) assert(bfl_map(&L, T, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFL_FAKE_OK);
}
static int step(unsigned op)
{
    switch (op) {
    case 0: return bfl_open(&L, &IO);
    case 1: return bfl_acquire(&L, &POLICY, &STORAGE, 3, 3, &T);
    case 2: return bfl_map(&L, T, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3);
    case 3: return bfl_expose(&L, T, producer, &producer_calls);
    default: return bfl_retire(&L, T, &W);
    }
}
static void retained_claims(void)
{
    struct bfl_lease *s = &L.leases[T.slot];
    assert(s->state == BFL_ACTIVE && s->generation == T.generation);
    assert(s->mapping.claimed && !s->mapping.closed && s->mapping.map.storage.root == root);
    assert(s->mapping.map.storage.leaf == leaf && s->interrupt.claimed && !s->interrupt.closed);
    assert(L.registry.stream == &s->mapping && L.registry.vectors[3] == &s->interrupt);
}
static void quarantine_refuses(void)
{
    snapshots(); unsigned reads = F.reads, calls = producer_calls;
    assert(bfl_retire(&L, T, &W) == BFL_QUARANTINED);
    assert(bfl_map(&L, T, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFL_QUARANTINED);
    assert(bfl_expose(&L, T, producer, &producer_calls) == BFL_QUARANTINED);
    assert(F.reads == reads && producer_calls == calls); unchanged(); retained_claims();
    struct bfl_token out = {0xdead, 1};
    assert(bfl_acquire(&L, &POLICY, &STORAGE, 4, 3, &out) == BFO_BUSY);
    assert(out.generation == 0xdead && out.slot == 1); retained_claims();
}
static void generations_and_private_pairs(void)
{
    setup(3); struct bfl_token old = T;
    assert(L.count == 2 && L.records[0].kind == BFL_ACQUIRE && L.records[1].kind == BFL_PRIVATE_MAP);
    struct bfl_lease *s = &L.leases[T.slot];
    assert(s->private_started == 2 && s->private_complete == 2 && !s->exposed);
    assert(recorded_reads && reentries);
    assert(bfl_retire(&L, T, &W) == BFL_FAKE_OK);
    assert(s->state == BFL_RETIRED && !L.registry.stream && !L.registry.vectors[3]);
    assert(s->private_started == 3 && s->private_complete == 3 && L.count == 3);
    for (unsigned i = 0; i < DLD_PAGE; ++i) assert(!root[i] && !leaf[i]);
    struct bfl_record history[3]; memcpy(history, L.records, sizeof history);
    assert(bfl_acquire(&L, &POLICY, &STORAGE, 3, 3, &T) == BFL_FAKE_OK);
    assert(T.generation != old.generation && T.generation == old.generation+1 && T.slot == old.slot);
    assert(!memcmp(history, L.records, sizeof history));
    snapshots(); unsigned reads = F.reads;
    assert(bfl_map(&L, old, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFL_STALE);
    assert(bfl_retire(&L, old, &W) == BFL_STALE);
    assert(bfl_expose(&L, old, producer, &producer_calls) == BFL_STALE);
    struct bfl_token wrong = T; wrong.generation++;
    assert(bfl_retire(&L, wrong, &W) == BFL_STALE);
    wrong = T; wrong.slot = BFL_LEASES;
    assert(bfl_retire(&L, wrong, &W) == BFL_STALE);
    wrong = T; wrong.generation = 0;
    assert(bfl_retire(&L, wrong, &W) == BFL_STALE);
    assert(F.reads == reads); unchanged();
    assert(bfl_retire(&L, T, &W) == BFL_FAKE_OK); /* never mapped, pair still complete */
    setup(2); s = &L.leases[T.slot];
    s->private_started++; /* deliberate inconsistent fixture, no caller ACK accepted */
    snapshots(); reads = F.reads;
    assert(bfl_retire(&L, T, &W) == BFL_STATE && F.reads == reads); unchanged();
}
static void exposure_and_uncertainty(void)
{
    for (unsigned outcome = 0; outcome < 4; ++outcome) {
        setup(3); snapshots();
        producer_outcome = outcome == 3 ? (enum bfl_outcome)-55 : (enum bfl_outcome)outcome;
        int e = bfl_expose(&L, T, producer, &producer_calls);
        assert(producer_calls == 1 && L.leases[T.slot].exposed);
        struct bfl_record *r = &L.records[L.count-1];
        assert(r->kind == BFL_EXPOSE && r->result == (int32_t)producer_outcome);
        if (!outcome) {
            assert(!e && r->phase == BFL_COMPLETE && !L.leases[T.slot].quarantined);
            snapshots(); unsigned reads = F.reads;
            assert(bfl_retire(&L, T, &W) == BFL_EXPOSED && F.reads == reads); unchanged();
        } else {
            assert(e == (outcome == 1 ? BFL_CALLBACK_FAILED : BFL_UNCERTAIN));
            assert(r->phase == (outcome == 1 ? BFL_FAILED : BFL_UNKNOWN));
            assert(L.leases[T.slot].quarantined); quarantine_refuses();
        }
        backing_unchanged(); retained_claims();
    }
    setup(3); snapshots(); F.reset_after_read = F.reads+7; /* successful producer, lost postcheck */
    assert(bfl_expose(&L, T, producer, &producer_calls) == BFO_PHY_NOT_READY && producer_calls == 1);
    assert(L.leases[T.slot].quarantined && L.records[L.count-1].phase == BFL_UNKNOWN);
    F.phy = 0xf; quarantine_refuses();
}
static void unsafe_and_faults(void)
{
    unsigned faults = 0, recorded_faults = 0;
    for (unsigned op = 0; op < 5; ++op) {
        for (unsigned bad = 0; bad < 7; ++bad) {
            setup(op); snapshots(); struct bfl_token token = T;
            if (bad == 0) F.phy = 0x8f; else if (bad == 1) F.phy = 0x80;
            else if (bad == 2) F.link = 0; else if (bad == 3) F.run = 0;
            else if (bad == 4) F.link = 5; else if (bad == 5) F.link = 0x41; else F.phy = 0xb;
            unsigned reads = F.reads;
            assert(step(op) == (bad == 0 || bad == 1 || bad == 6 ? BFO_PHY_NOT_READY : BFO_LINK_DOWN));
            assert(T.generation == token.generation && T.slot == token.slot && !producer_calls);
            if (bad == 0 || bad == 1 || bad == 6) assert(F.reads == reads+1);
            unchanged();
        }
        setup(op); unsigned before = F.reads;
        assert(step(op) == BFL_FAKE_OK);
        unsigned total = F.reads-before;
        for (unsigned n = 1; n <= total; ++n) {
            setup(op); snapshots(); unsigned count = L.count;
            F.fail_at = F.reads+n;
            int e = step(op); assert(e == BFO_IO || e == BFD_IO);
            backing_unchanged(); assert(!L.busy);
            if (L.count != count) {
                assert(L.count == count+1 && L.leases[T.slot].quarantined);
                assert(L.records[count].phase == BFL_FAILED || L.records[count].phase == BFL_UNKNOWN);
                assert(L.leases[T.slot].private_started >= L.leases[T.slot].private_complete);
                quarantine_refuses(); ++recorded_faults;
            } else unchanged(); /* read-only admission failed, no effect had begun */
            ++faults;
        }
    }
    printf("HOST ledger read-fault positions: %u; recorded/quarantined: %u\n", faults, recorded_faults);
    /* Staged IRQ close succeeded, then map close failed. Neither claim may
     * escape even though the private first half individually completed. */
    setup(4); snapshots(); F.fail_at = F.reads+22;
    assert(bfl_retire(&L, T, &W) == BFO_IO);
    assert(L.leases[T.slot].quarantined); backing_unchanged(); retained_claims();
    assert(!memcmp(&L.leases[T.slot].mapping, &SL.leases[T.slot].mapping, sizeof L.leases[0].mapping));
    assert(!memcmp(&L.leases[T.slot].interrupt, &SL.leases[T.slot].interrupt, sizeof L.leases[0].interrupt));
    setup(2); snapshots();
    assert(bfl_map(&L, T, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 4) == DLD_ARGUMENT);
    assert(L.leases[T.slot].quarantined && L.leases[T.slot].private_started == 2 && L.leases[T.slot].private_complete == 1);
    backing_unchanged(); quarantine_refuses();
}
static void limits_and_no_retry(void)
{
    setup(1); L.last_generation = UINT64_MAX; snapshots(); unsigned reads = F.reads;
    assert(bfl_acquire(&L, &POLICY, &STORAGE, 3, 3, &T) == BFL_LIMIT && F.reads == reads); unchanged();
    setup(3);
    while (L.count < BFL_RECORDS) assert(bfl_expose(&L, T, producer, &producer_calls) == BFL_FAKE_OK);
    snapshots(); reads = F.reads; unsigned calls = producer_calls;
    assert(bfl_expose(&L, T, producer, &producer_calls) == BFL_LIMIT);
    assert(bfl_map(&L, T, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFL_LIMIT);
    assert(bfl_retire(&L, T, &W) == BFL_EXPOSED);
    assert(F.reads == reads && producer_calls == calls); unchanged(); retained_claims();
    setup(1); /* Fill append-only history with actual private acquire/retire pairs. */
    for (unsigned i = 0; i < BFL_RECORDS/2; ++i) {
        assert(bfl_acquire(&L, &POLICY, &STORAGE, 3, 3, &T) == BFL_FAKE_OK);
        assert(bfl_retire(&L, T, &W) == BFL_FAKE_OK);
    }
    snapshots(); reads = F.reads;
    assert(bfl_acquire(&L, &POLICY, &STORAGE, 3, 3, &T) == BFL_LIMIT && F.reads == reads); unchanged();
}
#else
static enum bfl_outcome forbidden_producer(void *ctx, const struct bfl_record *r)
{ (void)ctx;(void)r; assert(!"default/kernel cannot call producer"); return BFL_EFFECT_OK; }
static void default_no_io(void)
{
    reset_objects(); snapshots();
    struct apb_io io = {NULL, fake_read, fake_write, NULL, NULL, NULL};
    assert(!bfl_fake_ledger_compiled());
    assert(bfl_open(&L, &io) == BFL_GATED);
    assert(bfl_acquire(&L, &POLICY, &STORAGE, 3, 3, &T) == BFL_GATED);
    assert(bfl_map(&L, T, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFL_GATED);
    assert(bfl_expose(&L, T, forbidden_producer, NULL) == BFL_GATED);
    assert(bfl_retire(&L, T, &W) == BFL_GATED);
    assert(!F.reads && !producer_calls && !T.generation); unchanged();
}
#endif
int main(void)
{
    assert(!apb_shared_writes_compiled() && !apl_shared_writes_compiled());
    assert(!apg_fake_drive_compiled() && !ars_fake_enable_compiled());
#ifdef BFL_FAKE_DEVICE
    assert(bfl_fake_ledger_compiled());
    generations_and_private_pairs(); exposure_and_uncertainty(); unsafe_and_faults(); limits_and_no_retry();
#else
    default_no_io();
#endif
    puts("HOST ONLY PASS: generation/intents/quarantine, no exposed release or register write");
    return 0;
}
