/* SPDX-License-Identifier: MIT */
/* Host-only kobject reference ABI, not native discovery or hardware. */
#ifndef SF_TB_TEST_DEVICE_H
#define SF_TB_TEST_DEVICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define EPROBE_DEFER 517
#define EAUTH 80
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct kref { unsigned refcount; };
struct kobject;
struct kobj_type { void (*release)(struct kobject *); };
struct kobject { struct kref kref;const struct kobj_type *ktype;char *name; };
struct list_head { int initialized; };
struct bus_type;struct kobj_uevent_env;
struct device;
struct device_type { const char *name;void (*release)(struct device *);int (*uevent)(const struct device *,struct kobj_uevent_env *); };
struct device_driver { int unused; };
struct class { int unused; };
struct device {
 const struct bus_type *bus;bool sf_uevent_suppress,sf_wakeup_enabled,sf_wakeup_capable,sf_registered,sf_parent_held;
 void *sf_native_token;struct device *parent;struct device_type *type;
 void (*release)(struct device *);struct class *class;struct kobject kobj;
 int devres_lock;struct list_head devres_head,irqents;
};
static inline bool kref_get_unless_zero(struct kref *r) { if(!r->refcount)return false;++r->refcount;return true; }
static inline void kobject_init(struct kobject *k,const struct kobj_type *t) { k->kref.refcount=1;k->ktype=t; }
static inline struct device *get_device(struct device *d) { if(d)++d->kobj.kref.refcount;return d; }
static inline void put_device(struct device *d) {
 if(d && !--d->kobj.kref.refcount) { char *name=d->kobj.name;d->kobj.ktype->release(&d->kobj);free(name); }
}
static inline struct device *kobj_to_dev(struct kobject *k) { return container_of(k,struct device,kobj); }
static inline const char *dev_name(struct device *d) { return d->kobj.name; }
static inline void lkpi_devres_release_free_list(struct device *d) { (void)d; }
#define INIT_LIST_HEAD(p) ((p)->initialized=1)
#endif
