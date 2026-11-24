/* Existing simulated register arrays and callbacks, not a new MMIO backend. */
#ifdef NDEBUG
#error "test_readonly_accessor requires assertions"
#endif
#define main existing_linkup_test_main
#include "test_linkup.c"
#undef main
#include "readonly_accessor.h"

static const struct dls_board OBSERVER_BOARD = {
    {UINT64_C(0x23b700000), UINT64_C(0x680000000), UINT64_C(0x680080000),
     UINT64_C(0x681000000), UINT64_C(0x681008000)},
    {0x4000, 0x4000, 0x4000, 0x4000, 0x4000}};

static struct {
    unsigned count, fail_at, power_reads;
    int lose_power;
    struct { unsigned region; uint32_t offset; } log[32];
} R;

static int recorded_fake_read(void *context, unsigned region, uint32_t offset, uint32_t *value)
{
    (void)context;
    assert(R.count < 32);
    R.log[R.count].region = region; R.log[R.count].offset = offset;
    ++R.count;
    if (R.fail_at == R.count) { *value = 0xdeadc0de; return -1; }
    int result = fake_read32(NULL, region, offset, value);
    if (!result && region == APB_PMGR && ++R.power_reads > 4 && R.lose_power) *value = 0xf0;
    return result;
}

static struct apb_io backend(void)
{
    struct apb_io io = IO;
    io.read32 = recorded_fake_read;
    return io;
}

static void observer_ready(void)
{
    reset_fake();
    memset(&R, 0, sizeof R);
    mem_pmgr[0x1b8 / 4] = mem_pmgr[0x348 / 4] = mem_pmgr[0x3e8 / 4] = mem_pmgr[0x1a0 / 4] = 0xf;
    mem_phy[0] = 3;
    mem_rc[0x24 / 4] = 1;
    mem_port0[0x800 / 4] = 1;
    mem_port0[0x814 / 4] = 1;
    F.link_up_at_start = 1;
}

static void no_side_effects(void)
{
    assert(F.write_calls == 0 && F.pin_calls == 0 && F.delayed == 0 && F.nev == 0);
    assert(apb_shared_writes_compiled() == 0 && apl_shared_writes_compiled() == 0);
}

static void complete_order_and_snapshot(void)
{
    static const unsigned regions[14] = {0, 0, 0, 0, 2, 1, 6, 6, 6, 6, 0, 0, 0, 0};
    static const uint32_t offsets[14] = {0x1b8, 0x348, 0x3e8, 0x1a0, 0, 0x24,
                                       0x800, 0x814, 0x804, 0x208, 0x1b8, 0x348, 0x3e8, 0x1a0};
    struct apo_snapshot snapshot;
    observer_ready();
    struct apb_io io = backend();
    assert(apo_observe_fake_accessor(&OBSERVER_BOARD, &io, &snapshot) == APO_OBSERVED);
    assert(R.count == 14 && F.reads == 14 && snapshot.reads_attempted == 14);
    for (unsigned i = 0; i < 14; ++i) {
        assert(R.log[i].region == regions[i] && R.log[i].offset == offsets[i]);
    }
    assert(snapshot.valid_mask == 0x3fff && snapshot.stage_reached == APO_STAGE_LINK);
    assert(snapshot.result == APO_OBSERVED && snapshot.port0_linksts == 1);
    assert(snapshot.port0_appclk == 1 && snapshot.port0_reset == 1 && snapshot.port0_status == 1);
    assert(snapshot.last_region == DLS_PMGR); /* output retains observer namespace */
    no_side_effects();
}

static void guarded_stops(void)
{
    struct apo_snapshot snapshot;
    struct apb_io io = backend();
    observer_ready(); mem_pmgr[0x348 / 4] = 0;
    assert(apo_observe_fake_accessor(&OBSERVER_BOARD, &io, &snapshot) == APO_POWER_NOT_READY);
    assert(R.count == 2 && snapshot.valid_mask == 3);
    no_side_effects();
    observer_ready(); mem_phy[0] |= 0x80;
    assert(apo_observe_fake_accessor(&OBSERVER_BOARD, &io, &snapshot) == APO_PHY_NOT_READY);
    assert(R.count == 9 && snapshot.stage_reached == APO_STAGE_PHY && !(snapshot.valid_mask & APO_V_PORT_STATUS));
    for (unsigned i = 0; i < R.count; ++i) assert(R.log[i].region != APB_RC && R.log[i].region != APB_PORT0);
    no_side_effects();
    observer_ready(); mem_rc[0x24 / 4] = 0;
    assert(apo_observe_fake_accessor(&OBSERVER_BOARD, &io, &snapshot) == APO_PHYIF_NOT_RUNNING);
    assert(R.count == 10);
    for (unsigned i = 0; i < R.count; ++i) assert(R.log[i].region != APB_PORT0);
    no_side_effects();
    observer_ready(); F.ready_never = 1;
    assert(apo_observe_fake_accessor(&OBSERVER_BOARD, &io, &snapshot) == APO_PORT_NOT_RUNNING);
    assert(R.count == 13 && !(snapshot.valid_mask & APO_V_PORT_LINKSTS));
    for (unsigned i = 0; i < R.count; ++i)
        assert(!(R.log[i].region == APB_PORT0 && R.log[i].offset == 0x208));
    no_side_effects();
    observer_ready(); R.lose_power = 1;
    assert(apo_observe_fake_accessor(&OBSERVER_BOARD, &io, &snapshot) == APO_POWER_LOST);
    assert(R.count == 11 && snapshot.port0_linksts == 1);
    no_side_effects();
}

static void each_read_can_fail(void)
{
    const uint32_t bits[14] = {APO_V_POWER(0), APO_V_POWER(1), APO_V_POWER(2), APO_V_POWER(3),
        APO_V_PHY_CTRL, APO_V_RC_PHYIF, APO_V_PORT_APPCLK, APO_V_PORT_RESET, APO_V_PORT_STATUS,
        APO_V_PORT_LINKSTS, APO_V_FINAL(0), APO_V_FINAL(1), APO_V_FINAL(2), APO_V_FINAL(3)};
    for (unsigned n = 1; n <= 14; ++n) {
        struct apo_snapshot snapshot;
        observer_ready(); R.fail_at = n;
        struct apb_io io = backend();
        assert(apo_observe_fake_accessor(&OBSERVER_BOARD, &io, &snapshot) == APO_IO);
        assert(R.count == n && snapshot.reads_attempted == n);
        uint32_t expected = 0;
        for (unsigned i = 0; i + 1 < n; ++i) expected |= bits[i];
        assert(snapshot.valid_mask == expected);
        /* Fake poisons its output before failure; accessor must contain it. */
        const uint32_t values[] = {snapshot.power[0], snapshot.power[1], snapshot.power[2], snapshot.power[3],
            snapshot.phy_ctrl, snapshot.rc_phyif_ctrl, snapshot.port0_appclk, snapshot.port0_reset,
            snapshot.port0_status, snapshot.port0_linksts, snapshot.power_final[0], snapshot.power_final[1],
            snapshot.power_final[2], snapshot.power_final[3]};
        assert(values[n - 1] == 0);
        no_side_effects();
    }
}

static void read_only_capability_and_invalid_inputs(void)
{
    struct apr_observer wire = {0}, before = wire;
    struct dls_io output = {0}, saved = output;
    struct apb_io io = backend(), bad = io;
    struct apo_snapshot snapshot;
    struct dls_board board = OBSERVER_BOARD;
    uint32_t value = 0xaabbccdd;
    observer_ready();
    bad.read32 = NULL;
    assert(apr_bind_observer_fake(&wire, &bad, &output) == APB_ARGUMENT);
    assert(!memcmp(&wire, &before, sizeof wire) && !memcmp(&output, &saved, sizeof output));
    assert(apr_bind_observer_fake(NULL, &io, &output) == APB_ARGUMENT);
    assert(apr_bind_observer_fake(&wire, &io, NULL) == APB_ARGUMENT);
    assert(apr_bind_observer_fake(&wire, &io, &output) == APB_OK);
    assert(apr_bind_observer_fake(&wire, &wire.port, &output) == APB_ARGUMENT);
    assert(apr_bind_observer_fake(&wire, &wire.checked.fake, &output) == APB_ARGUMENT);
    unsigned allowed = 0;
    for (unsigned r = 0; r <= DLS_REGIONS; ++r) {
        for (uint32_t off = 0; off < 0x4000; ++off) {
            unsigned count = R.count;
            int result = output.read32(output.context, r, off, &value);
            if (apo_read_allowed(r, off)) { assert(result == APB_OK && R.count == count + 1); ++allowed; }
            else assert(result == APB_ARGUMENT && R.count == count);
        }
    }
    assert(allowed == 10 && R.count == 10);
    value = 0xaabbccdd;
    assert(output.read32(output.context, DLS_DART, 0, &value) == APB_ARGUMENT && value == 0xaabbccdd);
    assert(output.read32(output.context, UINT32_MAX, 0, &value) == APB_ARGUMENT);
    assert(output.read32(output.context, DLS_PORT, UINT32_MAX, &value) == APB_ARGUMENT);
    assert(output.read32(NULL, DLS_PORT, 0x208, &value) == APB_ARGUMENT);
    assert(output.read32(output.context, DLS_PORT, 0x208, NULL) == APB_ARGUMENT);
    observer_ready();
    board.base[DLS_PORT] += 0x8000;
    assert(apo_observe_fake_accessor(&board, &io, &snapshot) == APO_BOARD && R.count == 0);
    board = OBSERVER_BOARD; board.size[DLS_PHY] *= 2;
    assert(apo_observe_fake_accessor(&board, &io, &snapshot) == APO_BOARD && R.count == 0);
    assert(apo_observe_fake_accessor(NULL, &io, &snapshot) == APO_ARGUMENT && R.count == 0);
    assert(apo_observe_fake_accessor(&OBSERVER_BOARD, &io, NULL) == APO_ARGUMENT && R.count == 0);
    memset(&snapshot, 0xff, sizeof snapshot);
    assert(apo_observe_fake_accessor(&OBSERVER_BOARD, &bad, &snapshot) == APO_ARGUMENT && R.count == 0);
    assert(snapshot.version == 1 && snapshot.result == APO_ARGUMENT && snapshot.valid_mask == 0);
    assert(apo_observe_fake_accessor(&OBSERVER_BOARD, NULL, &snapshot) == APO_ARGUMENT && R.count == 0);
    no_side_effects();
}

int main(void)
{
    complete_order_and_snapshot();
    guarded_stops();
    each_read_can_fail();
    read_only_capability_and_invalid_inputs();
    puts("HOST ONLY PASS: unchanged observer through checked accessor, explicit regions, 14 read faults, zero writes/pins/delays");
    return 0;
}
