/* Original bounded CPU-control protection derivation, not DMA authority. */
#ifndef BCM_OWNED_CONTROL_PROTECTION_H
#define BCM_OWNED_CONTROL_PROTECTION_H
#include "../native_owned/owned.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BOC_PAGES 8u
#define BOC_VERSION 1u
enum boc_status { BOC_OK=0, BOC_ARGUMENT=-200, BOC_POLICY=-201,
    BOC_PROFILE=-202, BOC_LIMIT=-203, BOC_MAPPING=-204, BOC_CHANGED=-205,
    BOC_BINDING=-206, BOC_NATIVE=-207 };
struct boc_source {
    uint64_t pmap_identity, mutex_cpu;
    uint32_t page_shift, page_size, native_page_size, mutex_size, mutex_align;
};
struct boc_reads {
    void *context;
    int (*observe)(void *,uint64_t opaque_mutex,struct boc_source *);
    int (*lookup)(void *,uint64_t pmap_identity,uint64_t page_va,uint32_t *ppnum);
};
struct boc_boot_ops {
    struct boc_reads reads;
    int (*pin_empty)(void *,struct bno_native_setup *);
    int (*configure)(void *,const struct bno_native_setup *,const struct bpl_policy *,const struct bno_protection *);
};
struct boc_receipt {
    uint32_t version, complete, configured, page_count, query_count;
    char provider_freeze[65], core_freeze[65], type_profile[65];
    struct bno_native_setup setup;
    struct boc_source source;
    struct bpl_range mutex_span;
    uint64_t cpu_pages[BOC_PAGES], pa_pages[BOC_PAGES];
    uint32_t ppnums[BOC_PAGES];
    struct bpl_policy base_policy, final_policy;
    struct bno_protection protection;
};
/* Caller heap/static workspace, never a large kernel-stack temporary. Its
 * contents may change on failure. These fields are not authorization to call
 * the raw frozen configure API; callers must not mutate them during an operation. */
struct boc_workspace {
    struct boc_receipt expected, candidate;
    struct bpl_policy original_policy;
    struct bno_native_setup original_setup;
    struct boc_source original_source;
    uint64_t pages[BOC_PAGES];
    uint32_t first[BOC_PAGES], second[BOC_PAGES], count;
    struct bpl_range union_ranges[BPL_RANGES+BOC_PAGES];
};
/* Pure preflight: accessible, immutable objects and serialized boot lifetime
 * remain caller obligations. Output/workspace/input byte ranges must not alias.
 * No allocation, callbacks or writes on this preflight path. */
int boc_preflight(const struct bpl_policy *,struct boc_workspace *,struct boc_receipt *);
/* Low-level TRUSTED/internal/test entry only. Raw setup/read callbacks must
 * originate from held pinned controls, never userspace or arbitrary addresses.
 * Outputs unchanged on failure. Two equal samples are not a no-remap guarantee. */
int boc_derive(const struct bno_native_setup *,const struct bpl_policy *,
               const struct boc_reads *,struct boc_workspace *,struct boc_receipt *);
/* Internal receipt consistency check, NOT detached-receipt authentication. */
int boc_check_workspace(const struct boc_workspace *);
/* Trusted composition entry obtains setup via pin, derives fresh receipt and
 * calls configure once. It never accepts a detached receipt or caller setup.
 * The real native pin callback enforces one attempt. No automatic retry. */
int boc_boot_configure(const struct bpl_policy *,const struct boc_boot_ops *,
                       struct boc_workspace *,struct boc_receipt *);
/* Genuine kernel API adapter. No caller raw setup/address or detached receipt. */
int boc_native_derive_and_configure(const struct bpl_policy *,struct boc_workspace *,struct boc_receipt *);
#ifdef __cplusplus
}
#endif
#endif
