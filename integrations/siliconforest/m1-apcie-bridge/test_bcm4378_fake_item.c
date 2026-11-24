/* Reuse the exact frozen fake BAR/ring fixture, not an alternate backend. */
#ifdef NDEBUG
#error "test_bcm4378_fake_item requires assertions"
#endif
#define main previous_fake_ring_test_main
#include "test_bcm4378_fake_ring.c"
#undef main
#include "bcm4378_fake_item.h"

#ifdef BFI_FAKE_DEVICE
static void encode_check_submit(void)
{
    uint8_t out[16];
    prepare(2); memset(out, 0x9a, sizeof out); save(); unsigned writes = F.writes;
    assert(bfi_fake_item_compiled());
    assert(bfi_encode(&R, out, 8) == BFI_FAKE_OK && !memcmp(out, ITEM, 8));
    for (unsigned i = 8; i < sizeof out; ++i) assert(out[i] == 0x9a);
    assert(bfi_check(&R, out, 8) == BFI_FAKE_OK && F.writes == writes); unchanged(1);
    assert(bfi_submit(&R, out, 8) == BCR_FAKE_OK);
    assert(R.completed == 1 && R.write_index == 1 && F.bells == 1 && F.writes == writes+2);
    assert(!memcmp(host_buffer, ITEM, 8));
    assert(*slot(APR_BCM_BAR2, WI) == 0xface0001);
    assert(F.log[writes].offset == WI && F.log[writes+1].region == APR_BCM_BAR0 && F.log[writes+1].offset == 0x2140);
    assert(bfi_check(&R, host_buffer, 8) == BFI_FAKE_OK); /* read-only slot validation */
    save(); writes = F.writes;
    assert(bfi_encode(&R, host_buffer, 8) == BCR_ARGUMENT);
    assert(bfi_encode(&R, (uint8_t *)&R, 8) == BCR_ARGUMENT);
    assert(bfi_encode(&R, (uint8_t *)&T, 8) == BCR_ARGUMENT);
    assert(bfi_submit(&R, host_buffer, 8) == BCR_ARGUMENT);
    assert(F.writes == writes); unchanged(1);
}
static void invalid_lengths_and_bytes(void)
{
    uint8_t out[16], saved_out[16], bad[8];
    prepare(2); save(); unsigned writes = F.writes;
    memset(out, 0x9a, sizeof out); memcpy(saved_out, out, sizeof out);
    for (size_t n = 0; n <= 16; ++n) if (n != 8) {
        assert(bfi_encode(&R, out, n) == BFI_SIZE && !memcmp(out, saved_out, sizeof out));
        assert(bfi_check(&R, ITEM, n) == BFI_SIZE);
        assert(bfi_submit(&R, ITEM, n) == BFI_SIZE);
        assert(F.writes == writes); unchanged(1);
    }
    assert(bfi_encode(&R, out, SIZE_MAX) == BFI_SIZE && !memcmp(out, saved_out, sizeof out));
    assert(bfi_check(&R, ITEM, SIZE_MAX) == BFI_SIZE && bfi_submit(&R, ITEM, SIZE_MAX) == BFI_SIZE);
    assert(bfi_encode(&R, NULL, 8) == BCR_ARGUMENT);
    assert(bfi_check(&R, NULL, 8) == BCR_ARGUMENT && bfi_submit(&R, NULL, 8) == BCR_ARGUMENT);
    for (unsigned bit = 0; bit < 64; ++bit) {
        memcpy(bad, ITEM, 8); bad[bit/8] ^= (uint8_t)(1u << (bit%8));
        assert(bfi_check(&R, bad, 8) == BFI_MISMATCH);
        assert(bfi_submit(&R, bad, 8) == BFI_MISMATCH);
        assert(F.writes == writes); unchanged(1);
    }
    R.plan.item_bytes = 4;
    assert(bfi_encode(&R, out, 8) == BFI_UNSUPPORTED && bfi_check(&R, ITEM, 8) == BFI_UNSUPPORTED);
}
static int item_step(unsigned op, uint8_t *out)
{
    if (op == 0) return bfi_encode(&R, out, 8);
    if (op == 1) return bfi_check(&R, ITEM, 8);
    return bfi_submit(&R, ITEM, 8);
}
static void down_reset_and_read_failures(void)
{
    for (unsigned op = 0; op < 3; ++op) {
        for (unsigned bad = 0; bad < 7; ++bad) {
            uint8_t out[8], original[8]; memset(out, 0x9a, 8); memcpy(original, out, 8);
            prepare(2); save(); unsigned writes = F.writes, calls = F.calls;
            if (bad == 0) F.phy = 0x8f; else if (bad == 1) F.phy = 0x80;
            else if (bad == 2) F.link = 0; else if (bad == 3) F.run = 0;
            else if (bad == 4) F.link = 5; else if (bad == 5) F.link = 0x41; else F.phy = 0xb;
            assert(item_step(op, out) == (bad == 0 || bad == 1 || bad == 6 ? BCR_PHY_NOT_READY : BCR_LINK_DOWN));
            assert(F.writes == writes && !memcmp(out, original, 8)); unchanged(1);
            if (bad == 0 || bad == 1 || bad == 6) assert(F.calls == calls+1);
        }
        prepare(2); save(); F.reset_after_read = F.calls+2; unsigned writes = F.writes;
        uint8_t out[8]; memset(out, 0x9a, 8);
        assert(item_step(op, out) == BCR_PHY_NOT_READY && F.writes == writes);
        for (unsigned i = 0; i < 8; ++i) assert(out[i] == 0x9a);
        unchanged(1);
        for (unsigned fail = 1; fail <= 5; ++fail) {
            prepare(2); save(); F.fail_at = F.calls+fail; writes = F.writes;
            memset(out, 0x9a, 8);
            assert(item_step(op, out) == BCR_IO && F.writes == writes); unchanged(1);
        }
    }
}
static void submit_faults_and_full(void)
{
    prepare(2); unsigned start = F.calls;
    assert(bfi_submit(&R, ITEM, 8) == BCR_FAKE_OK); unsigned total = F.calls-start;
    for (unsigned applied = 0; applied < 2; ++applied) for (unsigned n = 1; n <= total; ++n) {
        prepare(2); start = F.calls; F.fail_at = start+n; F.apply_failed_write = applied;
        assert(bfi_submit(&R, ITEM, 8) == BCR_IO && F.calls == start+n);
        if (n > 5) {
            assert(R.faulted); unsigned calls = F.calls;
            assert(bfi_submit(&R, ITEM, 8) == BCR_FAULTED && F.calls == calls);
        } else assert(!R.faulted); /* read-only item gate failed before ring admission */
    }
    prepare(2);
    for (unsigned i = 0; i < 3; ++i) assert(bfi_submit(&R, ITEM, 8) == BCR_FAKE_OK);
    save(); unsigned writes = F.writes;
    assert(bfi_submit(&R, ITEM, 8) == BCR_FULL && F.writes == writes); unchanged(1);
    printf("HOST item-submit callback boundaries: %u (both failed-write models)\n", total);
}
#else
static void item_default_zero_io(void)
{
    memset(&R, 0, sizeof R); memset(&T, 0, sizeof T); memset(&F, 0, sizeof F);
    memset(host_buffer, 0xa5, sizeof host_buffer); memcpy(saved_buffer, host_buffer, sizeof host_buffer);
    memcpy(&SAVED_R, &R, sizeof R);
    T.io.write32 = no_write;
    uint8_t out[16], saved_out[16]; memset(out, 0x9a, sizeof out); memcpy(saved_out, out, sizeof out);
    assert(!bfi_fake_item_compiled());
    assert(bfi_encode(&R, out, 8) == BFI_GATED);
    assert(bfi_check(&R, ITEM, 8) == BFI_GATED && bfi_submit(&R, ITEM, 8) == BFI_GATED);
    assert(!F.calls && !F.writes && !memcmp(out, saved_out, sizeof out));
    assert(!memcmp(&SAVED_R, &R, sizeof R) && !memcmp(host_buffer, saved_buffer, sizeof host_buffer));
}
#endif
int main(void)
{
    assert(!apb_shared_writes_compiled() && !apl_shared_writes_compiled());
    assert(!apg_fake_drive_compiled() && !ars_fake_enable_compiled());
#ifdef BFI_FAKE_DEVICE
    encode_check_submit(); invalid_lengths_and_bytes(); down_reset_and_read_failures(); submit_faults_and_full();
#else
    item_default_zero_io();
#endif
    puts("HOST ONLY PASS: frozen opaque synthetic item, exact size/bytes, guarded fake-ring submission only");
    return 0;
}
