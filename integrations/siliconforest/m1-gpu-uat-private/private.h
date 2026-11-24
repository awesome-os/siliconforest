/* Original code; license selection pending. Private CPU-only ownership. */
#ifndef DUAT_PRIVATE_H
#define DUAT_PRIVATE_H
#include "../m1-gpu-uat-codec/uat_codec.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DUAT_CAPACITY 16u
#define DUAT_BYTES 16384u
#define DUAT_PA_END (UINT64_C(1) << 36)
enum duat_result { DUAT_OK, DUAT_ARGUMENT, DUAT_STATE, DUAT_LIMIT,
    DUAT_ALLOC, DUAT_PREPARE, DUAT_RANGE, DUAT_GRAPH, DUAT_QUARANTINE };
enum duat_state { DUAT_EMPTY, DUAT_OWNED, DUAT_PREPARED, DUAT_RETAINED };
struct duat_context;
struct duat_token {
    const struct duat_context *owner;
    uint64_t generation;
    unsigned slot;
};
/* Provider must return a distinct owned allocation or NULL; failure transfers
 * nothing. prepare returns 0 only for a successful explicit prepare. inspect
 * borrows immutable bytes, valid until release. complete is attempted once;
 * any nonzero result is uncertain. release consumes the factory reference.
 * Callbacks must not reenter, expose, modify or asynchronously access pages. */
struct duat_view {
    const uint8_t *bytes;
    uint64_t length, capacity, page_size, physical, segment;
};
struct duat_ops {
    void *(*allocate)(void *);
    int (*prepare)(void *, void *);
    int (*inspect)(void *, void *, struct duat_view *);
    int (*complete)(void *, void *);
    void (*release)(void *, void *);
};
struct duat_slot {
    void *handle;
    struct duat_view view;
    uint64_t generation;
    unsigned level;
    enum duat_state state;
};
/* Caller-owned stable-address, zero-initialized storage. Fields are private
 * implementation state, not an interface for importing pages or modifying
 * ownership. Never copy/reset/reinitialize; retain while any slot is live.
 * External serialization in a sleep-capable context is mandatory. */
struct duat_context {
    const struct duat_context *self;
    struct duat_ops ops;
    void *provider;
    struct duat_slot slots[DUAT_CAPACITY];
    uint64_t next_generation;
    unsigned busy, stopping, failed;
};
/* Allocate outside the kernel stack. Ordinary private RAM, disjoint from the
 * context, outputs, tokens and provider pages. No GPU-visible storage. */
struct duat_workspace {
    uint8_t pages[DUAT_CAPACITY][DUAT_BYTES];
    uint8_t roots[16];
    struct uat_table tables[DUAT_CAPACITY];
    struct uat_range ranges[DUAT_CAPACITY];
    uint64_t generations[DUAT_CAPACITY];
};
struct duat_summary { size_t tables; uint64_t pages, mapped_bytes; };
enum duat_result duat_init(struct duat_context *, const struct duat_ops *, void *);
enum duat_result duat_allocate(struct duat_context *, unsigned level,
                             struct duat_token *);
/* owner==NULL denotes an absent root only when every token field is zero.
 * Nonempty roots must identify owned level-1 pages. All live tables must be
 * reached exactly once. No data leaves, caller ranges or raw root words. */
enum duat_result duat_validate(struct duat_context *,
    const struct duat_token roots[2], struct duat_workspace *, struct duat_summary *);
enum duat_result duat_release(struct duat_context *, struct duat_token);
/* Conservative notification only; does not expose memory or prove quiescence.
 * Makes the whole context fail closed and permanently retains this slot. */
enum duat_result duat_quarantine(struct duat_context *, struct duat_token);
/* Idempotent shutdown; returns QUARANTINE if any retained allocation exists.
 * Success means private allocations released, never GPU/unload safety. */
enum duat_result duat_shutdown(struct duat_context *);
#ifdef __cplusplus
}
#endif
#endif
