/* Original fixed opaque-fixture codec, deliberately not a msgbuf wire ABI. */
#include "bcm4378_fake_item.h"

#if defined(KERNEL) && defined(BFI_FAKE_DEVICE)
#error "BFI_FAKE_DEVICE is host-test only; no kernel item/MMIO path"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "fake item requires shared PHY and PMGR writes compiled out"
#endif
#if defined(BFI_FAKE_DEVICE) && !defined(BCR_FAKE_DEVICE)
#error "fake item requires the explicit fake ring build"
#endif

#ifdef BFI_FAKE_DEVICE
#include "register_accessor.h"
#include <string.h>

static const uint8_t fixture[BFI_SYNTHETIC_BYTES] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

static int ready(const struct bcr_ring *r)
{
    uint32_t phy = 0, run = 0, link = 0, pci = 0;
    if (!r || !r->bound || !r->configured || r->closed || !r->transport ||
        r->transport->phase != BCT_SHARED || r->transport->faulted) return BCR_STATE;
    if (r->faulted) return BCR_FAULTED;
    if (r->plan.item_bytes != BFI_SYNTHETIC_BYTES) return BFI_UNSUPPORTED;
    const struct apb_io *io = &r->transport->io;
    if (!apr_bcm_io_bound(io)) return BCR_STATE;
    if (io->read32(io->context, APB_PHY, 0, &phy)) return BCR_IO;
    if ((phy & 0x8fu) != 0x0fu) return BCR_PHY_NOT_READY;
    if (io->read32(io->context, APB_PORT0, 0x804, &run)) return BCR_IO;
    if (!(run & 1)) return BCR_LINK_DOWN;
    if (io->read32(io->context, APB_PORT0, 0x208, &link)) return BCR_IO;
    if ((link & 0x45u) != 1) return BCR_LINK_DOWN;
    if (io->read32(io->context, APR_BCM_CONFIG, 0, &pci)) return BCR_IO;
    if (pci != 0x442514e4u) return BCR_OWNER;
    if (io->read32(io->context, APB_PHY, 0, &phy)) return BCR_IO;
    return (phy & 0x8fu) == 0x0fu ? BFI_FAKE_OK : BCR_PHY_NOT_READY;
}

static int overlap(const void *a, size_t n, const void *b, size_t k)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (n > UINTPTR_MAX-x || k > UINTPTR_MAX-y) return 1;
    return x < y+k && y < x+n;
}

int bfi_fake_item_compiled(void) { return 1; }
int bfi_encode(struct bcr_ring *r, uint8_t *out, size_t bytes)
{
    int e;
    if (bytes != BFI_SYNTHETIC_BYTES) return BFI_SIZE;
    if (!out) return BCR_ARGUMENT;
    if ((e = ready(r))) return e;
    if (overlap(out, bytes, r->plan.buffer, r->plan.capacity) || overlap(out, bytes, r, sizeof *r) ||
        overlap(out, bytes, r->transport, sizeof *r->transport)) return BCR_ARGUMENT;
    memcpy(out, fixture, BFI_SYNTHETIC_BYTES);
    return BFI_FAKE_OK;
}
int bfi_check(struct bcr_ring *r, const uint8_t *item, size_t bytes)
{
    int e;
    if (bytes != BFI_SYNTHETIC_BYTES) return BFI_SIZE;
    if (!item) return BCR_ARGUMENT;
    if ((e = ready(r))) return e;
    return memcmp(item, fixture, BFI_SYNTHETIC_BYTES) ? BFI_MISMATCH : BFI_FAKE_OK;
}
int bfi_submit(struct bcr_ring *r, const uint8_t *item, size_t bytes)
{
    int e = bfi_check(r, item, bytes);
    if (e) return e;
    return bcr_submit(r, item, bytes);
}
#else
int bfi_fake_item_compiled(void) { return 0; }
int bfi_encode(struct bcr_ring *r, uint8_t *out, size_t bytes)
{ (void)r;(void)out;(void)bytes; return BFI_GATED; }
int bfi_check(struct bcr_ring *r, const uint8_t *item, size_t bytes)
{ (void)r;(void)item;(void)bytes; return BFI_GATED; }
int bfi_submit(struct bcr_ring *r, const uint8_t *item, size_t bytes)
{ (void)r;(void)item;(void)bytes; return BFI_GATED; }
#endif
