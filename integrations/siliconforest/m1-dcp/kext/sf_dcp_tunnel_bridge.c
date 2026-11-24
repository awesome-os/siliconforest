/* SPDX-License-Identifier: MIT */
/* Direct bridge to the pinned apple_dcp_tb_dp_tunnel implementation.
 * Its documented detach/failed-attach contract drains callbacks before return.
 * This is not native provider publication or a substitute for power ownership.
 */
#include <linux/errno.h>
#include <linux/soc/apple/dp-tunnel.h>
#include "sf_dcp_tunnel_epoch.h"

static int sf_dcp_tunnel_callback(void *cookie,bool active)
{
    struct sf_dcp_tunnel_epoch *epoch=cookie;
    if(!__atomic_load_n(&epoch->accepting,__ATOMIC_ACQUIRE) ||
       (active && __atomic_load_n(&epoch->closing,__ATOMIC_ACQUIRE)))return -ESTALE;
    return epoch->set_active(epoch->context,epoch->generation,active);
}

__attribute__((used))
int sf_dcp_tunnel_epoch_init(struct sf_dcp_tunnel_epoch *epoch,void *connector,const void *owner)
{
    if(!epoch || !connector || !owner || epoch->connector || epoch->owner ||
       epoch->state || epoch->accepting)return -EINVAL;
    epoch->connector=connector;epoch->owner=owner;
    return 0;
}

__attribute__((used))
int sf_dcp_tunnel_epoch_call(struct sf_dcp_tunnel_epoch *epoch,const void *owner,
                            const struct sf_dcp_tunnel_request *request)
{
    if(!epoch || !request || !epoch->connector || !epoch->owner ||
       request->version!=SF_DCP_TUNNEL_VERSION || request->size!=sizeof(*request) ||
       request->typec_port!=1 || request->dpin>1 || !request->generation ||
       request->active>1 || request->reserved || !request->set_active || !request->context)
        return -EINVAL;
    if(owner!=epoch->owner)return -EPERM;
    if(epoch->state==SF_DCP_TUNNEL_FAILED)return -EIO;
    if(epoch->state==SF_DCP_TUNNEL_RETIRED)return -ESTALE;
    if(epoch->state==SF_DCP_TUNNEL_NEW){
        if(!request->active)return -ESTALE;
        epoch->generation=request->generation;epoch->dpin=request->dpin;
        epoch->set_active=request->set_active;epoch->context=request->context;
        epoch->state=SF_DCP_TUNNEL_BOUND;
    }else if(epoch->generation!=request->generation || epoch->dpin!=request->dpin ||
             epoch->set_active!=request->set_active || epoch->context!=request->context)
        return -ESTALE;
    // Synchronous DCP ACTIVATE is allowed before this call returns. Identity
    // is already immutable and admitted; callback does not take control locks.
    __atomic_store_n(&epoch->closing,!request->active,__ATOMIC_RELEASE);
    __atomic_store_n(&epoch->accepting,true,__ATOMIC_RELEASE);
    int error=apple_dcp_tb_dp_tunnel((struct device_node *)epoch->connector,epoch->dpin,
        epoch->generation,request->active,sf_dcp_tunnel_callback,epoch);
    if(!request->active || error)
        __atomic_store_n(&epoch->accepting,false,__ATOMIC_RELEASE);
    if(!request->active)epoch->state=error?SF_DCP_TUNNEL_FAILED:SF_DCP_TUNNEL_RETIRED;
    else if(!error)epoch->state=SF_DCP_TUNNEL_ACTIVE;
    else if(error==-EBUSY || error==-EADDRINUSE)epoch->state=SF_DCP_TUNNEL_BOUND;
    else epoch->state=SF_DCP_TUNNEL_FAILED;
    // Do not clear/rebind the callback cookie, free backing, or report a
    // provider call as successful after an actual negative driver return.
    return error;
}
