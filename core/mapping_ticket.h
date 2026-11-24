/* Original Darwin Lab code. License selection pending.
 * Pager-release handoff, not a pager, VM mapper, DMA allocator or XNU kext.
 * See docs/MAPPING_HANDOFF.md before integration.
 */
#ifndef DARWIN_LAB_MAPPING_TICKET_H
#define DARWIN_LAB_MAPPING_TICKET_H
#include "backing_store.h"

/* Compiler-owned C11 storage, independent of a hosted/kernel stdatomic.h.
 * The implementation uses only Clang/GCC lock-free compiler primitives. */
#if !defined(__clang__) && !defined(__GNUC__)
#error "Mapping notifications require Clang or GCC atomic primitives"
#endif
typedef _Atomic(bool) dl_map_atomic_bool;
#define DL_MAP_ATOMIC_BOOL_LOCK_FREE __GCC_ATOMIC_BOOL_LOCK_FREE

/* Our internal policy bits, NOT Darwin/Linux PROT_* values. */
#define DL_MAP_READ  1u
#define DL_MAP_WRITE 2u

typedef enum {
    DL_MAP_FRESH = 0, DL_MAP_PREPARED, DL_MAP_PUBLISHED,
    DL_MAP_DRAINED, DL_MAP_ABORTED
} dl_map_phase;
typedef struct {
    uint64_t offset, bytes;
    unsigned requested_access, allowed_access;
} dl_map_request;
typedef struct {
    dl_store *store;
    dl_buffer buffer;
    dl_lease hold;
    dl_map_request request;
    dl_map_phase phase;
    /* Only this member is accessed by the asynchronous release callback. */
    dl_map_atomic_bool release_notified;
} dl_map_ticket;
typedef struct {
    void (*release)(void *);
    void *release_arg;
} dl_map_handoff;

/* Tickets are caller-owned, zero-initialized, stable-address, SINGLE-USE.
 * Do not copy, move, reset, reinitialize or reuse a published ticket.
 * Keep it alive until the external pager/callback producer is detached and
 * joined, AND drain has completed. DRAINED alone is NOT an unload barrier.
 * Store/control operations must share the store's external serialization.
 * The release callback alone can run concurrently, provided its ticket lives.
 */
int dl_map_prepare(dl_store *, dl_buffer, dl_map_request, dl_map_ticket *);
/* Call after page enumeration and all driver-local fallible preparation, but
 * BEFORE returning success from mmap_pages. Once exposed, the pager owns the
 * notification obligation, even when mapping setup subsequently fails. */
int dl_map_publish(dl_map_ticket *, dl_map_handoff *);
/* Roll back only BEFORE the handoff is published. Never destroys backing. */
int dl_map_abort(dl_map_ticket *);
/* Worker/control context. No backing is freed here; collect remains separate.
 * DL_BUSY means the pager has not notified release. DL_STALE means consumed.
 * A callback notification is not proof that this worker may destroy the ticket.
 */
int dl_map_drain(dl_map_ticket *);

/* Conversion helper for a 32-bit page-number sink such as pd_dri_ops.
 * physical_address must be a HOST physical address from a verified backend,
 * not a virtual address, IOVA or GPU address. No translation is performed.
 * A zero address is not categorically rejected: allocation validity belongs
 * to the backend. The output is unchanged on any error.
 */
int dl_map_page_number(uint64_t physical_address, unsigned page_shift,
                       uint32_t *page_number);
#endif
