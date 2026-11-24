/* Trusted internal synchronous coordinator; never an IOUserClient API. */
#ifndef BCM_PRIVATE_PROBE_H
#define BCM_PRIVATE_PROBE_H
#include "protocol.h"
#include "../native_private_policy/private_policy.h"
#include "../native_owned/native_owned.h"
#ifdef __cplusplus
extern "C" {
#endif
struct bcp_workspace {
    struct npp_expected expected;
    struct npp_workspace policy;
    struct npp_result configured;
    struct bno_token token;
    struct bno_snapshot snapshot;
};
struct bcp_ops {
    void *context;
    int (*labels)(void *,struct nps_expected *);
    int (*configure)(void *,const struct npp_expected *,struct npp_workspace *,struct npp_result *);
    int (*acquire)(void *,const struct bno_request *,uint64_t,uint32_t,struct bno_token *);
    int (*zero)(void *,const struct bno_token *);
    int (*copy)(void *,const struct bno_token *,uint64_t,const void *,size_t);
    int (*inspect)(void *,uint32_t,struct bno_snapshot *);
    int (*retire)(void *,const struct bno_token *);
};
/* Caller has consumed the permanent driver CAS and provides distinct accessible
 * stable heap objects/table/context. Ops are copied before calls, no asynchronous
 * callbacks/retry/reset. Actual driver supplies fixed wrappers only. */
void bcp_run(const struct bcp_ops *,struct bcp_workspace *,struct bcp_record *);
#ifdef __cplusplus
}
#endif
#endif
