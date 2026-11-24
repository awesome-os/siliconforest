/* Original private DATA owner; license selection pending. No table ownership. */
#ifndef UDATA_H
#define UDATA_H
#include "../m1-gpu-uat-builder/builder.h"
#ifdef __cplusplus
extern "C" {
#endif
#define UDATA_CAPACITY 16u
enum udata_code { UD_OK, UD_ARGUMENT, UD_STATE, UD_GRAPH, UD_LIMIT, UD_ALLOC,
    UD_PREPARE, UD_ZERO, UD_VIEW, UD_OVERLAP, UD_PERMISSION, UD_PLAN, UD_QUARANTINE };
enum udata_state { UD_EMPTY, UD_OWNED, UD_PREPARED, UD_RETAINED };
enum udata_zero_result { UD_ZEROED, UD_ZERO_FAILED_PRIVATE, UD_ZERO_UNCERTAIN };
enum udata_permission { UD_READ = 1, UD_WRITE = 2, UD_EXEC = 4 };
struct udata_context;
struct udata_token { const struct udata_context *owner; uint64_t generation; unsigned slot; };
struct udata_view {
    const uint8_t *bytes;
    uint64_t length, capacity, page_size, physical, segment;
};
/* Each allocation transfers a distinct DATA owner reference or returns NULL.
 * prepare=0 owns one successful explicit pair; complete nonzero is uncertain.
 * zero writes only the exact expected private view; non-private uncertainty
 * must return UD_ZERO_UNCERTAIN. No publication or asynchronous consumers. */
struct udata_ops {
    void *(*allocate)(void *);
    int (*prepare)(void *, void *);
    int (*inspect)(void *, void *, struct udata_view *);
    enum udata_zero_result (*zero)(void *, void *, const struct udata_view *);
    int (*complete)(void *, void *);
    void (*release)(void *, void *);
};
struct udata_slot { void *handle; struct udata_view view; uint64_t generation; enum udata_state state; };
struct udata_context {
    const struct udata_context *self;
    struct udata_ops ops;
    void *provider;
    struct uatb_context *graph;
    struct uatb_ticket graph_ticket;
    struct udata_slot slots[UDATA_CAPACITY];
    uint64_t next_generation;
    unsigned busy, stopping, failed;
};
struct udata_mapping { uint64_t va; struct udata_token data; uint32_t permissions; };
/* Caller-owned heap/static scratch, NEVER kernel stack storage. The only leaf
 * writes are to this detached scratch; original table pages remain untouched. */
struct udata_workspace { struct duat_workspace graph; struct uat_range data_ranges[UDATA_CAPACITY]; };
struct udata_summary { size_t tables, data_pages; uint64_t mapped_bytes; };
/* Stable zeroed context, common external serialization with the borrowed graph.
 * Graph and its owner/code must outlive data context and every plan call.
 * Never copy/reset/reinitialize contexts or recycle while tokens exist. */
enum udata_code udata_init(struct udata_context *, const struct udata_ops *, void *,
                           struct uatb_context *, struct uatb_ticket);
enum udata_code udata_allocate(struct udata_context *, struct udata_token *);
enum udata_code udata_release(struct udata_context *, struct udata_token);
enum udata_code udata_quarantine(struct udata_context *, struct udata_token);
enum udata_code udata_shutdown(struct udata_context *);
/* <=16 unique one-page mappings; no VA/data aliases, only RO/RW. Failure leaves
 * summary unchanged, but scratch is unspecified. Success is point-in-time
 * statistics, NOT a reusable ticket or authorization to install the scratch. */
enum udata_code udata_plan(struct udata_context *, const struct udata_mapping *, size_t,
    struct udata_workspace *, size_t workspace_bytes, struct udata_summary *);
/* Pure numeric policy: denies EXEC and every unknown bit. No raw flags input. */
enum udata_code udata_leaf_flags(uint32_t permissions, uint64_t *out);
enum udata_code udata_iokit_init(struct udata_context *, struct uatb_context *, struct uatb_ticket);
#ifdef __cplusplus
}
#endif
#endif
