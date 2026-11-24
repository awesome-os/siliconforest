/* Exact existing IRQ fake register backend, no physical access implementation. */
#ifdef NDEBUG
#error "test_bcm4378_fake_ownership requires assertions"
#endif
#define main previous_fake_irq_test_main
#include "test_bcm4378_fake_irq.c"
#undef main
#include "bcm4378_fake_ownership.h"

static struct bfo_registry G, SG;
static struct bfo_mapping M, SM, SECOND;
static struct bfo_interrupt Q, SQ, OTHER;
static struct bfo_workspace W, SW;
static uint8_t root[DLD_PAGE], leaf[DLD_PAGE], sr[DLD_PAGE], sl[DLD_PAGE];
static const struct dld_range RAM[] = {{UINT64_C(0x800000000), 0x2000000}};
static const struct dld_policy POLICY = {{DLD_APCIE_BASE, DLD_APCIE_SIZE}, RAM, 1, NULL, 0};
static const struct bfd_storage STORAGE = {root, leaf, DLD_PAGE, DLD_PAGE,
    UINT64_C(0x800000000), UINT64_C(0x800004000)};
static const uint64_t DATA = UINT64_C(0x800100000);
static void snapshots(void)
{
    SG = G; SM = M; SQ = Q; SW = W;
    memcpy(sr, root, sizeof root); memcpy(sl, leaf, sizeof leaf);
}
static void preserved(int scratch, int fault)
{
    struct bfo_mapping expected = SM;
    if (fault) expected.map.faulted = 1;
    assert(!memcmp(&G, &SG, sizeof G) && !memcmp(&M, &expected, sizeof M));
    assert(!memcmp(&Q, &SQ, sizeof Q));
    assert(!memcmp(root, sr, sizeof root) && !memcmp(leaf, sl, sizeof leaf));
    if (scratch) assert(!memcmp(&W, &SW, sizeof W));
    assert(F.writes == 0);
}
static void objects_reset(void)
{
    reset(); memset(&G, 0, sizeof G); memset(&M, 0, sizeof M); memset(&Q, 0, sizeof Q);
    memset(&SECOND, 0, sizeof SECOND); memset(&OTHER, 0, sizeof OTHER);
    memset(&W, 0xcc, sizeof W); memset(root, 0xa5, sizeof root); memset(leaf, 0x5a, sizeof leaf);
}

#ifdef BFO_FAKE_DEVICE
static void setup(unsigned op)
{
    objects_reset(); bind();
    if (op) assert(bfo_registry_open(&G, &IO) == BFO_FAKE_OK);
    if (op >= 3 && op <= 5) assert(bfo_map_open(&G, &M, &POLICY, &STORAGE, 3) == BFO_FAKE_OK);
    if (op == 4 || op == 5) assert(bfo_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFO_FAKE_OK);
    if (op >= 7) assert(bfo_irq_open(&G, &Q, 3) == BFO_FAKE_OK);
    if (op == 9) {
        assert(bfo_irq_mask(&Q, 0) == BFO_FAKE_OK);
        assert(bfo_irq_receive(&Q, PROFILE.address, 3) == BFO_FAKE_OK);
    }
}
static int step(unsigned op, uint64_t *pa, uint32_t *v, uint32_t *irq)
{
    switch (op) {
    case 0: return bfo_registry_open(&G, &IO);
    case 1: return bfo_registry_close(&G);
    case 2: return bfo_map_open(&G, &M, &POLICY, &STORAGE, 3);
    case 3: return bfo_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3);
    case 4: return bfo_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE+0x123, 1, pa);
    case 5: return bfo_map_close(&M);
    case 6: return bfo_irq_open(&G, &Q, 3);
    case 7: return bfo_irq_mask(&Q, 0);
    case 8: return bfo_irq_receive(&Q, PROFILE.address, 3);
    case 9: return bfo_irq_dispatch(&Q, v, irq);
    default: return bfo_irq_close(&Q);
    }
}
static void exclusive_lifecycle(void)
{
    setup(3); snapshots();
    assert(bfo_map_open(&G, &SECOND, &POLICY, &STORAGE, 3) == BFO_BUSY);
    assert(bfo_map_open(&G, &SECOND, &POLICY, &STORAGE, 4) == BFO_BUSY); /* same RID, other SID */
    assert(!SECOND.claimed && !SECOND.map.bound); preserved(1, 0);
    assert(bfo_registry_close(&G) == BFO_BUSY); preserved(1, 0);
    assert(bfo_map_pages(&M, &W, DLD_APCIE_BASE, DATA, 2*DLD_PAGE, 3) == BFO_FAKE_OK);
    assert(M.map.image.tables == M.map.tables && M.map.tables[0].bytes == root && M.map.tables[1].bytes == leaf);
    uint64_t pa = 0xdead;
    assert(bfo_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE+DLD_PAGE+0x123, 1, &pa) == BFO_FAKE_OK);
    assert(pa == DATA+DLD_PAGE+0x123);
    snapshots(); pa = 0xdead;
    assert(bfo_translate(&M, &W, 0x101, 3, DLD_APCIE_BASE, 1, &pa) == BFD_OWNER && pa == 0xdead);
    assert(bfo_translate(&M, &W, 0x100, 2, DLD_APCIE_BASE, 1, &pa) == BFD_OWNER && pa == 0xdead);
    struct bfo_mapping stolen = M;
    assert(bfo_map_close(&stolen) == BFO_OWNER); preserved(0, 0);
    assert(bfo_map_close(&M) == BFO_FAKE_OK);
    assert(!G.stream && M.closed && !M.claimed && !M.map.mapped && !M.map.mapped_pages);
    for (unsigned i = 0; i < DLD_PAGE; ++i) assert(!root[i] && !leaf[i]);
    struct dld_image empty = {0}; assert(!memcmp(&M.map.image, &empty, sizeof empty));
    assert(M.map.storage.root == root && M.map.storage.leaf == leaf && M.map.table_commits == 1);
    snapshots();
    assert(bfo_map_close(&M) == BFO_OWNER && bfo_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFO_OWNER);
    assert(bfo_map_open(&G, &M, &POLICY, &STORAGE, 3) == BFO_STATE); preserved(1, 0);
    assert(bfo_map_open(&G, &SECOND, &POLICY, &STORAGE, 3) == BFO_FAKE_OK);
    assert(bfo_map_close(&SECOND) == BFO_FAKE_OK); /* release unopened image, no table reuse */
    assert(bfo_registry_close(&G) == BFO_FAKE_OK && G.closed);
    assert(bfo_registry_open(&G, &IO) == BFO_STATE);

    setup(3); /* Independent resources share one registry's close boundary. */
    assert(bfo_irq_open(&G, &Q, 3) == BFO_FAKE_OK);
    assert(bfo_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFO_FAKE_OK);
    assert(bfo_irq_close(&Q) == BFO_FAKE_OK && G.stream == &M);
    assert(bfo_registry_close(&G) == BFO_BUSY);
    assert(bfo_map_close(&M) == BFO_FAKE_OK && bfo_registry_close(&G) == BFO_FAKE_OK);

    for (unsigned vec = 0; vec < 8; ++vec) {
        setup(6); assert(bfo_irq_open(&G, &Q, vec) == BFO_FAKE_OK);
        snapshots();
        assert(bfo_irq_open(&G, &OTHER, vec) == BFO_BUSY && !OTHER.claimed && !OTHER.source.bound);
        assert(bfo_registry_close(&G) == BFO_BUSY); preserved(1, 0);
        uint32_t alternate = (vec+1)%8;
        assert(bfo_irq_open(&G, &OTHER, alternate) == BFO_FAKE_OK);
        assert(G.vectors[vec] == &Q && G.vectors[alternate] == &OTHER);
        assert(bfo_irq_close(&OTHER) == BFO_FAKE_OK && !G.vectors[alternate]);
        assert(bfo_irq_receive(&Q, PROFILE.address, vec) == BFO_FAKE_OK);
        assert(bfo_irq_receive(&Q, PROFILE.address, vec) == BFO_FAKE_OK); /* coalesces */
        snapshots();
        assert(Q.source.pending == (1u << vec) && bfo_irq_close(&Q) == BMI_BUSY); preserved(1, 0);
        assert(bfo_irq_mask(&Q, 0) == BFO_FAKE_OK);
        uint32_t v = 0xdead, irq = 0xbeef;
        assert(bfo_irq_dispatch(&Q, &v, &irq) == BFO_FAKE_OK && v == vec && irq == 704+vec);
        assert(Q.source.received == 2 && Q.source.delivered == 1 && !Q.source.pending);
        snapshots(); assert(bfo_irq_close(&Q) == BMI_BUSY); preserved(1, 0); /* unmasked */
        assert(bfo_irq_mask(&Q, 1) == BFO_FAKE_OK);
        struct bfo_interrupt stolen_irq = Q;
        assert(bfo_irq_close(&stolen_irq) == BFO_OWNER);
        assert(bfo_irq_close(&Q) == BFO_FAKE_OK && !G.vectors[vec]);
        assert(bfo_irq_open(&G, &Q, vec) == BFO_STATE && bfo_irq_close(&Q) == BFO_OWNER);
        struct bfo_interrupt fresh = {0}; /* new owner, old closed handles retained */
        assert(bfo_irq_open(&G, &fresh, vec) == BFO_FAKE_OK);
        assert(bfo_irq_close(&fresh) == BFO_FAKE_OK);
        assert(bfo_registry_close(&G) == BFO_FAKE_OK);
    }
}
static void guards_and_fault_sweep(void)
{
    unsigned fault_cases = 0;
    for (unsigned op = 0; op < 11; ++op) {
        for (unsigned bad = 0; bad < 7; ++bad) {
            setup(op); snapshots();
            if (bad == 0) F.phy = 0x8f; else if (bad == 1) F.phy = 0x80;
            else if (bad == 2) F.link = 0; else if (bad == 3) F.run = 0;
            else if (bad == 4) F.link = 5; else if (bad == 5) F.link = 0x41; else F.phy = 0xb;
            uint64_t pa = 0xdead; uint32_t v = 0xdead, irq = 0xbeef; unsigned reads = F.reads;
            assert(step(op, &pa, &v, &irq) == (bad == 0 || bad == 1 || bad == 6 ? BFO_PHY_NOT_READY : BFO_LINK_DOWN));
            assert(pa == 0xdead && v == 0xdead && irq == 0xbeef); preserved(1, 0);
            if (bad == 0 || bad == 1 || bad == 6) assert(F.reads == reads+1);
        }
        setup(op); snapshots(); F.reset_after_read = F.reads+2;
        uint64_t pa = 0xdead; uint32_t v = 0xdead, irq = 0xbeef;
        assert(step(op, &pa, &v, &irq) == BFO_PHY_NOT_READY); preserved(1, 0);
        assert(pa == 0xdead && v == 0xdead && irq == 0xbeef);
        setup(op); unsigned start = F.reads;
        assert(step(op, &pa, &v, &irq) == BFO_FAKE_OK);
        unsigned total = F.reads-start;
        for (unsigned n = 1; n <= total; ++n) {
            setup(op); snapshots(); F.fail_at = F.reads+n; pa = 0xdead; v = 0xdead; irq = 0xbeef;
            int e = step(op, &pa, &v, &irq);
            assert(e == BFO_IO || e == BFD_IO); /* BFD_IO equals BMI_IO, exact predecessor result */
            assert(pa == 0xdead && v == 0xdead && irq == 0xbeef);
            int active_map = op >= 3 && op <= 5;
            preserved(0, active_map);
            if (active_map) {
                unsigned reads = F.reads;
                assert(M.map.faulted && G.stream == &M);
                assert(bfo_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFO_FAULTED);
                assert(bfo_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE, 1, &pa) == BFO_FAULTED);
                assert(bfo_map_close(&M) == BFO_FAULTED && F.reads == reads && G.stream == &M);
                assert(bfo_map_open(&G, &SECOND, &POLICY, &STORAGE, 3) == BFO_BUSY);
            }
            ++fault_cases;
        }
    }
    printf("HOST ownership read-fault cases: %u\n", fault_cases);
    /* Older fake DART only samples link: a reset after its final read must
     * still prevent writes to our owned table output or translation output. */
    setup(3); snapshots(); F.reset_after_read = F.reads+11;
    assert(bfo_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFO_PHY_NOT_READY); preserved(0, 0);
    setup(4); snapshots(); F.reset_after_read = F.reads+8;
    uint64_t pa = 0xdead;
    assert(bfo_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE, 1, &pa) == BFO_PHY_NOT_READY && pa == 0xdead); preserved(0, 0);
    for (unsigned op = 7; op <= 10; ++op) {
        setup(op); snapshots(); F.reset_after_read = F.reads+10;
        uint32_t v = 0xdead, irq = 0xbeef; pa = 0xdead;
        assert(step(op, &pa, &v, &irq) == BFO_PHY_NOT_READY); preserved(0, 0);
        assert(v == 0xdead && irq == 0xbeef);
    }
}
static void refusals_and_corruption(void)
{
    setup(2); snapshots();
    assert(bfo_map_open(&G, &M, &POLICY, &STORAGE, 16) == BFD_OWNER); preserved(1, 0);
    assert(bfo_irq_open(&G, &Q, 8) == BFO_ARGUMENT); preserved(1, 0);
    setup(7); snapshots();
    assert(bfo_irq_mask(&Q, 2) == BMI_ARGUMENT);
    assert(bfo_irq_receive(&Q, PROFILE.address+4, 3) == BMI_OWNER);
    assert(bfo_irq_receive(&Q, PROFILE.address, 2) == BMI_OWNER);
    uint32_t v = 0xdead, irq = 0xbeef;
    assert(bfo_irq_dispatch(&Q, &v, &irq) == BMI_EMPTY && v == 0xdead && irq == 0xbeef); preserved(1, 0);
    setup(9); snapshots();
    assert(bfo_irq_dispatch(&Q, NULL, &irq) == BFO_ARGUMENT);
    assert(bfo_irq_dispatch(&Q, &v, &v) == BFO_ARGUMENT); preserved(1, 0);
    setup(3); snapshots();
    assert(bfo_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 4) == DLD_ARGUMENT);
    preserved(1, 0); assert(!M.map.faulted);
    setup(4); root[0] |= 2; /* invalid table word, no valid translation */
    uint64_t pa = 0xdead;
    assert(bfo_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE, 1, &pa) == DLD_ENCODING && pa == 0xdead);
    assert(M.map.faulted && bfo_map_close(&M) == BFO_FAULTED && G.stream == &M);
    setup(4); snapshots(); F.pci = 0x5f6914e4;
    assert(bfo_map_close(&M) == BFO_OWNER); preserved(1, 0);
}
#else
static void default_no_io(void)
{
    objects_reset(); snapshots();
    struct apb_io io = {NULL, fake_read, fake_write, NULL, NULL, NULL};
    uint64_t pa = 0xdead; uint32_t v = 0xdead, irq = 0xbeef;
    assert(!bfo_fake_registry_compiled());
    assert(bfo_registry_open(&G, &io) == BFO_GATED && bfo_registry_close(&G) == BFO_GATED);
    assert(bfo_map_open(&G, &M, &POLICY, &STORAGE, 3) == BFO_GATED);
    assert(bfo_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFO_GATED);
    assert(bfo_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE, 1, &pa) == BFO_GATED);
    assert(bfo_map_close(&M) == BFO_GATED);
    assert(bfo_irq_open(&G, &Q, 3) == BFO_GATED && bfo_irq_mask(&Q, 0) == BFO_GATED);
    assert(bfo_irq_receive(&Q, PROFILE.address, 3) == BFO_GATED);
    assert(bfo_irq_dispatch(&Q, &v, &irq) == BFO_GATED && bfo_irq_close(&Q) == BFO_GATED);
    assert(!F.reads && pa == 0xdead && v == 0xdead && irq == 0xbeef); preserved(1, 0);
}
#endif
int main(void)
{
    assert(!apb_shared_writes_compiled() && !apl_shared_writes_compiled());
    assert(!apg_fake_drive_compiled() && !ars_fake_enable_compiled());
#ifdef BFO_FAKE_DEVICE
    assert(bfo_fake_registry_compiled());
    exclusive_lifecycle(); guards_and_fault_sweep(); refusals_and_corruption();
#else
    default_no_io();
#endif
    puts("HOST ONLY PASS: exclusive stream/vector, retained faults, guarded software cleanup; zero register writes");
    return 0;
}
