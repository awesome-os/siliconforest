/* Original bounded metadata policy. No provider, device or backing authority. */
#ifndef BCM_PRIVATE_LIFECYCLE_H
#define BCM_PRIVATE_LIFECYCLE_H
#include <stddef.h>
#include <stdint.h>

#define BPL_SLOTS 4u
#define BPL_RANGES 8u
#define BPL_SPANS 3u
#define BPL_EFFECTS 32u
#define BPL_PAGE UINT64_C(0x4000)
#define BPL_READ 1u
#define BPL_WRITE 2u
enum bpl_status { BPL_OK=0, BPL_ARGUMENT=-1, BPL_STATE=-2,
    BPL_CONFLICT=-3, BPL_RANGE=-4, BPL_STALE=-5, BPL_LIMIT=-6,
    BPL_RETAINED=-7 };
enum bpl_state { BPL_UNUSED, BPL_CLAIMED, BPL_PRIVATE_METADATA,
    BPL_EFFECT_RECORDED, BPL_QUARANTINED, BPL_RETIRED };
enum bpl_role { BPL_ROOT=1, BPL_LEAF=2, BPL_DATA=3 };
enum bpl_kind { BPL_CONFIG=1, BPL_MAPPING, BPL_DESCRIPTOR,
    BPL_PRODUCER, BPL_DOORBELL, BPL_INTERRUPT };
enum bpl_outcome { BPL_PENDING, BPL_REPORTED_SUCCESS,
    BPL_REPORTED_FAILURE, BPL_UNKNOWN };
/* Resource bits: root=1, leaf=2, data=4, endpoint=8, IRQ=16.
 * Minimum masks: CONFIG8, MAPPING7, DESCRIPTOR12, PRODUCER12, DOORBELL8,
 * INTERRUPT16. Extra conservative bits allowed. Table/data bits require bound
 * metadata; this is a bookkeeping rule, never backing authentication. */
#define BPL_RESOURCES 31u
struct bpl_range { uint64_t base, bytes; };
struct bpl_policy {
    uint64_t platform_domain, dart_domain, irq_domain; /* trusted labels only */
    uint32_t ram_count, reserved_count;
    struct bpl_range ram[BPL_RANGES], reserved[BPL_RANGES];
};
struct bpl_request { uint32_t rid, sid, vectors; };
struct bpl_span {
    uint32_t role, permissions;
    uint64_t handle, generation; /* numeric metadata, not provider attestation */
    uint64_t cpu, pa, bytes, iova; /* never dereferenced; table iova must be0 */
};
struct bpl_effect {
    uint32_t sequence, kind, resources, outcome;
    int32_t error;
};
struct bpl_entry {
    uint64_t generation;
    uint32_t state, span_count, effect_count, pending;
    uint32_t ever_resources;
    int32_t first_error;
    struct bpl_request request;
    struct bpl_span spans[BPL_SPANS];
    struct bpl_effect effects[BPL_EFFECTS];
};
struct bpl_registry {
    uint64_t magic, next_generation;
    uint32_t exhausted;
    struct bpl_policy policy;
    struct bpl_entry entries[BPL_SLOTS];
};
struct bpl_lease {
    const struct bpl_registry *registry;
    uint64_t generation;
    uint32_t slot;
};
struct bpl_ticket { struct bpl_lease lease; uint32_t sequence; };
struct bpl_snapshot {
    uint32_t state, span_count, effect_count, pending, ever_resources;
    int32_t first_error;
};

/* Caller owns one stable zero-initialized registry per platform resource
 * domain, and serializes every call and all access to its storage. No callbacks
 * or allocations occur. Fields are visible for storage, NOT caller mutation.
 * No memset/reinit recovery; separate registries do not establish exclusivity.
 * Inputs/outputs must be ordinary accessible objects; output may not overlap
 * registry or inputs. Accessible object validity cannot be checked by this C API.
 * Token copies refer to the same lease, never a new owner. Numeric spans are
 * copied and validated but NOT prepared, mapped or authenticated backing.
 */
int bpl_init(struct bpl_registry *, const struct bpl_policy *);
int bpl_claim(struct bpl_registry *, const struct bpl_request *, struct bpl_lease *);
int bpl_bind_metadata(struct bpl_registry *, const struct bpl_lease *, const struct bpl_span [BPL_SPANS]);
/* Journals possible effect BEFORE caller may invoke any future backend.
 * This core cannot force an external caller to obey that ordering. Ticket is
 * only an operation identity, never hardware permission. Any journaled effect
 * permanently prevents private retirement, even a reported success/no-op.
 */
int bpl_effect_begin(struct bpl_registry *, const struct bpl_lease *,
                     uint32_t kind, uint32_t resources, struct bpl_ticket *);
int bpl_effect_result(struct bpl_registry *, const struct bpl_ticket *,
                      uint32_t outcome, int32_t error);
int bpl_quarantine(struct bpl_registry *, const struct bpl_lease *, int32_t error);
/* Releases metadata reservations ONLY. Does not call a provider or free any
 * storage. Quarantined/exposed entries can never retire through this API. */
int bpl_retire_private(struct bpl_registry *, const struct bpl_lease *);
int bpl_inspect(const struct bpl_registry *, const struct bpl_lease *, struct bpl_snapshot *);
#endif
