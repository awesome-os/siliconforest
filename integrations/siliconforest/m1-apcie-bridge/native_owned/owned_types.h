/* Public bounded values only; no backing/registry access. Original source. */
#ifndef BCM_OWNED_TYPES_H
#define BCM_OWNED_TYPES_H
#if defined(BNO_TEST_DISPATCH) && defined(KERNEL)
#error "TEST dispatcher cannot be compiled with KERNEL"
#endif
#include <stdint.h>
#include <stddef.h>
enum bno_status { BNO_OK=0, BNO_ARGUMENT=-100, BNO_BUSY=-101,
    BNO_STALE=-102, BNO_LIMIT=-103, BNO_RETAINED=-104, BNO_VALIDATION=-105,
    BNO_REFUSED=-106, BNO_PAGE=-107, BNO_UNAVAILABLE=-108 };
enum bno_phase { BNO_EMPTY, BNO_ACQUIRING, BNO_VALIDATED, BNO_INITIALIZED,
    BNO_RETIRING, BNO_CORE_RETIRED, BNO_REFS_RELEASED, BNO_QUARANTINED, BNO_EFFECT_PENDING, BNO_EFFECT_RETAINED };
struct bno_token { const void *anchor; uint64_t generation; uint32_t slot; };
struct bno_snapshot {
    uint64_t generation, sequence;
    uint32_t phase, busy, halted, metadata_bound, core_retired, call_pending;
    uint32_t identity_records, owned_pairs, balanced_pairs, resets_returned;
    int32_t first_error;
    uint32_t effect_count, effect_pending, ever_resources;
};

struct bno_request { uint32_t rid, sid, vectors; };
enum bno_operation { BNO_OP_CONFIG=1, BNO_OP_MAPPING, BNO_OP_DESCRIPTOR,
    BNO_OP_SUBMIT, BNO_OP_INTERRUPT };
/* No pointers/callbacks/addresses. DATA extents for DESCRIPTOR/SUBMIT, <=64B.
 * Others require offset=length=0. words are bounded test values only. */
struct bno_effect_request {
    uint32_t operation, reserved;
    uint64_t offset, length, words[2];
};
struct bno_effect_result { uint64_t sequence; uint32_t operation, ticket_sequence; };
#endif
