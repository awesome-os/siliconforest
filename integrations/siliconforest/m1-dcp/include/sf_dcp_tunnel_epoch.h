/* SPDX-License-Identifier: MIT */
/* SFDcp-private bridge storage, NEVER passed to CIO. Caller serialization and
 * actual retained local connector/owner references are required. Each object
 * has one immutable generation/callback identity; retire never recycles it.
 */
#ifndef SF_DCP_TUNNEL_EPOCH_H
#define SF_DCP_TUNNEL_EPOCH_H
#include "sf_dcp_tunnel_request.h"
enum sf_dcp_tunnel_state { SF_DCP_TUNNEL_NEW,SF_DCP_TUNNEL_BOUND,
    SF_DCP_TUNNEL_ACTIVE,SF_DCP_TUNNEL_RETIRED,SF_DCP_TUNNEL_FAILED };
struct sf_dcp_tunnel_epoch {
    void *connector; /* SFDcp's own retained device_node, not CIO's node. */
    const void *owner;
    uint64_t generation;
    uint32_t dpin, state;
    int (*set_active)(void *,uint64_t,bool);
    void *context;
    bool accepting, closing;
};
#ifdef __cplusplus
extern "C" {
#endif
/* Connector/owner lifetime and provider power are established by the native
 * provider BEFORE init/call. These functions publish no provider/route/PHY.
 * The real provider must serialize calls, retain all storage while Linux owns
 * callbacks, and detach/drain before power loss or object reclamation.
 */
int sf_dcp_tunnel_epoch_init(struct sf_dcp_tunnel_epoch *,void *connector,const void *owner);
int sf_dcp_tunnel_epoch_call(struct sf_dcp_tunnel_epoch *,const void *owner,
                            const struct sf_dcp_tunnel_request *);
#ifdef __cplusplus
}
#endif
#endif
