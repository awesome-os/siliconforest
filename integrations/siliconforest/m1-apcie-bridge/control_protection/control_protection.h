/* Original bounded CPU-control protection derivation, not DMA authority. */
#ifndef BCM_CONTROL_PROTECTION_H
#define BCM_CONTROL_PROTECTION_H
#include "../native_backing/native_iokit.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BCP_PAGES 8u
#define BCP_VERSION 1u
enum bcp_status { BCP_OK=0, BCP_ARGUMENT=-200, BCP_POLICY=-201,
    BCP_PROFILE=-202, BCP_LIMIT=-203, BCP_MAPPING=-204, BCP_CHANGED=-205,
    BCP_BINDING=-206, BCP_NATIVE=-207 };
struct bcp_source {
    uint64_t pmap_identity, mutex_cpu;
    uint32_t page_shift, page_size, native_page_size, mutex_size, mutex_align;
};
struct bcp_reads {
    void *context;
    int (*observe)(void *,uint64_t opaque_mutex,struct bcp_source *);
    int (*lookup)(void *,uint64_t pmap_identity,uint64_t page_va,uint32_t *ppnum);
};
struct bcp_boot_ops {
    struct bcp_reads reads;
    int (*pin_empty)(void *,struct bpn_native_setup *);
    int (*configure)(void *,const struct bpn_native_setup *,const struct bpl_policy *,const struct bpn_protection *);
};
struct bcp_receipt {
    uint32_t version, complete, configured, page_count, query_count;
    char provider_freeze[65], core_freeze[65], type_profile[65];
    struct bpn_native_setup setup;
    struct bcp_source source;
    struct bpl_range mutex_span;
    uint64_t cpu_pages[BCP_PAGES], pa_pages[BCP_PAGES];
    uint32_t ppnums[BCP_PAGES];
    struct bpl_policy base_policy, final_policy;
    struct bpn_protection protection;
};
/* Caller heap/static workspace, never a large kernel-stack temporary. Its
 * contents may change on failure. These fields are not authorization to call
 * the raw frozen configure API; callers must not mutate them during an operation. */
struct bcp_workspace {
    struct bcp_receipt expected, candidate;
    struct bpl_policy original_policy;
    struct bpn_native_setup original_setup;
    struct bcp_source original_source;
    uint64_t pages[BCP_PAGES];
    uint32_t first[BCP_PAGES], second[BCP_PAGES], count;
    struct bpl_range union_ranges[BPL_RANGES+BCP_PAGES];
};
/* Pure preflight: accessible, immutable objects and serialized boot lifetime
 * remain caller obligations. Output/workspace/input byte ranges must not alias.
 * No allocation, callbacks or writes on this preflight path. */
int bcp_preflight(const struct bpl_policy *,struct bcp_workspace *,struct bcp_receipt *);
/* Low-level TRUSTED/internal/test entry only. Raw setup/read callbacks must
 * originate from held pinned controls, never userspace or arbitrary addresses.
 * Outputs unchanged on failure. Two equal samples are not a no-remap guarantee. */
int bcp_derive(const struct bpn_native_setup *,const struct bpl_policy *,
               const struct bcp_reads *,struct bcp_workspace *,struct bcp_receipt *);
/* Internal receipt consistency check, NOT detached-receipt authentication. */
int bcp_check_workspace(const struct bcp_workspace *);
/* Trusted composition entry obtains setup via pin, derives fresh receipt and
 * calls configure once. It never accepts a detached receipt or caller setup.
 * The real native pin callback enforces one attempt. No automatic retry. */
int bcp_boot_configure(const struct bpl_policy *,const struct bcp_boot_ops *,
                       struct bcp_workspace *,struct bcp_receipt *);
/* Genuine kernel API adapter. No caller raw setup/address or detached receipt. */
int bcp_native_derive_and_configure(const struct bpl_policy *,struct bcp_workspace *,struct bcp_receipt *);
#ifdef __cplusplus
}
#endif
#endif
