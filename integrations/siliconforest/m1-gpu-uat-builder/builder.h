/* Original CPU-only table topology builder; license selection pending. */
#ifndef UATB_BUILDER_H
#define UATB_BUILDER_H
#include "../m1-gpu-uat-private/private.h"
#ifdef __cplusplus
extern "C" {
#endif
#define UATB_NO_PARENT UINT32_MAX
enum uatb_code { UATB_OK, UATB_ARGUMENT, UATB_STATE, UATB_PLAN, UATB_LIMIT,
    UATB_ALLOC, UATB_VIEW, UATB_WRITE, UATB_VALIDATE, UATB_QUARANTINE };
enum uatb_phase { UATB_EMPTY, UATB_READY, UATB_BUILDING, UATB_VALID,
                  UATB_FAILED, UATB_CLOSED };
/* No leaf or permission flags accepted. Parent must precede child. For L1,
 * parent=NO_PARENT and entry selects detached root0/1. For L2 entry<8;
 * for L3 entry<2048. Repeated parent/entry, cycles and forward edges refuse. */
struct uatb_node { uint32_t level, parent, entry, permissions; };
enum uatb_write_result { UATB_WRITTEN, UATB_FAILED_PRIVATE, UATB_UNCERTAIN };
typedef enum uatb_write_result (*uatb_write_fn)(void *,
    const struct duat_context *, struct duat_token, size_t offset,
    const uint8_t *source, size_t length);
struct uatb_context;
struct uatb_ticket { const struct uatb_context *owner; uint64_t generation; };
struct uatb_context {
    const struct uatb_context *self;
    struct duat_context owner;
    struct uatb_node plan[DUAT_CAPACITY];
    struct duat_token tokens[DUAT_CAPACITY], roots[2];
    uatb_write_fn write;
    void *writer;
    size_t count, allocated;
    uint64_t generation;
    enum uatb_phase phase;
    enum uatb_code error;
    unsigned busy;
};
/* All storage is caller-owned, zeroed, stable, externally serialized and
 * disjoint. workspace is the frozen duat_workspace, never kernel stack RAM.
 * The builder exclusively owns its embedded duat_context and provider handles;
 * no external code may operate on that owner or reset/copy this context. */
enum uatb_code uatb_init(struct uatb_context *, const struct duat_ops *, void *,
                        uatb_write_fn, void *writer);
enum uatb_code uatb_build(struct uatb_context *, const struct uatb_node *, size_t,
    struct duat_workspace *, struct uatb_ticket *);
enum uatb_code uatb_validate(struct uatb_context *, struct uatb_ticket,
    struct duat_workspace *, struct duat_summary *);
enum uatb_code uatb_quarantine(struct uatb_context *, struct uatb_ticket);
/* Sticky failure survives successful private rollback/close. CLOSED/OK means
 * only private references released; never GPU quiescence or unload safety. */
enum uatb_code uatb_shutdown(struct uatb_context *);
#ifdef __cplusplus
}
#endif
#endif
