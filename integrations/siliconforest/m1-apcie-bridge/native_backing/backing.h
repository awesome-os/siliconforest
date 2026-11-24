/* Original private ownership manager; no activation or DMA publication API. */
#ifndef BCM_NATIVE_BACKING_H
#define BCM_NATIVE_BACKING_H
#include "lifecycle_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BPN_JOBS 4u
#define BPN_ROLES 3u
#define BPN_MASK UINT64_C(0xfffffc000)
enum bpn_status { BPN_OK=0, BPN_ARGUMENT=-100, BPN_BUSY=-101,
    BPN_STALE=-102, BPN_LIMIT=-103, BPN_RETAINED=-104, BPN_VALIDATION=-105,
    BPN_REFUSED=-106, BPN_PAGE=-107 };
enum bpn_phase { BPN_EMPTY, BPN_ACQUIRING, BPN_VALIDATED, BPN_INITIALIZED,
    BPN_RETIRING, BPN_CORE_RETIRED, BPN_REFS_RELEASED, BPN_QUARANTINED };
enum bpn_action { BPN_ALLOCATE=1, BPN_PREPARE, BPN_QUERY, BPN_ZERO,
    BPN_COPY_DATA, BPN_COMPLETE, BPN_RESET_REF };
enum bpn_outcome { BPN_CALL_OK, BPN_CALL_REFUSED, BPN_CALL_ERROR, BPN_CALL_UNKNOWN };
struct bpn_view { uint64_t cpu, pa, span, length, capacity; };
struct bpn_protection {
    uint64_t provenance; /* trusted setup receipt identity, not authentication */
    uint32_t complete, pa_count, cpu_count;
    struct bpl_range pa[BPL_RANGES], cpu[BPL_RANGES];
};
struct bpn_call {
    uint64_t sequence, generation, identity, offset, bytes;
    uint32_t slot, role, action;
    const void *source;
    struct bpn_view expected;
};
struct bpn_return {
    uint64_t sequence, generation, identity;
    uint32_t slot, role, action, outcome;
    int32_t native_error;
    struct bpn_view view;
};
/* invoke must anchor a returned native strong reference before returning any
 * allocation result. It must not release/replace it except RESET_REF. Mutex
 * callbacks protect metadata only; invoke always executes outside the mutex.
 * Exact operation identity is echoed. Unknown/nonreturn has no retry meaning.
 */
struct bpn_ops {
    void *context;
    void (*lock)(void *);
    void (*unlock)(void *);
    void (*invoke)(void *, const struct bpn_call *, struct bpn_return *);
};
struct bpn_record {
    uint64_t handle, identity;
    struct bpn_view view;
    uint32_t alloc_started, alloc_returned, prepare_started, prepare_returned;
    uint32_t owns_pair, complete_started, complete_returned, pair_balanced;
    uint32_t reset_started, reset_returned;
};
struct bpn_job {
    struct bpl_lease lease;
    uint64_t generation, sequence, iova;
    uint32_t phase, permissions, metadata_bound, core_retired;
    uint32_t call_pending, last_action, last_role;
    int32_t first_error;
    struct bpn_record records[BPN_ROLES];
};
struct bpn_manager {
    uint64_t magic, next_handle, next_sequence;
    uint32_t busy, halted;
    struct bpn_ops ops;
    struct bpl_range control; /* full stable anchor CPU range, not PA */
    struct bpn_protection protection;
    struct bpl_registry registry;
    struct bpn_job jobs[BPN_JOBS];
};
struct bpn_token { const struct bpn_manager *anchor; uint64_t generation; uint32_t slot; };
struct bpn_snapshot {
    uint64_t generation, sequence;
    uint32_t phase, busy, halted, metadata_bound, core_retired, call_pending;
    uint32_t identity_records, owned_pairs, balanced_pairs, resets_returned;
    int32_t first_error;
};
/* One stable zeroed boot-lifetime manager and independently anchored provider
 * references. Single-threaded init, then all calls use the supplied short lock.
 * No destruction/reset/reinitialization API; fields are not caller-mutable.
 * Caller policy must conservatively reserve actual control physical backing.
 * No guessed VA->PA or physical discovery occurs in this manager.
 */
int bpn_init(struct bpn_manager *, const struct bpn_ops *, const struct bpl_policy *,
             const struct bpn_protection *, uint64_t actual_page_size, struct bpl_range full_anchor_cpu);
int bpn_acquire(struct bpn_manager *, const struct bpl_request *, uint64_t data_iova,
                uint32_t permissions, struct bpn_token *);
int bpn_zero_private(struct bpn_manager *, const struct bpn_token *);
int bpn_copy_private_data(struct bpn_manager *, const struct bpn_token *,
                          uint64_t offset, const void *, size_t);
int bpn_retire_private(struct bpn_manager *, const struct bpn_token *);
int bpn_inspect(struct bpn_manager *, uint32_t slot, struct bpn_snapshot *);
#ifdef __cplusplus
}
#endif
#endif
