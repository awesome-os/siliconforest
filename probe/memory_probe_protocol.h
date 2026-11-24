/* Original Darwin Lab code. License selection pending.
 * RAM-only lab protocol, NOT a stable graphics ABI. No pointers/physical
 * addresses appear in these messages. ioctl encodings come from the OS SDK.
 */
#ifndef DARWIN_LAB_MEMORY_PROBE_PROTOCOL_H
#define DARWIN_LAB_MEMORY_PROBE_PROTOCOL_H
#include <stdint.h>
#define DLMP_VERSION 1u
#define DLMP_CAP_RAM_ONLY UINT64_C(1)
#define DLMP_CAP_MAPPED_RECLAIM UINT64_C(2)
#define DLMP_CAP_KERNEL_BACKEND UINT64_C(4)
#define DLMP_MAX_ALLOCATION (UINT64_C(1024) * 1024)
#define DLMP_BUDGET (UINT64_C(8) * 1024 * 1024)
#define DLMP_OP_INFO 1u
#define DLMP_OP_ALLOC 2u
#define DLMP_OP_STATS 3u
#define DLMP_OP_COLLECT 4u
struct dlmp_info {
    uint32_t version, flags;
    uint64_t page_size, max_allocation, capabilities;
};
struct dlmp_alloc {
    uint32_t version, flags;
    uint64_t bytes, object_id, reserved[2];
};
struct dlmp_stats {
    uint32_t version, flags;
    uint64_t resident_bytes, live_buffers, open_sessions, published_maps;
    uint64_t pending_maps, drained_maps, reclaimed_buffers;
    uint64_t own_object, own_bytes;
    /* Current exported backing slots without MAPPED_RECLAIM, even while held. */
    uint64_t quarantined_buffers;
};
struct dlmp_collect {
    uint32_t version, flags;
    uint64_t reclaimed, retained, backend_failures, drained;
    /* Engine quarantine refusals in this collection, included in backend_failures. */
    uint64_t quarantined;
};
#ifdef __cplusplus
static_assert(sizeof(dlmp_info) == 32 && sizeof(dlmp_alloc) == 40 &&
              sizeof(dlmp_stats) == 88 && sizeof(dlmp_collect) == 48,
              "probe protocol layout changed");
#else
_Static_assert(sizeof(struct dlmp_info) == 32 && sizeof(struct dlmp_alloc) == 40 &&
               sizeof(struct dlmp_stats) == 88 && sizeof(struct dlmp_collect) == 48,
               "probe protocol layout changed");
#endif
#endif
