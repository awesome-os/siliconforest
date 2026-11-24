/* Original codec for the frozen eight-byte opaque HOST TEST item only.
 * No acquired msgbuf definition or actual firmware message ABI exists here. */
#ifndef DL_BCM4378_FAKE_ITEM_H
#define DL_BCM4378_FAKE_ITEM_H
#include "bcm4378_fake_ring.h"
#ifdef __cplusplus
extern "C" {
#endif

#define BFI_SYNTHETIC_BYTES 8u
enum bfi_result {
    BFI_FAKE_OK = BCR_FAKE_OK, BFI_GATED = BCR_GATED,
    BFI_SIZE = -200, BFI_MISMATCH = -201, BFI_UNSUPPORTED = -202
}; /* Other results are unchanged BCR_* readiness/ring errors. */

/* Layout: exactly8 opaque bytes11 22 33 44 55 66 77 88 from the frozen ring
 * fixture. No message type, flags, request ID, endian field or opcode is
 * inferred. This pattern must never be sent to physical firmware.
 * Caller keeps ring/parent/accessor/buffers stable and serialized. */

/* Exact size required (not a minimum capacity). Failure preserves output.
 * Output may not alias the active fake ring, ring object or parent object. */
int bfi_encode(struct bcr_ring *, uint8_t *out, size_t bytes);
/* Exact length and all8 opaque bytes checked; no state or register writes.
 * Checking a retained fake-ring slot is allowed; no firmware semantic claim. */
int bfi_check(struct bcr_ring *, const uint8_t *item, size_t bytes);
/* Validates first, then calls unchanged bcr_submit. Only a valid synthetic
 * fixture on an admitted fake link/PHY can reach fake buffer/index/doorbell.
 * No hidden allocation, firmware image, DMA, type conversion or retry. */
int bfi_submit(struct bcr_ring *, const uint8_t *item, size_t bytes);

/* Default/KERNEL every operation returns GATED with zero I/O/buffer changes.
 * Working host mode also requires the explicit BCR_FAKE_DEVICE build. */
int bfi_fake_item_compiled(void);
#ifdef __cplusplus
}
#endif
#endif
