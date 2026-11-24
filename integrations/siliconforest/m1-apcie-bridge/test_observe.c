/* Host-only tests for observe.c (no hardware). Build:
 * cc -std=c11 -Wall -Wextra -Werror -I. -I../m1-dart-survey -fsanitize=address,undefined
 *    observe.c test_observe.c && ./a.out */
#include "observe_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error test_observe requires assertions
#endif

#define ON 0x000000ffu            /* TARGET=ACTUAL=0xf, nothing else */
#define PHY_READY 0x0000000fu     /* REQ0/1 + ACK0/1, reset clear */
#define PHY_RESET 0x0000008fu     /* clocks acked but reset bit7 asserted */

struct fake {
    uint32_t pmgr[4], pmgr_late[4], phy, phyif, appclk, reset, status, linksts;
    int late_after;               /* PMGR reads after this count return pmgr_late */
    int fail_at;                  /* 1-based read index that returns an error; 0 none */
    int pmgr_reads, count;
    struct { unsigned r; uint32_t off; } log[32];
};

static int fake_read(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    struct fake *f = ctx;
    static const uint32_t po[4] = {0x1b8, 0x348, 0x3e8, 0x1a0};
    assert(f->count < 32);
    f->log[f->count].r = r; f->log[f->count].off = off;
    f->count++;
    if (f->fail_at && f->count == f->fail_at) return -1;
    if (r == DLS_PMGR) {
        unsigned i;
        f->pmgr_reads++;
        for (i = 0; i < 4; i++) if (off == po[i]) {
            *out = (f->late_after && f->pmgr_reads > f->late_after) ? f->pmgr_late[i] : f->pmgr[i];
            return 0;
        }
    }
    if (r == DLS_PHY && off == 0) { *out = f->phy; return 0; }
    if (r == DLS_RC && off == 0x24) { *out = f->phyif; return 0; }
    if (r == DLS_PORT) {
        if (off == 0x800) { *out = f->appclk; return 0; }
        if (off == 0x814) { *out = f->reset; return 0; }
        if (off == 0x804) { *out = f->status; return 0; }
        if (off == 0x208) { *out = f->linksts; return 0; }
    }
    assert(!"read outside the whitelist reached the device");
    return -1;
}

static const struct dls_board BOARD = {
    {UINT64_C(0x23b700000), UINT64_C(0x680000000), UINT64_C(0x680080000), UINT64_C(0x681000000),
     UINT64_C(0x681008000)},
    {0x4000, 0x4000, 0x4000, 0x4000, 0x4000}};

static struct fake ready(void)
{
    struct fake f;
    memset(&f, 0, sizeof f);
    for (int i = 0; i < 4; i++) f.pmgr[i] = ON;
    f.phy = PHY_READY; f.phyif = 1; f.appclk = 1; f.reset = 1; f.status = 1; f.linksts = 0x1;
    return f;
}
static int run(struct fake *f, struct apo_snapshot *s)
{
    struct dls_io io = {f, fake_read};
    return apo_observe(&BOARD, &io, s);
}
static int no_reads_in(const struct fake *f, unsigned region)
{
    for (int i = 0; i < f->count; i++) if (f->log[i].r == region) return 0;
    return 1;
}

static void whitelist(void)
{
    static const uint32_t pmgr_ok[4] = {0x1b8, 0x348, 0x3e8, 0x1a0};
    for (int i = 0; i < 4; i++) assert(apo_read_allowed(DLS_PMGR, pmgr_ok[i]));
    assert(apo_read_allowed(DLS_PHY, 0) && apo_read_allowed(DLS_RC, 0x24));
    assert(apo_read_allowed(DLS_PORT, 0x800) && apo_read_allowed(DLS_PORT, 0x814));
    assert(apo_read_allowed(DLS_PORT, 0x804) && apo_read_allowed(DLS_PORT, 0x208));
    unsigned allowed = 0;
    for (unsigned r = 0; r <= DLS_REGIONS; r++)
        for (uint32_t off = 0; off < 0x4000; off++) allowed += (unsigned)apo_read_allowed(r, off);
    assert(allowed == 10);                                /* exactly the ten pairs */
    for (uint32_t off = 0; off < 0x4000; off += 4) assert(!apo_read_allowed(DLS_DART, off));
    assert(!apo_read_allowed(DLS_PORT, 0x80a) && !apo_read_allowed(DLS_PHY, 4));
}

static void power_not_ready_stops_everything(void)
{
    struct fake f = ready();
    struct apo_snapshot s;
    f.pmgr[1] = 0x000000f4;                             /* APCIE target on, actual off */
    assert(run(&f, &s) == APO_POWER_NOT_READY);
    assert(f.count == 2 && s.reads_attempted == 2 && s.stage_reached == APO_STAGE_POWER);
    assert(s.valid_mask == (APO_V_POWER(0) | APO_V_POWER(1)));
    assert(no_reads_in(&f, DLS_PHY) && no_reads_in(&f, DLS_RC) && no_reads_in(&f, DLS_PORT));
    f = ready(); f.pmgr[3] = 0x100000ffu;               /* reset bit set */
    assert(run(&f, &s) == APO_POWER_NOT_READY && f.count == 4);
    f = ready(); f.pmgr[0] = 0x000004ffu;               /* PS reset bit10 */
    assert(run(&f, &s) == APO_POWER_NOT_READY && f.count == 1);
    f = ready(); f.pmgr[2] = 0x800000ffu;               /* device disable bit31 */
    assert(run(&f, &s) == APO_POWER_NOT_READY && f.count == 3);
    f = ready(); f.pmgr[2] = 0x000003ffu;               /* historical WAS-gated bits8/9 allowed */
    assert(run(&f, &s) == APO_OBSERVED);
}

static void phy_reset_reads_no_rc_or_port(void)
{
    struct fake f = ready();
    struct apo_snapshot s;
    f.phy = PHY_RESET;                                  /* the state DART survey 001 saw */
    assert(run(&f, &s) == APO_PHY_NOT_READY);
    assert(f.count == 9 && s.reads_attempted == 9);     /* 4 power + PHY + 4 final */
    assert(no_reads_in(&f, DLS_RC) && no_reads_in(&f, DLS_PORT));
    assert(s.phy_ctrl == PHY_RESET && (s.valid_mask & APO_V_PHY_CTRL));
    assert(!(s.valid_mask & (APO_V_RC_PHYIF | APO_V_PORT_APPCLK | APO_V_PORT_LINKSTS)));
    assert((s.valid_mask & 0x3c00u) == 0x3c00u);         /* final power words read */
    f = ready(); f.phy = 0x0000000bu;                   /* CLK1ACK missing */
    assert(run(&f, &s) == APO_PHY_NOT_READY && no_reads_in(&f, DLS_RC));
}

static void phyif_clear_reads_no_port(void)
{
    struct fake f = ready();
    struct apo_snapshot s;
    f.phyif = 0;
    assert(run(&f, &s) == APO_PHYIF_NOT_RUNNING);
    assert(f.count == 10 && no_reads_in(&f, DLS_PORT));
    assert(f.log[5].r == DLS_RC && f.log[5].off == 0x24);
}

static void port_not_running_skips_linksts(void)
{
    struct fake f = ready();
    struct apo_snapshot s;
    f.status = 0; f.appclk = 0; f.reset = 0;            /* APPCLK off, PERST asserted */
    assert(run(&f, &s) == APO_PORT_NOT_RUNNING);
    assert(f.count == 13);
    for (int i = 0; i < f.count; i++) assert(!(f.log[i].r == DLS_PORT && f.log[i].off == 0x208));
    assert(f.log[6].off == 0x800 && f.log[7].off == 0x814 && f.log[8].off == 0x804);
    assert((s.valid_mask & (APO_V_PORT_APPCLK | APO_V_PORT_RESET | APO_V_PORT_STATUS)) ==
           (APO_V_PORT_APPCLK | APO_V_PORT_RESET | APO_V_PORT_STATUS));
    assert(!(s.valid_mask & APO_V_PORT_LINKSTS) && s.port0_reset == 0);
}

static void full_observation_order(void)
{
    struct fake f = ready();
    struct apo_snapshot s;
    static const unsigned rr[14] = {DLS_PMGR, DLS_PMGR, DLS_PMGR, DLS_PMGR, DLS_PHY, DLS_RC,
                                    DLS_PORT, DLS_PORT, DLS_PORT, DLS_PORT,
                                    DLS_PMGR, DLS_PMGR, DLS_PMGR, DLS_PMGR};
    static const uint32_t ro[14] = {0x1b8, 0x348, 0x3e8, 0x1a0, 0, 0x24, 0x800, 0x814, 0x804,
                                    0x208, 0x1b8, 0x348, 0x3e8, 0x1a0};
    f.linksts = 0x45;                                   /* UP + BUSY + L2, raw kept */
    assert(run(&f, &s) == APO_OBSERVED);
    assert(f.count == 14 && s.reads_attempted == APO_MAX_READS);
    for (int i = 0; i < 14; i++) assert(f.log[i].r == rr[i] && f.log[i].off == ro[i]);
    assert(s.valid_mask == 0x3fffu && s.port0_linksts == 0x45);
    assert(s.stage_reached == APO_STAGE_LINK && s.result == APO_OBSERVED && s.version == 1);
}

static void power_lost_and_io(void)
{
    struct fake f = ready();
    struct apo_snapshot s;
    f.late_after = 4;
    for (int i = 0; i < 4; i++) f.pmgr_late[i] = ON;
    f.pmgr_late[1] = 0x000000f0u;                       /* domain dropped during observation */
    assert(run(&f, &s) == APO_POWER_LOST);
    assert(s.port0_linksts == 1 && (s.valid_mask & APO_V_PORT_LINKSTS));   /* kept */
    f = ready(); f.fail_at = 6;                         /* RC PHYIF read faults */
    assert(run(&f, &s) == APO_IO && f.count == 6 && s.last_region == DLS_RC);
    f = ready(); f.fail_at = 1;
    assert(run(&f, &s) == APO_IO && f.count == 1 && s.valid_mask == 0);
}

static void arguments_and_board(void)
{
    struct fake f = ready();
    struct apo_snapshot s;
    struct dls_io io = {&f, fake_read}, bad_io = {&f, NULL};
    struct dls_board b = BOARD;
    assert(apo_observe(&BOARD, &io, NULL) == APO_ARGUMENT);
    assert(apo_observe(NULL, &io, &s) == APO_ARGUMENT && s.result == APO_ARGUMENT);
    assert(apo_observe(&BOARD, &bad_io, &s) == APO_ARGUMENT);
    b.base[DLS_PORT] += 0x8000;
    assert(apo_observe(&b, &io, &s) == APO_BOARD && f.count == 0);
    b = BOARD; b.size[DLS_PHY] = 0x8000;
    assert(apo_observe(&b, &io, &s) == APO_BOARD && f.count == 0);
    b = BOARD; b.base[DLS_DART] = 0;                    /* DART base is not used here */
    assert(apo_observe(&b, &io, &s) == APO_OBSERVED);
}

/* ADT callbacks for apo_validate_bridge. */
struct adt { const char *bridge_compat; size_t bridge_len; int perst; const char *wlan; size_t wlan_len; int bridge1; };
static int adt_get(void *ctx, const char *path, const char *key, const uint8_t **p, size_t *n)
{
    struct adt *a = ctx;
    static const uint8_t one[4] = {1, 0, 0, 0};
    if (!strcmp(path, "/arm-io/apcie/pci-bridge0") && !strcmp(key, "compatible") && a->bridge_compat) {
        *p = (const uint8_t *)a->bridge_compat; *n = a->bridge_len; return 0;
    }
    if (!strcmp(path, "/arm-io/apcie/pci-bridge0") && !strcmp(key, "function-perst") && a->perst) {
        *p = one; *n = sizeof one; return 0;
    }
    if (!strcmp(path, "/arm-io/apcie/pci-bridge0/wlan") && !strcmp(key, "compatible") && a->wlan) {
        *p = (const uint8_t *)a->wlan; *n = a->wlan_len; return 0;
    }
    if (!strcmp(path, "/arm-io/apcie/pci-bridge1") && !strcmp(key, "name") && a->bridge1) {
        *p = (const uint8_t *)"pci-bridge1"; *n = 12; return 0;
    }
    return -1;
}
static void bridge_binding(void)
{
    static const char bridge[] = "apcie-bridge", wlan[] = "wlan-pcie,bcm4378\0wlan-pcie,bcm";
    struct adt good = {bridge, sizeof bridge, 1, wlan, sizeof wlan, 0}, a;
    assert(sizeof bridge == 13 && sizeof wlan == 32);
    assert(apo_validate_bridge(adt_get, &good) == APO_OBSERVED);
    a = good; a.bridge1 = 1;                            /* a second port would change scope */
    assert(apo_validate_bridge(adt_get, &a) == APO_BOARD);
    a = good; a.perst = 0;
    assert(apo_validate_bridge(adt_get, &a) == APO_BOARD);
    a = good; a.wlan = "wlan-pcie,bcm4387\0wlan-pcie,bcm"; a.wlan_len = 32;
    assert(apo_validate_bridge(adt_get, &a) == APO_BOARD);
    a = good; a.wlan_len = 18;                          /* first string only */
    assert(apo_validate_bridge(adt_get, &a) == APO_BOARD);
    a = good; a.bridge_len = 12;                        /* missing NUL */
    assert(apo_validate_bridge(adt_get, &a) == APO_BOARD);
    assert(apo_validate_bridge(NULL, &good) == APO_ARGUMENT);
}

int main(void)
{
    whitelist();
    power_not_ready_stops_everything();
    phy_reset_reads_no_rc_or_port();
    phyif_clear_reads_no_port();
    port_not_running_skips_linksts();
    full_observation_order();
    power_lost_and_io();
    arguments_and_board();
    bridge_binding();
    puts("HOST ONLY PASS: whitelist, staged guard order, stop points, final power, IO, board, port-0 binding");
    return 0;
}
