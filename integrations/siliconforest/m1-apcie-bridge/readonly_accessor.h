/* Original host-only wiring for the unchanged read-only port observer. */
#ifndef DL_APCIE_READONLY_ACCESSOR_H
#define DL_APCIE_READONLY_ACCESSOR_H
#include "register_accessor.h"
#include "observe.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Caller-owned, single-threaded; stable through the observer call. Only a
 * read32 capability is exported. The underlying callbacks are the existing
 * host fake file, never a device or mapping. All register_accessor.h build
 * refusals and lifetime restrictions apply here too. */
struct apr_observer {
    struct apr_accessor checked;
    struct apb_io port;
};

/* Explicit region translation: DLS_PORT is 3, whereas APB_PORT0 is 6.
 * Only the observer's ten allowed register pairs pass. DLS_DART never passes.
 * Failed binding preserves wire and observer_io; fake must not alias wire. */
int apr_bind_observer_fake(struct apr_observer *wire, const struct apb_io *fake,
                           struct dls_io *observer_io);

/* Calls the real, unchanged apo_observe through the checked read-only path.
 * Failed binding returns APO_ARGUMENT and initializes out as apo_observe does.
 * No automatic retry, power change, write, pin call or delay is added. */
int apo_observe_fake_accessor(const struct dls_board *board, const struct apb_io *fake,
                              struct apo_snapshot *out);

#ifdef __cplusplus
}
#endif
#endif
