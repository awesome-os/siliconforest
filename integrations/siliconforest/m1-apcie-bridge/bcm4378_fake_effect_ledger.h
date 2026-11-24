/* Original finite host-only intent ledger; not a native lifetime capability. */
#ifndef DL_BCM4378_FAKE_EFFECT_LEDGER_H
#define DL_BCM4378_FAKE_EFFECT_LEDGER_H
#include "bcm4378_fake_ownership.h"
#ifdef __cplusplus
extern "C" {
#endif

#define BFL_LEASES 2u
#define BFL_RECORDS 32u
enum bfl_result {
    BFL_FAKE_OK = 0, BFL_GATED = 100,
    BFL_STALE = -400, BFL_QUARANTINED = -401, BFL_EXPOSED = -402,
    BFL_BUSY = -403, BFL_LIMIT = -404, BFL_STATE = -405,
    BFL_ARGUMENT = -406, BFL_CALLBACK_FAILED = -407, BFL_UNCERTAIN = -408
}; /* Underlying readiness/registry/codec errors remain exact. */
enum bfl_kind { BFL_ACQUIRE = 1, BFL_PRIVATE_MAP, BFL_EXPOSE, BFL_PRIVATE_RETIRE };
enum bfl_phase { BFL_PENDING = 1, BFL_COMPLETE, BFL_FAILED, BFL_UNKNOWN };
enum bfl_outcome { BFL_EFFECT_OK = 0, BFL_EFFECT_FAILED = 1, BFL_EFFECT_UNCERTAIN = 2 };
enum bfl_lease_state { BFL_UNUSED = 0, BFL_ACTIVE, BFL_RETIRED };
struct bfl_token { uint64_t generation; uint32_t slot; };
struct bfl_record {
    uint64_t generation;
    uint32_t slot, sequence, kind, phase;
    int32_t result; /* exact registry error or callback outcome; not an ACK */
};
struct bfl_lease {
    struct bfo_mapping mapping;
    struct bfo_interrupt interrupt;
    uint64_t generation;
    uint32_t state, exposed, quarantined, private_started, private_complete;
};
struct bfl_ledger {
    struct bfo_registry registry;
    struct bfl_lease leases[BFL_LEASES];
    struct bfl_record records[BFL_RECORDS]; /* append-only, including old generations */
    uint64_t last_generation;
    uint32_t bound, busy, count; /* busy1 read preflight; busy2 recorded effect */
};
/* Synchronous HOST FAKE callback only. It is called once, with PENDING intent
 * and sticky exposed already visible. No real device callback/capability/cast
 * is supplied here. Unknown result, failure or failed postcheck quarantines.
 * Must not mutate ledger/owners directly or keep pointers after returning. */
typedef enum bfl_outcome (*bfl_fake_effect)(void *, const struct bfl_record *);

/* One canonical stable ledger/registry per fake endpoint. Caller serializes
 * every user and cannot bypass this layer with old APIs. Zero-initialize once;
 * never copy/move/edit ledger or active handles. All inputs/outputs/workspace/
 * backing must be accessible, disjoint, retained ordinary RAM. Read-only fake
 * preflight callbacks perform no effects; all later effect callbacks observe
 * a recorded intent. Reentrant API calls refuse BUSY without callbacks.
 * No allocation, native adapter, DMA, hardware drain or actual safe-free. */
int bfl_open(struct bfl_ledger *, const struct apb_io *checked_fake);
int bfl_acquire(struct bfl_ledger *, const struct dld_policy *, const struct bfd_storage *,
                 uint32_t sid, uint32_t vector, struct bfl_token *);
int bfl_map(struct bfl_ledger *, struct bfl_token, struct bfo_workspace *,
              uint64_t iova, uint64_t pa, uint64_t bytes, unsigned permissions);
int bfl_expose(struct bfl_ledger *, struct bfl_token, bfl_fake_effect, void *);
/* No success flag, completion ACK, reset, fault-clear or exposed close API.
 * Only never-exposed, nonquarantined leases with complete private pairs may
 * attempt retirement. Failures retain BOTH registry claims and backing.
 * Healthy retirement stages both closes and commits with no later callback.
 * Exposed leases remain retained even after a successful fake callback. */
int bfl_retire(struct bfl_ledger *, struct bfl_token, struct bfo_workspace *);

/* Default/KERNEL all5 operations only returnGATED; no callbacks/output change.
 * Host execution requires BFL/BFO/BFD/BMI fake flags and fake accessor. */
int bfl_fake_ledger_compiled(void);
#ifdef __cplusplus
}
#endif
#endif
