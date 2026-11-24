/* Original bounded fake shared-memory ring, not a DMA/msgbuf driver. */
#ifndef DL_BCM4378_FAKE_RING_H
#define DL_BCM4378_FAKE_RING_H
#include "bcm4378_transport.h"
#ifdef __cplusplus
extern "C" {
#endif

#define BCR_MAX_DEPTH 16u
#define BCR_MAX_ITEM 64u
#define BCR_SHARED_GUARD_BYTES 120u /* includes mapped header fields through+116 */
#define BCR_INFO_GUARD_BYTES 64u   /* conservative policy, not a struct ABI claim */
enum bcr_result {
    BCR_FAKE_OK = 0, BCR_GATED = 100, BCR_PHY_NOT_READY = 101,
    BCR_LINK_DOWN = 102, BCR_FULL = 103, BCR_BUSY = 104,
    BCR_ARGUMENT = -100, BCR_STATE = -101, BCR_IO = -102,
    BCR_RANGE = -103, BCR_VERIFY = -104, BCR_OWNER = -105, BCR_FAULTED = -106
};
struct bcr_plan {
    uint8_t *buffer; /* ordinary caller-preallocated fake host ring, not DMA */
    size_t capacity; /* exactly depth*item_bytes, <=1024 */
    uint32_t depth, item_bytes;
    uint64_t fake_bus_label; /* serialized number only, never a CPU pointer */
};
struct bcr_ring {
    struct bct_session *transport; /* stable, serialized, held in BCT_SHARED */
    struct bcr_plan plan;
    uint32_t bound, configured, closed, faulted, dirty, inflight;
    uint32_t descriptor, write_address, read_address, write_index, bell_offset;
    uint32_t register_writes, buffer_writes, index_write_attempts, index_published;
    uint32_t bell_attempts, completed;
};

/* Only H2D ring0, TCM indices (not DMA-index mode). Requires the original fake
 * transport's discovered shared state and unchanged pointer/flags. Ring-info
 * prefix supplies descriptor/index addresses; no firmware bytes are read.
 * Caller owns disjoint stable ring/parent/plan/buffer/input storage, serializes
 * all operations and must not mutate the parent's window or metadata. */
int bcr_open(struct bcr_ring *, struct bct_session *, const struct bcr_plan *);

/* Requires descriptor words and initial low16 indices zero; upper index
 * halves preserved. Serializes supplied fake label/depth/item size only.
 * No allocation, real DMA or firmware-visible hardware publication. */
int bcr_configure(struct bcr_ring *);

/* One opaque synthetic item: buffer copy -> low16 producer publication ->
 * BCM BAR0 ring doorbell1. One slot remains empty; no overwrite on FULL.
 * The fake word accessor models halfword index writes via a checked word
 * RMW, preserving high16. This is NOT a real-device access-width contract.
 * Partial mutations latch fault; no retry/rollback/free or invented success. */
int bcr_submit(struct bcr_ring *, const uint8_t *item, size_t bytes);

/* Logical retirement only when observed consumer==cached/published producer.
 * Descriptor/buffer remain retained. No physical DMA/IRQ drain claim. */
int bcr_close(struct bcr_ring *);

/* Default/KERNEL APIs return GATED with zero I/O or buffer changes.
 * BCR_FAKE_DEVICE+KERNEL or shared PHY/PMGR fake writes are compile-refused. */
int bcr_fake_ring_compiled(void);
#ifdef __cplusplus
}
#endif
#endif
