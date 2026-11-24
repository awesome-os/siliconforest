/* Original host-only composition test. No driver, firmware or hardware API. */
#include "bcm4378_transport.h"
#include "bcm4378_identity_fw.h"
#include "bcm4378_fake_dart.h"
#include "bcm4378_fake_irq.h"
#include "bcm4378_fake_ring.h"
#include "linkup.h"
#include "perst_gpio.h"
#include "radio_enable.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "pipeline tests require assertions"
#endif
#if defined(BCL_TEST_HOST) && defined(KERNEL)
#error "composition fixture is host-only"
#endif

#ifdef BCL_TEST_HOST
#include "register_accessor.h"
enum { SH = BCT_RAM_BASE+0x100, INFO = BCT_RAM_BASE+0x300,
       DESC = BCT_RAM_BASE+0x400, WI = BCT_RAM_BASE+0x500, RI = BCT_RAM_BASE+0x504 };
static const uint8_t ITEM[8] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
static const struct bct_plan TP = {0x10000, 0x18000000, 14, 0xfeed0000};
/* Deliberately synthetic: numeric chip fields remain unverified. */
static const struct bci_profile CP = {0x18000000,0xffff,0xf0000,16,0xf0000000,28,0x4378,1};
static const struct dld_range RAM[] = {{UINT64_C(0x800000000),0x2000000}};
static const struct dld_range RESERVED[] = {{UINT64_C(0x800080000),DLD_PAGE}};
static const struct dld_policy POLICY = {{DLD_APCIE_BASE,DLD_APCIE_SIZE},RAM,1,RESERVED,1};
static const struct bmi_profile MSI = {UINT64_C(0xfffff000),0x100,0,8,704};
static const uint64_t DATA_PA = UINT64_C(0x800100000);

/* Exactly one owner in this TEST context. No cross-object allocator exists in
 * the imported models. Even successful logical close retains all storage. */
static struct fixture {
    struct apr_accessor accessor;
    struct apb_io io;
    struct bct_session transport;
    struct bci_identity identity;
    struct bci_fw_plan firmware;
    struct bci_request_record request;
    struct bfd_map map;
    struct bfd_workspace workspace;
    struct bmi_source irq;
    struct bcr_ring ring;
    uint8_t root[DLD_PAGE], leaf[DLD_PAGE], buffer[32];
    uint32_t config[0x1000/4], bar0[0x8000/4], bar2[0x10000/4];
    uint32_t phy, run, link;
    unsigned calls, writes, endpoint_reads, bells, fw_calls;
    unsigned fail_at, apply_failed_write, reset_on_index;
    unsigned owner, complete, pa_checked;
} F;

static uint32_t *word(unsigned r, uint32_t off)
{
    assert(!(off & 3u));
    if (r == APR_BCM_CONFIG) { assert(off < sizeof F.config); return &F.config[off/4]; }
    if (r == APR_BCM_BAR0) { assert(off < sizeof F.bar0); return &F.bar0[off/4]; }
    assert(r == APR_BCM_BAR2 && off >= BCT_RAM_BASE && off-BCT_RAM_BASE < sizeof F.bar2);
    return &F.bar2[(off-BCT_RAM_BASE)/4];
}
static int read_fake(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    (void)ctx;
    if (++F.calls == F.fail_at) return -1;
    if (r == APB_PHY) { assert(off == 0); *out = F.phy; }
    else if (r == APB_PORT0) { assert(off == 0x804 || off == 0x208); *out = off == 0x804 ? F.run : F.link; }
    else { F.endpoint_reads++; *out = *word(r, off); }
    return 0;
}
static int write_fake(void *ctx, unsigned r, uint32_t off, uint32_t value)
{
    (void)ctx;
    assert(F.owner && F.phy == 0xf && F.run == 1 && F.link == 1);
    assert((r == APR_BCM_CONFIG && off == 0x80) ||
           (r == APR_BCM_BAR2 && (off == DESC+4 || off == DESC+8 || off == DESC+12 || off == WI)) ||
           (r == APR_BCM_BAR0 && (off == 0x2140 || off == 0x2a20)));
    int fail = ++F.calls == F.fail_at;
    F.writes++;
    if (!fail || F.apply_failed_write) {
        if (r == APR_BCM_BAR0) {
            assert(value == 1 && F.pa_checked && F.map.mapped);
            assert(F.ring.index_published == F.bells+1);
            assert((*word(APR_BCM_BAR2, WI) & 0xffffu) == F.ring.write_index);
            assert(!memcmp(F.buffer+((F.ring.write_index+3)%4)*8, ITEM, 8));
            F.bells++;
        }
        *word(r, off) = value;
        if (F.reset_on_index && r == APR_BCM_BAR2 && off == WI) F.phy = 0x80;
    }
    return fail ? -1 : 0;
}
static void fresh(unsigned dar)
{
    memset(&F, 0, sizeof F);
    memset(F.root, 0xa5, sizeof F.root); memset(F.leaf, 0x5a, sizeof F.leaf);
    memset(F.buffer, 0xcc, sizeof F.buffer);
    F.phy = 0xf; F.run = F.link = 1;
    F.config[0] = 0x442514e4; F.bar0[0] = 0x10054378; /* fabricated revision5 */
    *word(APR_BCM_BAR2, BCT_RAM_BASE+TP.ram_bytes-4) = SH; /* fabricated FW handoff */
    *word(APR_BCM_BAR2, SH) = 0x02000007u | (dar ? 0x80000000u : 0);
    *word(APR_BCM_BAR2, SH+40) = BCT_RAM_BASE+0x200;
    *word(APR_BCM_BAR2, SH+44) = BCT_RAM_BASE+0x204;
    *word(APR_BCM_BAR2, SH+48) = INFO;
    *word(APR_BCM_BAR2, INFO) = DESC;
    *word(APR_BCM_BAR2, INFO+4) = WI; *word(APR_BCM_BAR2, INFO+8) = RI;
    *word(APR_BCM_BAR2, WI) = 0xface0000; *word(APR_BCM_BAR2, RI) = 0xbeef0000;
    struct apb_io raw = {NULL,read_fake,write_fake,NULL,NULL,NULL};
    assert(apr_bind_bcm_fake(&F.accessor, &raw, &F.io) == APB_OK);
}
/* TEST-only reachability guard precedes endpoint operations. This does not
 * implement a hardware reachability or PMGR policy; it reads ordinary RAM. */
static int preflight(void)
{
    uint32_t v = 0;
    if (F.io.read32(F.io.context, APB_PHY, 0, &v)) return -900;
    return (v & 0x8fu) == 0x0fu ? 0 : -901;
}
static int request_fake(void *ctx, const char *name, unsigned item, int required)
{
    (void)ctx;
    F.fw_calls++;
    assert(item < BCI_ITEM_COUNT && required == (item == BCI_EXECUTABLE));
    assert(!strchr(name, '/') && strstr(name, "brcmfmac4378b3-pcie") == name);
    if (item == BCI_EXECUTABLE && !strcmp(name, "brcmfmac4378b3-pcie.bin")) return 0;
    return 1; /* Metadata only. No firmware payload, files or download. */
}
#define STEP(expr) do { int e_ = (expr); if (e_) return e_; } while (0)
static int prepare(void)
{
    if (F.owner) return -902;
    F.owner = 1;
    STEP(preflight());
    STEP(bct_init(&F.transport, &F.io, &TP));
    STEP(bct_identify(&F.transport)); STEP(bct_select_window(&F.transport));
    STEP(bci_identify_fake(&F.io, &CP, &F.identity));
    assert(F.identity.valid && F.identity.revision == 5 && !F.identity.numeric_profile_verified);
    const char *qualifiers[] = {"synthetic-test"};
    STEP(bci_build_fw_plan(&F.identity, qualifiers, 1, &F.firmware));
    STEP(bci_request_fw_item(&F.io, &F.firmware, BCI_EXECUTABLE, request_fake, NULL, &F.request));
    assert(F.request.found && F.request.attempts == 2);
    int e = bci_request_fw_item(&F.io, &F.firmware, BCI_BOARD_NVRAM, request_fake, NULL, &F.request);
    if (e != BCI_OPTIONAL_MISSING) return e ? e : -903;
    assert(F.request.optional_missing && !F.request.found);
    struct bfd_storage storage = {F.root,F.leaf,DLD_PAGE,DLD_PAGE,
                                  UINT64_C(0x800000000),UINT64_C(0x800004000)};
    STEP(bfd_init(&F.map, &F.io, &POLICY, &storage, 0x100, 3));
    STEP(bfd_map_pages(&F.map, &F.workspace, DLD_APCIE_BASE, DATA_PA, DLD_PAGE, DLD_READ));
    uint64_t pa = 0;
    STEP(bfd_translate(&F.map, &F.workspace, 0x100, 3, DLD_APCIE_BASE, DLD_READ, &pa));
    assert(pa == DATA_PA); F.pa_checked = 1;
    STEP(bmi_open(&F.irq, &F.io, &MSI)); STEP(bmi_arm(&F.irq, 1));
    STEP(bmi_set_mask(&F.irq, 0, 0));
    STEP(bct_discover_shared(&F.transport));
    assert(F.transport.phase == BCT_SHARED);
    struct bcr_plan plan = {F.buffer,sizeof F.buffer,4,8,DLD_APCIE_BASE};
    STEP(bcr_open(&F.ring, &F.transport, &plan)); STEP(bcr_configure(&F.ring));
    assert(*word(APR_BCM_BAR2, DESC+8) == DLD_APCIE_BASE && *word(APR_BCM_BAR2, DESC+12) == 0);
    return 0;
}
static int finish(void)
{
    uint32_t vector = UINT32_MAX, aic = UINT32_MAX;
    STEP(bcr_submit(&F.ring, ITEM, sizeof ITEM));
    assert(F.bells == 1 && !memcmp(F.buffer, ITEM, sizeof ITEM));
    assert(*word(APR_BCM_BAR2, WI) == 0xface0001);
    /* Explicit test action, NOT device DMA or proof of completion. */
    *word(APR_BCM_BAR2, RI) = 0xbeef0001;
    STEP(bmi_receive(&F.irq, MSI.address, 0));
    STEP(bmi_dispatch_one(&F.irq, &vector, &aic));
    assert(vector == 0 && aic == 704 && F.irq.delivered == 1 && !F.irq.pending);
    STEP(bcr_close(&F.ring)); STEP(bmi_set_mask(&F.irq, 0, 1)); STEP(bmi_close(&F.irq));
    assert(F.ring.closed && F.irq.closed && F.map.mapped && F.owner);
    F.complete = 1; /* Logical close only. No DART unmap or storage release. */
    return 0;
}
static void healthy(void)
{
    for (unsigned dar = 0; dar < 2; ++dar) {
        fresh(dar); assert(prepare() == 0);
        unsigned calls = F.calls; assert(prepare() == -902 && F.calls == calls);
        assert(finish() == 0 && F.complete && F.owner);
        calls = F.calls; assert(prepare() == -902 && F.calls == calls);
        assert(F.ring.bell_offset == (dar ? 0x2a20u : 0x2140u));
        assert(F.fw_calls == 4 && F.map.table_commits == 1 && F.map.mapped_pages == 1);
    }
}
static void cold_and_wrong_owner(void)
{
    fresh(0); F.phy = 0x80;
    assert(prepare() == -901 && F.calls == 1 && !F.endpoint_reads && !F.writes && !F.fw_calls);
    assert(!F.transport.phase && !F.map.bound && !F.irq.bound && !F.ring.bound && !F.complete);
    for (unsigned i = 0; i < sizeof F.root; ++i) assert(F.root[i] == 0xa5 && F.leaf[i] == 0x5a);
    fresh(0); assert(prepare() == 0);
    uint64_t pa = UINT64_MAX;
    assert(bfd_translate(&F.map, &F.workspace, 0x100, 4, DLD_APCIE_BASE, DLD_READ, &pa) == BFD_OWNER);
    assert(pa == UINT64_MAX);
    assert(bfd_translate(&F.map, &F.workspace, 0x100, 3, DLD_APCIE_BASE, DLD_WRITE, &pa) == DLD_PERMISSION);
    assert(pa == UINT64_MAX);
    assert(bmi_receive(&F.irq, MSI.address, 1) == BMI_OWNER && !F.irq.pending);
    assert(finish() == 0);
}
static void unresolved_cleanup(void)
{
    fresh(0); assert(prepare() == 0); F.reset_on_index = 1;
    assert(bcr_submit(&F.ring, ITEM, sizeof ITEM) == BCR_PHY_NOT_READY);
    assert(F.ring.faulted && F.ring.dirty && F.ring.inflight && !F.complete && !F.bells);
    assert(*word(APR_BCM_BAR2, WI) == 0xface0001 && !memcmp(F.buffer, ITEM, sizeof ITEM));
    unsigned calls = F.calls, writes = F.writes;
    assert(bcr_submit(&F.ring, ITEM, sizeof ITEM) == BCR_FAULTED && F.calls == calls);
    assert(bmi_close(&F.irq) == BMI_PHY_NOT_READY && !F.irq.closed);
    assert(F.writes == writes && F.map.mapped && F.owner && !F.ring.closed);
    fresh(0); assert(prepare() == 0);
    assert(bmi_receive(&F.irq, MSI.address, 0) == 0);
    assert(bmi_set_mask(&F.irq, 0, 1) == 0);
    assert(bmi_close(&F.irq) == BMI_BUSY && F.irq.pending == 1 && !F.irq.closed);
    assert(!F.complete && F.owner && F.map.mapped);
}
static void every_callback_fault(void)
{
    fresh(0); assert(prepare() == 0 && finish() == 0);
    unsigned total = F.calls;
    for (unsigned applied = 0; applied < 2; ++applied) for (unsigned n = 1; n <= total; ++n) {
        fresh(0); F.fail_at = n; F.apply_failed_write = applied;
        int e = prepare(); if (!e) e = finish();
        assert(e != 0 && !F.complete && F.owner && F.calls == n);
        if (F.ring.faulted) {
            unsigned calls = F.calls;
            assert(bcr_submit(&F.ring, ITEM, sizeof ITEM) == BCR_FAULTED && F.calls == calls);
        }
        /* No automatic retry, descriptor rollback, unmap or release. */
    }
    printf("HOST composition callback boundaries=%u failure_cases=%u\n", total, total*2);
}
#else
static int forbidden_read(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{ (void)ctx;(void)r;(void)off;(void)out; assert(!"gated path must not read"); return -1; }
static void gated(void)
{
    struct apb_io io = {NULL,forbidden_read,NULL,NULL,NULL,NULL};
    struct bct_session t = {0}; struct bci_identity id = {0};
    struct bfd_map map = {0}; struct bmi_source irq = {0}; struct bcr_ring ring = {0};
    assert(!bct_fake_transport_compiled() && !bfd_fake_map_compiled() && !bmi_fake_source_compiled() && !bcr_fake_ring_compiled());
    assert(bct_init(&t, &io, NULL) == BCT_GATED);
    assert(bci_identify_fake(&io, NULL, &id) == BCI_GATED);
    assert(bfd_init(&map, &io, NULL, NULL, 0, 0) == BFD_GATED);
    assert(bmi_open(&irq, &io, NULL) == BMI_GATED);
    assert(bcr_open(&ring, &t, NULL) == BCR_GATED);
    assert(!t.phase && !id.valid && !map.bound && !irq.bound && !ring.bound);
}
#endif
int main(void)
{
    assert(!apb_shared_writes_compiled() && !apl_shared_writes_compiled());
    assert(!apg_fake_drive_compiled() && !ars_fake_enable_compiled());
#ifdef BCL_TEST_HOST
    healthy(); cold_and_wrong_owner(); unresolved_cleanup(); every_callback_fault();
#else
    gated();
#endif
    puts("HOST ONLY PASS: five-model composition; no chip, firmware, DMA, MSI or hardware proof");
    return 0;
}
