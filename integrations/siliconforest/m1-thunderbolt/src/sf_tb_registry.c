// SPDX-License-Identifier: MIT
/* Native reference-safe registry for LinuxKPI bus/child/class traversal.
 * Device callbacks supply actual kobject references; this code establishes
 * no hardware or backend completion facts.
 */
#include "sf_tb_registry.h"
#include <errno.h>
#include <string.h>
static bool matches(const struct sf_tb_member *m,enum sf_tb_match kind,void *key)
{
    return m->device && (kind==SF_TB_MATCH_BUS?m->bus:kind==SF_TB_MATCH_PARENT?m->parent:m->device_class)==key;
}
int sf_tb_registry_init(struct sf_tb_registry *r,const struct sf_tb_registry_ops *ops)
{
    if(!r || !ops || !ops->lock || !ops->unlock || !ops->retain || !ops->release)return -EINVAL;
    if(r->initialized)return -EALREADY;
    struct sf_tb_registry_ops copy=*ops;
    memset(r,0,sizeof(*r));r->ops=copy;r->initialized=true;return 0;
}
int sf_tb_registry_add(struct sf_tb_registry *r,void *device,void *parent,void *bus,void *device_class)
{
    if(!r || !r->initialized || !device || device==parent)return -EINVAL;
    r->ops.lock(r->ops.ctx);
    struct sf_tb_member *slot=NULL;
    for(unsigned i=0;i<SF_TB_REGISTRY_CAPACITY;++i) {
        if(r->members[i].device==device){r->ops.unlock(r->ops.ctx);return -EEXIST;}
        if(!r->members[i].device && !slot)slot=&r->members[i];
    }
    if(!slot || r->serial>=UINT64_MAX-1){r->ops.unlock(r->ops.ctx);return !slot?-ENOSPC:-EOVERFLOW;}
    if(!r->ops.retain(r->ops.ctx,device)){r->ops.unlock(r->ops.ctx);return -ENODEV;}
    if(parent && !r->ops.retain(r->ops.ctx,parent)) {
        r->ops.unlock(r->ops.ctx);r->ops.release(r->ops.ctx,device);return -ENODEV;
    }
    *slot=(struct sf_tb_member){device,parent,bus,device_class,++r->serial};
    r->ops.unlock(r->ops.ctx);return 0;
}
int sf_tb_registry_remove(struct sf_tb_registry *r,void *device)
{
    if(!r || !r->initialized || !device)return -EINVAL;
    struct sf_tb_member old={0};
    r->ops.lock(r->ops.ctx);
    for(unsigned i=0;i<SF_TB_REGISTRY_CAPACITY;++i)if(r->members[i].device==device) {
        old=r->members[i];memset(&r->members[i],0,sizeof(r->members[i]));break;
    }
    r->ops.unlock(r->ops.ctx);
    if(!old.device)return -ENOENT;
    r->ops.release(r->ops.ctx,old.device);
    if(old.parent)r->ops.release(r->ops.ctx,old.parent);
    return 0;
}
int sf_tb_registry_begin(struct sf_tb_registry *r,struct sf_tb_registry_iter *out,
                          enum sf_tb_match kind,void *key,void *start,bool reverse)
{
    if(!r || !r->initialized || !out || (unsigned)kind>SF_TB_MATCH_CLASS)return -EINVAL;
    struct sf_tb_registry_iter iter={kind,key,0,reverse?UINT64_MAX:0,reverse};
    r->ops.lock(r->ops.ctx);iter.watermark=r->serial;
    if(start) {
        bool found=false;
        for(unsigned i=0;i<SF_TB_REGISTRY_CAPACITY;++i)if(matches(&r->members[i],kind,key) && r->members[i].device==start) {
            iter.cursor=r->members[i].serial;found=true;break;
        }
        if(!found){r->ops.unlock(r->ops.ctx);return -ENOENT;}
    }
    r->ops.unlock(r->ops.ctx);*out=iter;return 0;
}
int sf_tb_registry_next(struct sf_tb_registry *r,struct sf_tb_registry_iter *it,struct sf_tb_registry_ref *out)
{
    if(!r || !r->initialized || !it || !out || (unsigned)it->match>SF_TB_MATCH_CLASS)return -EINVAL;
    struct sf_tb_member *next=NULL;
    r->ops.lock(r->ops.ctx);
    for(unsigned i=0;i<SF_TB_REGISTRY_CAPACITY;++i) {
        struct sf_tb_member *m=&r->members[i];
        if(!matches(m,it->match,it->key) || m->serial>it->watermark ||
           (it->reverse?m->serial>=it->cursor:m->serial<=it->cursor))continue;
        if(!next || (it->reverse?m->serial>next->serial:m->serial<next->serial))next=m;
    }
    if(!next){r->ops.unlock(r->ops.ctx);*out=(struct sf_tb_registry_ref){0};return 0;}
    if(!r->ops.retain(r->ops.ctx,next->device)){r->ops.unlock(r->ops.ctx);return -ENODEV;}
    if(next->parent && !r->ops.retain(r->ops.ctx,next->parent)) {
        void *device=next->device;r->ops.unlock(r->ops.ctx);
        r->ops.release(r->ops.ctx,device);return -ENODEV;
    }
    it->cursor=next->serial;*out=(struct sf_tb_registry_ref){next->device,next->parent};
    r->ops.unlock(r->ops.ctx);return 1;
}
void sf_tb_registry_put(struct sf_tb_registry *r,struct sf_tb_registry_ref *ref)
{
    if(!r || !r->initialized || !ref)return;
    struct sf_tb_registry_ref old=*ref;*ref=(struct sf_tb_registry_ref){0};
    if(old.device)r->ops.release(r->ops.ctx,old.device);
    if(old.parent)r->ops.release(r->ops.ctx,old.parent);
}
