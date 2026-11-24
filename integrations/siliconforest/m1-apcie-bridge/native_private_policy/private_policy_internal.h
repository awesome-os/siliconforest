/* Trusted internal/test seam only. No caller backend on native public API. */
#ifndef BCM_PRIVATE_POLICY_INTERNAL_H
#define BCM_PRIVATE_POLICY_INTERNAL_H
#ifdef __cplusplus
#error private C atomics are not a public C++ layout
#endif
#include "private_policy.h"
#include <stdatomic.h>
enum npp_phase { NPP_EMPTY, NPP_READING, NPP_BUILDING, NPP_CONFIGURING,
                 NPP_CONFIGURED, NPP_FAILED };
struct npp_state { atomic_uint phase; };
struct npp_ops {
    void *context;
    size_t context_bytes;
    int (*read)(void *,const struct nps_expected *,struct npr_workspace *,struct nps_result *);
    int (*configure)(void *,const struct bpl_policy *,struct boc_workspace *,struct boc_receipt *);
};
/* Fresh isolated states only in host fixtures, never reset native resident state.
 * Context extent covers the supplied context object; reachable storage/code
 * and separate globals remain trusted caller obligations. Callback tables are
 * copied once, and original fields rechecked before/after configure. This is
 * not a sandbox or validation of arbitrary function/context pointers. */
int npp_with_ops(struct npp_state *,const struct npp_ops *,
                  const struct npp_expected *,struct npp_workspace *,struct npp_result *);
#endif
