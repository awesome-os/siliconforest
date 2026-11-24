/* Original private ownership manager; no activation or DMA publication API. */
#ifndef BCM_OWNED_BACKING_H
#define BCM_OWNED_BACKING_H
#include "lifecycle_bridge.h"
#include "owned_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BNO_JOBS 4u
#define BNO_ROLES 3u
#define BNO_MASK UINT64_C(0xfffffc000)
enum bno_action { BNO_ALLOCATE=1, BNO_PREPARE, BNO_QUERY, BNO_ZERO,
    BNO_COPY_DATA, BNO_COMPLETE, BNO_RESET_REF };
enum bno_outcome { BNO_CALL_OK, BNO_CALL_REFUSED, BNO_CALL_ERROR, BNO_CALL_UNKNOWN };
struct bno_view { uint64_t cpu, pa, span, length, capacity; };
struct bno_protection {
    uint64_t provenance; /* trusted setup receipt identity, not authentication */
    uint32_t complete, pa_count, cpu_count;
    struct bpl_range pa[BPL_RANGES], cpu[BPL_RANGES];
};
struct bno_call {
    uint64_t sequence, generation, identity, offset, bytes;
    uint32_t slot, role, action;
    const void *source;
    struct bno_view expected;
};
struct bno_return {
    uint64_t sequence, generation, identity;
    uint32_t slot, role, action, outcome;
    int32_t native_error;
    struct bno_view view;
};
/* invoke must anchor a returned native strong reference before returning any
 * allocation result. It must not release/replace it except RESET_REF. Mutex
 * callbacks protect metadata only; invoke always executes outside the mutex.
 * Exact operation identity is echoed. Unknown/nonreturn has no retry meaning.
 */
struct bno_effect_call;
struct bno_effect_return;
struct bno_ops {
    void *context;
    void (*lock)(void *);
    void (*unlock)(void *);
    void (*invoke)(void *, const struct bno_call *, struct bno_return *);
    void (*dispatch)(void *, const struct bno_effect_call *, struct bno_effect_return *);
    uint32_t effect_backend_available; /* immutable copied boot property */
};
struct bno_record {
    uint64_t handle, identity;
    struct bno_view view;
    uint32_t alloc_started, alloc_returned, prepare_started, prepare_returned;
    uint32_t owns_pair, complete_started, complete_returned, pair_balanced;
    uint32_t reset_started, reset_returned;
};
struct bno_job {
    struct bpl_lease lease;
    uint64_t generation, sequence, iova;
    uint32_t phase, permissions, metadata_bound, core_retired;
    uint32_t call_pending, last_action, last_role;
    int32_t first_error;
    struct bno_record records[BNO_ROLES];
};
/* Internal synchronous scope, never exported by the native facade. */
struct bno_effect_call {
    uint64_t sequence, generation;
    uint32_t slot, operation, kind, resources;
    struct bpl_ticket ticket;
    struct bno_effect_request request;
    uint64_t identities[BNO_ROLES];
    struct bno_view views[BNO_ROLES];
};
struct bno_effect_return {
    uint64_t sequence, generation;
    uint32_t slot, operation, ticket_sequence, outcome;
    int32_t error;
    uint64_t identities[BNO_ROLES];
};
struct bno_manager {
    uint64_t magic, next_handle, next_sequence;
    uint32_t busy, halted;
    struct bno_ops ops;
    struct bpl_range control; /* full stable anchor CPU range, not PA */
    struct bno_protection protection;
    struct bpl_registry registry;
    struct bno_job jobs[BNO_JOBS];
};


struct bno_native_setup {
    uint64_t identity, generation;
    struct bpl_range typed_anchor_cpu;
    uint64_t opaque_mutex_identity; /* address identity only; no size claim */
};
/* Single-threaded trusted boot setup BEFORE clients. Pin empty control objects
 * first; query only their identity and exact typed anchor CPU span. No backing
 * descriptors exist yet. Platform independently derives complete protected
 * spans. Configure binds them to the exact setup identity once. Invalid pure
 * configuration can be corrected while the manager is still zero; no backing
 * retry/reset is involved. Opaque IOLock allocation extent remains native
 * allocator/platform provenance, not sizeof/a guessed page span here. */
int bno_native_pin_empty(struct bno_native_setup *);
int bno_native_configure(const struct bno_native_setup *, const struct bpl_policy *, const struct bno_protection *);

/* One stable zeroed boot-lifetime manager and independently anchored provider
 * references. Single-threaded init, then all calls use the supplied short lock.
 * No destruction/reset/reinitialization API; fields are not caller-mutable.
 * Caller policy must conservatively reserve actual control physical backing.
 * No guessed VA->PA or physical discovery occurs in this manager.
 */
int bno_init(struct bno_manager *, const struct bno_ops *, const struct bpl_policy *,
             const struct bno_protection *, uint64_t actual_page_size, struct bpl_range full_anchor_cpu);
int bno_acquire(struct bno_manager *, const struct bno_request *, uint64_t data_iova,
                uint32_t permissions, struct bno_token *);
int bno_zero_private(struct bno_manager *, const struct bno_token *);
int bno_copy_private_data(struct bno_manager *, const struct bno_token *,
                          uint64_t offset, const void *, size_t);
int bno_retire_private(struct bno_manager *, const struct bno_token *);
int bno_run(struct bno_manager *, const struct bno_token *, const struct bno_effect_request *, struct bno_effect_result *);
int bno_inspect(struct bno_manager *, uint32_t slot, struct bno_snapshot *);
#ifdef __cplusplus
}
#endif
#endif
