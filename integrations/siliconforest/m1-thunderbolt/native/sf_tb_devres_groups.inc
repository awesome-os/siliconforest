/* SPDX-License-Identifier: GPL-2.0 */
/* Group-marker/rollback port from Linux drivers/base/devres.c (6a08a08e),
 * Copyright (c) 2006 SUSE Linux Products GmbH
 * Copyright (c) 2006 Tejun Heo <teheo@suse.de>
 * Adapted to the private BSD LinuxKPI devres layout/newest-first list.
 * Included after linux_devres.c, retaining all of that file's BSD notices.
 * The CM/component owner serializes group structure changes. Resource adds
 * and list extraction use devres_lock; release callbacks run outside it.
 * These callbacks retire resources; the caller must first stop/join hardware
 * producers, DMA and IRQ users. No hardware drain is inferred.
 * Groups must be properly nested: crossing closes fault the native adapter
 * before changing the list. This is the subset used by component.c.
 */
void sf_tb_native_fault(int error);
_Static_assert(offsetof(struct devres,__drdata)==sizeof(struct devres),"private LinuxKPI devres layout changed");
struct sf_tb_resource_group;
struct sf_tb_group_marker { struct sf_tb_resource_group *group; };
struct sf_tb_resource_group {
 void *id;
 struct sf_tb_group_marker *open,*close;
 bool closed;
};
static void sf_tb_group_close_release(struct device *dev,void *data)
{
 (void)dev;struct sf_tb_group_marker *marker=data;
 marker->group->close=NULL;
}
static void sf_tb_group_open_release(struct device *dev,void *data)
{
 (void)dev;struct sf_tb_group_marker *marker=data;
 struct sf_tb_resource_group *group=marker->group;
 /* An unclosed group reserved its closing marker but never linked it. */
 if(group->close)lkpi_devres_free(group->close);
 kfree(group);
}
static struct devres *sf_tb_group_dr(void *data)
{ return container_of(data,struct devres,__drdata); }
/* Caller holds devres_lock. NULL means the latest still-open group. */
static struct sf_tb_resource_group *sf_tb_find_group(struct device *dev,void *id)
{
 struct devres *dr;
 list_for_each_entry(dr,&dev->devres_head,entry) {
  if(dr->release!=sf_tb_group_open_release)continue;
  struct sf_tb_resource_group *g=((struct sf_tb_group_marker *)(dr+1))->group;
  if(id?g->id==id:!g->closed)return g;
 }
 return NULL;
}
void *devres_open_group(struct device *dev,void *id,gfp_t flags)
{
 if(!dev)return NULL;
 struct sf_tb_resource_group *g=kzalloc(sizeof(*g),flags);
 if(!g)return NULL;
 g->open=lkpi_devres_alloc(sf_tb_group_open_release,sizeof(*g->open),flags);
 g->close=lkpi_devres_alloc(sf_tb_group_close_release,sizeof(*g->close),flags);
 if(!g->open || !g->close) {
  lkpi_devres_free(g->open);lkpi_devres_free(g->close);kfree(g);return NULL;
 }
 g->id=id?id:g;g->open->group=g;g->close->group=g;
 lkpi_devres_add(dev,g->open);return g->id;
}
void devres_close_group(struct device *dev,void *id)
{
 if(!dev){sf_tb_native_fault(-EINVAL);return;}
 spin_lock(&dev->devres_lock);
 struct sf_tb_resource_group *g=sf_tb_find_group(dev,id);
 /* Never close through a still-open nested group or link a marker twice. */
 if(!g || g->closed || sf_tb_find_group(dev,NULL)!=g) {
  spin_unlock(&dev->devres_lock);sf_tb_native_fault(-EINVAL);return;
 }
 g->closed=true;list_add(&sf_tb_group_dr(g->close)->entry,&dev->devres_head);
 spin_unlock(&dev->devres_lock);
}
void devres_remove_group(struct device *dev,void *id)
{
 if(!dev){sf_tb_native_fault(-EINVAL);return;}
 spin_lock(&dev->devres_lock);
 struct sf_tb_resource_group *g=sf_tb_find_group(dev,id);
 if(!g){spin_unlock(&dev->devres_lock);sf_tb_native_fault(-ENOENT);return;}
 list_del_init(&sf_tb_group_dr(g->open)->entry);
 if(g->closed)list_del_init(&sf_tb_group_dr(g->close)->entry);
 spin_unlock(&dev->devres_lock);
 lkpi_devres_free(g->close);lkpi_devres_free(g->open);kfree(g);
}
int devres_release_group(struct device *dev,void *id)
{
 if(!dev)return -EINVAL;
 LIST_HEAD(todo);
 spin_lock(&dev->devres_lock);
 struct sf_tb_resource_group *g=sf_tb_find_group(dev,id);
 if(!g){spin_unlock(&dev->devres_lock);sf_tb_native_fault(-ENOENT);return -ENOENT;}
 struct list_head *last=&sf_tb_group_dr(g->open)->entry;
 struct list_head *first=g->closed?&sf_tb_group_dr(g->close)->entry:dev->devres_head.next;
 /* Prove the whole interval before extracting it. No speculative cleanup. */
 struct list_head *p=first;
 while(p!=&dev->devres_head && p!=last)p=p->next;
 if(p!=last){spin_unlock(&dev->devres_lock);sf_tb_native_fault(-EINVAL);return -EINVAL;}
 for(;;) {
  p=first;first=p->next;bool done=p==last;
  list_del_init(p);list_add_tail(p,&todo);if(done)break;
 }
 spin_unlock(&dev->devres_lock);
 int count=0;
 while(!list_empty(&todo)) {
  struct devres *dr=list_first_entry(&todo,struct devres,entry);
  list_del_init(&dr->entry);
  if(dr->release!=sf_tb_group_open_release && dr->release!=sf_tb_group_close_release)++count;
  if(dr->release)dr->release(dev,(void *)(dr+1));
  lkpi_devres_free_dr(dr);
 }
 return count;
}
