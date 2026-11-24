#include "bcm4378_fake_dart.h"
#include "linkup.h"
#include "perst_gpio.h"
#include "radio_enable.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "test_bcm4378_fake_dart requires assertions"
#endif

static uint8_t root_bytes[DLD_PAGE], leaf_bytes[DLD_PAGE], saved_root[DLD_PAGE], saved_leaf[DLD_PAGE];
static struct bfd_workspace W, SAVED_W;
static struct bfd_map M;
static const struct dld_range RAM[] = {{UINT64_C(0x800000000), 0x2000000}};
static const struct dld_range RESERVED[] = {{UINT64_C(0x800080000), DLD_PAGE}};
static const struct dld_policy POLICY = {{DLD_APCIE_BASE, DLD_APCIE_SIZE}, RAM, 1, RESERVED, 1};
static const struct bfd_storage STORAGE = {root_bytes, leaf_bytes, DLD_PAGE, DLD_PAGE,
    UINT64_C(0x800000000), UINT64_C(0x800004000)};
static const uint64_t DATA = UINT64_C(0x800100000);
static struct {
    uint32_t run, link, identity, phy;
    unsigned reads, writes, fail_at, drop_after_read;
} F;

static int fake_read(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    (void)ctx;
    ++F.reads;
    if (F.reads == F.fail_at) { *out = UINT32_MAX; return -1; }
    if (r == APB_PORT0 && off == 0x804) *out = F.run;
    else if (r == APB_PORT0 && off == 0x208) *out = F.link;
    else { assert(r == 9 && off == 0); *out = F.identity; }
    if (F.reads == F.drop_after_read) F.link = 0;
    return 0;
}
static int fake_write(void *ctx, unsigned r, uint32_t off, uint32_t value)
{
    (void)ctx;(void)r;(void)off;(void)value;
    ++F.writes;
    assert(!"no register write is part of a software fake DART map");
    return -1;
}
static void reset(void)
{
    memset(&F, 0, sizeof F); F.run = F.link = 1; F.identity = 0x442514e4;
    F.phy = 0x80; /* Intentionally stays in reset even in synthetic LINK_UP fixture. */
    memset(&M, 0, sizeof M); memset(&W, 0xcc, sizeof W);
    memset(root_bytes, 0xa5, sizeof root_bytes); memset(leaf_bytes, 0x5a, sizeof leaf_bytes);
}
static void save(void)
{ memcpy(saved_root, root_bytes, sizeof root_bytes); memcpy(saved_leaf, leaf_bytes, sizeof leaf_bytes); SAVED_W = W; }
static void unchanged(int workspace)
{
    assert(!memcmp(saved_root, root_bytes, sizeof root_bytes));
    assert(!memcmp(saved_leaf, leaf_bytes, sizeof leaf_bytes));
    if (workspace) assert(!memcmp(&SAVED_W, &W, sizeof W));
    assert(F.phy == 0x80 && F.writes == 0);
}

#ifdef BFD_FAKE_DEVICE
#include "register_accessor.h"
static struct apr_accessor A;
static struct apb_io IO;
static void bind(void)
{
    struct apb_io raw = {NULL, fake_read, fake_write, NULL, NULL, NULL};
    assert(apr_bind_bcm_fake(&A, &raw, &IO) == APB_OK);
}
static void init(void)
{ reset(); bind(); assert(bfd_init(&M, &IO, &POLICY, &STORAGE, 0x100, 3) == BFD_FAKE_OK); }

static void map_and_translate(void)
{
    uint64_t result;
    for (unsigned access = 1; access <= 3; ++access) {
        init();
        assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, 4*DLD_PAGE, access) == BFD_FAKE_OK);
        assert(M.mapped && M.mapped_pages == 4 && M.table_commits == 1);
        for (unsigned n = 0; n < 4; ++n) {
            result = 0xdead;
            assert(bfd_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE+n*DLD_PAGE+0x123, access, &result) == DLD_OK);
            assert(result == DATA+n*DLD_PAGE+0x123);
        }
        save(); result = 0xdead;
        assert(bfd_translate(&M, &W, 0x101, 3, DLD_APCIE_BASE, access, &result) == BFD_OWNER && result == 0xdead);
        assert(bfd_translate(&M, &W, 0x100, 2, DLD_APCIE_BASE, access, &result) == BFD_OWNER && result == 0xdead);
        assert(bfd_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE+4*DLD_PAGE, access, &result) == DLD_UNMAPPED && result == 0xdead);
        if (access != 3) {
            assert(bfd_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE, access == 1 ? 2 : 1, &result) == DLD_PERMISSION);
            assert(result == 0xdead);
        }
        assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFD_STATE);
        unchanged(0);
    }
    init();
    uint64_t last = DLD_APCIE_BASE + DLD_APCIE_SIZE - DLD_PAGE;
    assert(bfd_map_pages(&M, &W, last, DATA, DLD_PAGE, 3) == BFD_FAKE_OK);
    assert(bfd_translate(&M, &W, 0x100, 3, last+DLD_PAGE-1, 1, &result) == DLD_OK && result == DATA+DLD_PAGE-1);
    init();
    assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_MAX_LEAVES*DLD_PAGE, 3) == BFD_FAKE_OK);
    assert(M.mapped_pages == 256 && F.phy == 0x80 && F.writes == 0);
}

static void down_link_zero_writes(void)
{
    for (unsigned which = 0; which < 4; ++which) {
        reset(); bind(); save();
        if (which == 0) F.run = 0; else F.link = which == 1 ? 0 : which == 2 ? 5 : 0x41;
        assert(bfd_init(&M, &IO, &POLICY, &STORAGE, 0x100, 3) == BFD_LINK_DOWN);
        assert(!M.bound); unchanged(1);
        init(); save();
        if (which == 0) F.run = 0; else F.link = which == 1 ? 0 : which == 2 ? 5 : 0x41;
        assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFD_LINK_DOWN);
        assert(!M.mapped && M.table_commits == 0); unchanged(1);
        init(); assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFD_FAKE_OK); save();
        if (which == 0) F.run = 0; else F.link = which == 1 ? 0 : which == 2 ? 5 : 0x41;
        uint64_t result = 0xfeed;
        assert(bfd_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE, 1, &result) == BFD_LINK_DOWN);
        assert(result == 0xfeed && M.table_commits == 1); unchanged(1);
    }
    init(); save(); F.drop_after_read = F.reads + 3;
    assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFD_LINK_DOWN);
    assert(!M.mapped && M.table_commits == 0); unchanged(0); /* staging is private/inactive */
}

static void malformed_ranges_and_aliases(void)
{
    const struct { uint64_t iova, pa, bytes; unsigned permissions; } bad[] = {
        {DLD_APCIE_BASE, DATA, 0, 3}, {DLD_APCIE_BASE+1, DATA, DLD_PAGE, 3},
        {DLD_APCIE_BASE, DATA+1, DLD_PAGE, 3}, {DLD_APCIE_BASE, DATA, DLD_PAGE+1, 3},
        {DLD_APCIE_BASE, DATA, (DLD_MAX_LEAVES+1)*DLD_PAGE, 3},
        {DLD_MSI_PAGE, DATA, DLD_PAGE, 3}, {DLD_IOVA_LIMIT, DATA, DLD_PAGE, 3},
        {DLD_APCIE_BASE-DLD_PAGE, DATA, DLD_PAGE, 3},
        {DLD_APCIE_BASE, DLD_PA_LIMIT, DLD_PAGE, 3},
        {0x1ffc000, DATA, 2*DLD_PAGE, 3}, {DLD_APCIE_BASE, DATA, DLD_PAGE, 0},
        {DLD_APCIE_BASE, DATA, DLD_PAGE, 4},
        {DLD_APCIE_BASE, UINT64_C(0x800080000), DLD_PAGE, 3},
        {DLD_APCIE_BASE, STORAGE.root_pa, DLD_PAGE, 3},
        {DLD_APCIE_BASE, STORAGE.leaf_pa, DLD_PAGE, 3},
    };
    for (unsigned i = 0; i < sizeof bad/sizeof bad[0]; ++i) {
        init(); save();
        assert(bfd_map_pages(&M, &W, bad[i].iova, bad[i].pa, bad[i].bytes, bad[i].permissions) != BFD_FAKE_OK);
        assert(!M.mapped && M.table_commits == 0); unchanged(0);
    }
    init(); save();
    assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, (DLD_MAX_LEAVES+1)*DLD_PAGE, 3) == BFD_LIMIT);
    unchanged(1); /* Reject the work budget before staging, not just at validation. */
    reset(); bind();
    assert(bfd_init(&M, &IO, &POLICY, &STORAGE, 0x101, 3) == BFD_OWNER);
    assert(bfd_init(&M, &IO, &POLICY, &STORAGE, 0x100, 16) == BFD_OWNER);
    struct bfd_storage badstore = STORAGE; badstore.leaf_pa = badstore.root_pa;
    assert(bfd_init(&M, &IO, &POLICY, &badstore, 0x100, 3) == DLD_OVERLAP);
    badstore = STORAGE; badstore.leaf = badstore.root;
    assert(bfd_init(&M, &IO, &POLICY, &badstore, 0x100, 3) == DLD_OVERLAP);
    struct dld_policy p = POLICY; p.aperture.size += DLD_PAGE;
    assert(bfd_init(&M, &IO, &p, &STORAGE, 0x100, 3) == DLD_RANGE);
    init(); uint64_t result = 0xcafe;
    assert(bfd_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE, 1, &result) == DLD_UNMAPPED && result == 0xcafe);
    assert(bfd_map_pages(&M, (struct bfd_workspace *)root_bytes, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == DLD_OVERLAP);
    assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFD_FAKE_OK);
    leaf_bytes[64*8+1] |= 2; /* reserved PTE bit9 */
    assert(bfd_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE, 1, &result) == DLD_ENCODING && result == 0xcafe);
}

static void callback_errors_and_endpoint_change(void)
{
    for (unsigned fail = 1; fail <= 9; ++fail) {
        reset(); bind(); save(); F.fail_at = fail;
        int e = bfd_init(&M, &IO, &POLICY, &STORAGE, 0x100, 3);
        if (!e) e = bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3);
        assert(e == BFD_IO && F.reads == fail && !M.mapped); unchanged(0);
        if (M.bound) {
            unsigned before = F.reads;
            assert(M.faulted && bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFD_STATE);
            assert(F.reads == before);
        }
    }
    for (unsigned fail = 1; fail <= 3; ++fail) {
        init(); assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFD_FAKE_OK); save();
        F.fail_at = F.reads + fail;
        uint64_t result = 0xfeed;
        assert(bfd_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE, 1, &result) == BFD_IO && result == 0xfeed);
        unchanged(1);
    }
    init(); save(); F.identity = 0x5f6914e4;
    assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFD_OWNER);
    unchanged(1);
}
#else
static void default_zero_io(void)
{
    reset(); save();
    struct apb_io io = {NULL, fake_read, fake_write, NULL, NULL, NULL};
    uint64_t result = 0xfeed;
    assert(!bfd_fake_map_compiled());
    assert(bfd_init(&M, &io, &POLICY, &STORAGE, 0x100, 3) == BFD_GATED);
    assert(bfd_map_pages(&M, &W, DLD_APCIE_BASE, DATA, DLD_PAGE, 3) == BFD_GATED);
    assert(bfd_translate(&M, &W, 0x100, 3, DLD_APCIE_BASE, 1, &result) == BFD_GATED && result == 0xfeed);
    assert(!F.reads && !M.bound && !M.mapped && !M.table_commits); unchanged(1);
}
#endif
int main(void)
{
    assert(!apb_shared_writes_compiled() && !apl_shared_writes_compiled());
    assert(!apg_fake_drive_compiled() && !ars_fake_enable_compiled());
#ifdef BFD_FAKE_DEVICE
    assert(bfd_fake_map_compiled());
    map_and_translate(); down_link_zero_writes(); malformed_ranges_and_aliases(); callback_errors_and_endpoint_change();
#else
    default_zero_io();
#endif
    puts("HOST ONLY PASS: BCM4378 fake DART pages/lookup; PHY remains reset, zero register writes");
    return 0;
}
