// SPDX-License-Identifier: GPL-2.0
/* T8103 ACIO lifecycle, adapted from apple_cio_start/stop in Linux apple.c.
 * Copyright (c) Sven Peter <sven@kernel.org>
 * The native backend must drain children/callbacks before any power removal.
 */
#include "sf_cio_lifecycle.h"
#include <errno.h>
static bool complete(const struct sf_cio_ops *o)
{
    return o && o->phy_ready && o->pcie_reset_unbound && o->power_get &&
        o->reset_deassert && o->rtkit_start && o->firmware_ready &&
        o->apply_tunables && o->children_add && o->domain_ready && o->set_cable &&
        o->children_remove_drain && o->rtkit_stop_drain && o->power_put;
}
int sf_cio_stop(struct sf_cio_session *s,const struct sf_cio_ops *o)
{
    if(!s || !complete(o))return -EINVAL;
    int ret;
    s->ready=false;s->closing=true;
    if(s->children) {
        ret=o->children_remove_drain(o->ctx);
        if(ret)return ret;
        s->children=false;
    }
    if(s->rtkit) {
        ret=o->rtkit_stop_drain(o->ctx);
        if(ret)return ret;
        s->rtkit=false;
    }
    if(s->power) {
        ret=o->power_put(o->ctx);
        if(ret)return ret;
        s->power=false;
    }
    s->cable_info=0;s->closing=false;
    return 0;
}
int sf_cio_start(struct sf_cio_session *s,const struct sf_cio_ops *o,uint32_t info)
{
    if(!s || !complete(o) || !(info&1) || (info&~0x61fu))return -EINVAL;
    if(s->ready && s->cable_info==info)return 0;
    if(s->power || s->rtkit || s->children || s->closing) {
        int ret=sf_cio_stop(s,o);
        return ret?ret:-EINVAL; // cable parameters change only across full OFF
    }
    if(s->generation==UINT64_MAX)return -EOVERFLOW;
    int ret=o->phy_ready(o->ctx,info);
    if(ret)return ret;
    if((ret=o->pcie_reset_unbound(o->ctx)))return ret;
    s->power=true;
    if((ret=o->power_get(o->ctx)))goto fail;
    if((ret=o->reset_deassert(o->ctx)))goto fail;
    s->rtkit=true;
    if((ret=o->rtkit_start(o->ctx)))goto fail;
    if((ret=o->firmware_ready(o->ctx)))goto fail;
    if((ret=o->apply_tunables(o->ctx)))goto fail;
    s->children=true;
    if((ret=o->children_add(o->ctx)))goto fail;
    if((ret=o->domain_ready(o->ctx)))goto fail;
    if((ret=o->set_cable(o->ctx,info)))goto fail;
    s->cable_info=info;s->ready=true;
    ++s->generation;
    return 0;
fail:
    // Preserve the first startup failure; state records any failed cleanup,
    // and prevents restart/PHY removal while an owner remains undrained.
    sf_cio_stop(s,o);
    return ret;
}
