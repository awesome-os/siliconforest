/* Original Darwin Lab code. License selection pending.
 * Portable backing-store coordinator; not an XNU allocator or GPU driver.
 * Caller must serialize every operation, keep this object alive while leases
 * exist, and never manipulate policy/slots directly. Callbacks cannot reenter.
 */
#ifndef DARWIN_LAB_BACKING_STORE_H
#define DARWIN_LAB_BACKING_STORE_H
#include "buffer_lifetime.h"

typedef struct {
    /* Allocate stable, zero-initialized backing. On failure return NULL and
     * retain no resources. Memory must stay valid until release succeeds.
     * This interface does NOT supply cache attributes, DMA mapping or wiring.
     */
    void *(*allocate)(void *context, uint64_t bytes, uint64_t alignment);
    /* Success: backing is completely released. Failure: backing remains fully
     * owned, unchanged and retryable. A partially successful release violates
     * this contract. Runs only from an explicit collect call, never on a fence.
     */
    bool (*release)(void *context, void *cookie, uint64_t bytes);
} dl_backing_ops;

typedef struct { bool occupied; dl_buffer buffer; void *cookie; } dl_backing_slot;
typedef struct {
    dl_domain policy;
    dl_backing_ops ops;
    void *context;
    uint64_t page_size, byte_limit, resident_bytes;
    bool in_callback;
    dl_backing_slot backing[DL_BUFFER_CAPACITY];
} dl_store;

typedef struct {
    unsigned reclaimed, retained, backend_failures;
    uint64_t reclaimed_bytes;
} dl_collection;

/* DL_BAD_STATE also reports forbidden callback reentrancy. The additional
 * backend-error status is intentionally outside the existing policy enum.
 */
typedef enum { DL_STORE_BACKEND_ERROR = 100 } dl_store_extra_status;

/* Initializes a NEW store. Reinitialization of a live store is prohibited. */
int dl_store_init(dl_store *, uint64_t session, uint64_t page_size,
                  uint64_t byte_limit, dl_backing_ops, void *context);
int dl_store_allocate(dl_store *, uint64_t bytes, dl_buffer *out);
int dl_store_hold(dl_store *, dl_buffer, dl_lease_kind, dl_lease *out);
int dl_store_close_owner(dl_store *, dl_buffer);
int dl_store_release_host(dl_store *, dl_lease);
/* These acknowledge real operations already completed by a future backend.
 * Never call merely because an unmap, completion or firmware stop was requested.
 */
int dl_store_unbind_gpu(dl_store *, dl_lease);
int dl_store_complete_job(dl_store *, dl_lease);
int dl_store_ack_firmware_release(dl_store *, dl_lease);
int dl_store_timeout(dl_store *, dl_lease);
int dl_store_mark_device_lost(dl_store *);
int dl_store_backend_ack_quiescence(dl_store *, uint64_t sequence);
int dl_store_inspect(const dl_store *, dl_buffer, dl_view *out);
/* Never called implicitly by close, completion, unmap or quiescence callbacks.
 * Run from a caller-selected context that permits blocking destruction. */
int dl_store_collect(dl_store *, dl_collection *out);
/* Synchronous, serialized borrow of retained backing. Requires at least one
 * live host/device lease. The visitor must not retain the cookie or reenter the
 * store; no backing is released during the visit. */
typedef int (*dl_backing_visitor)(void *arg, void *cookie, uint64_t bytes);
int dl_store_visit_backing(dl_store *, dl_buffer, dl_backing_visitor, void *arg);
int dl_store_check_invariants(const dl_store *);
#endif
