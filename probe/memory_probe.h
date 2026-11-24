/* Original Darwin Lab code. License selection pending.
 * Serialized RAM probe engine shared by host tests and the unbuilt XNU adapter.
 * Only the mapping release callback is asynchronous. Never free service storage
 * while a callback producer or published ticket can still exist.
 */
#ifndef DARWIN_LAB_MEMORY_PROBE_H
#define DARWIN_LAB_MEMORY_PROBE_H
#include "backing_store.h"
#include "memory_probe_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DLMP_SESSION_CAPACITY 32u
#define DLMP_TICKET_CAPACITY 256u
typedef struct dl_probe dl_probe;
typedef struct {
    dl_backing_ops backing;
    int (*pages)(void *context, void *cookie, uint64_t offset, uint64_t bytes,
                 uint64_t page_size, uint32_t *pages, size_t count);
    /* Infallible provider notification BEFORE the successful mapping handoff.
     * The engine separately records export by buffer index and serial and
     * refuses mapped reclamation without DLMP_CAP_MAPPED_RECLAIM, even if this
     * callback does nothing. Providers must preserve their own quarantine. */
    void (*exported)(void *context, void *cookie);
    /* MAPPED_RECLAIM requires an independently verified finalization boundary.
     * The engine never adds it and has no runtime quarantine override. */
    uint64_t capabilities;
} dl_probe_ops;
size_t dl_probe_size(void);
size_t dl_probe_alignment(void);
int dl_probe_init(void *storage, size_t size, uint64_t page_size,
                  uint64_t budget, dl_probe_ops, void *context, dl_probe **out);
int dl_probe_open(dl_probe *, uint64_t *session);
int dl_probe_close(dl_probe *, uint64_t session);
/* Buffers must be suitably aligned C objects of exactly the expected size.
 * STATS counts current quarantined allocations, including live owners/maps.
 * COLLECT counts only engine refusals reached after owner/lease checks; these
 * also contribute to backend_failures. Provider errors remain separate. */
int dl_probe_command(dl_probe *, uint64_t session, unsigned operation,
                     void *message, size_t size);
/* On failure release/release_arg stay unchanged and no callback is published.
 * On success the platform must call the callback after its verified pager
 * release boundary, including failure after accepting the handoff. */
int dl_probe_map(dl_probe *, uint64_t session, uint64_t offset, uint64_t bytes,
                 unsigned access, uint32_t *pages, size_t count,
                 void (**release)(void *), void **release_arg);
#ifdef __cplusplus
}
#endif
#endif
