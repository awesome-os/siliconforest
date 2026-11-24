// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "sf_tb_device_api.h"
#include <linux/utsname.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
struct node { struct device dev;unsigned index; };
static bool destroyed[16];static unsigned published,unpublished,events,pm_calls,forgotten;
static bool fail_publish,fail_event,bad_os;static int pm_result;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static const struct bus_type bus={.name="test"};
static void take_lock(void *ctx) { (void)ctx;assert(!pthread_mutex_lock(&lock)); }
static void drop_lock(void *ctx) { (void)ctx;assert(!pthread_mutex_unlock(&lock)); }
static int publish(void *ctx,void *dev,void *parent,const char *name,const char *type,void **token)
{ (void)ctx;(void)parent;(void)type;assert(name && *name);if(fail_publish)return -EIO;*token=dev;++published;return 0; }
static void unpublish(void *ctx,void *token) { (void)ctx;assert(token);++unpublished; }
static void forget(void *ctx,void *dev) { (void)ctx;assert(dev && !((struct device *)dev)->sf_registered);++forgotten; }
static int event(void *ctx,void *token,int action,const char *const *env,size_t count)
{ (void)ctx;assert(token);assert(action>=0 && action<=2);assert(count==1 && !strcmp(env[0],"TYPE=router"));++events;return fail_event?-EIO:0; }
static int runtime(void *ctx,void *dev,enum sf_tb_pm_op op,int arg)
{ (void)ctx;(void)arg;assert(dev);assert(op<=SF_TB_PM_WAKE_POLICY);++pm_calls;return pm_result; }
static int os_info(void *ctx,void *out,size_t size)
{ (void)ctx;assert(size==sizeof(struct new_utsname));memset(out,bad_os?'x':0,size);if(!bad_os)strcpy(((struct new_utsname *)out)->sysname,"host-test");return 0; }
static void destroy(struct device *dev)
{
 struct node *node=container_of(dev,struct node,dev);assert(!destroyed[node->index]);
 if(dev->sf_parent_held)assert(!destroyed[container_of(dev->parent,struct node,dev)->index]);
 destroyed[node->index]=true;free(node);
}
static int uevent(const struct device *dev,struct kobj_uevent_env *env) { (void)dev;return add_uevent_var(env,"TYPE=%s","router"); }
static struct device_type type={.name="router",.release=destroy,.uevent=uevent};
/* Host-only topology/publication fixture for the actual staged Linux function. */
struct tb_switch { struct device dev; };
struct tb_port { struct tb_switch *sw;int port;bool upstream; };
struct usb4_port { struct device dev;struct tb_port *port; };
static unsigned usb4_released,component_calls;
static void usb4_release(struct device *dev) { ++usb4_released;free(container_of(dev,struct usb4_port,dev)); }
static struct device_type usb4_port_device_type={.name="usb4_port",.release=usb4_release,.uevent=uevent};
static const int connector_ops;
static int component_add(struct device *dev,const int *ops) { assert(dev && ops==&connector_ops);++component_calls;return -EIO; }
static bool tb_is_upstream_port(struct tb_port *port) { return port->upstream; }
static void set_name(struct device *dev,int port) { char name[64];snprintf(name,sizeof(name),"usb4_port%d",port);dev->kobj.name=strdup(name); }
#define dev_set_name(dev,fmt,port) set_name((dev),(port))
#define dev_err(dev,...) ((void)(dev))
#define kzalloc_obj(obj) calloc(1,sizeof(obj))
#define TB_AUTOSUSPEND_DELAY 15000 /* actual tb.h constant, fixture only */
#define ERR_PTR(err) ((void *)(intptr_t)(err))
#include "usb4_port_add.inc"
static struct device *node(unsigned index,struct device *parent)
{
 struct node *n=calloc(1,sizeof(*n));assert(n);n->index=index;
 n->dev.kobj.name=strdup("router");device_initialize(&n->dev);
 assert(!strcmp(dev_name(&n->dev),"router"));n->dev.bus=&bus;n->dev.type=&type;n->dev.parent=parent;return &n->dev;
}
static int unregister_child(struct device *dev,void *ctx)
{
 struct device *parent=ctx;device_unregister(dev);assert(!destroyed[1]);
 device_unregister(parent);assert(!destroyed[0]);return 7;
}
static int count_child(struct device *dev,void *ctx) { (void)dev;++*(unsigned *)ctx;return 0; }
static int match(struct device *dev,const void *ctx) { return dev==ctx; }
int main(int argc,char **argv)
{
 (void)argv;assert(bus_register(&bus)==-EPROBE_DEFER);
 struct sf_tb_native_ops ops={NULL,take_lock,drop_lock,publish,unpublish,event,runtime,os_info,forget};
 struct sf_tb_native_ops missing=ops;missing.runtime=NULL;assert(sf_tb_native_install(&missing)==-EINVAL);
 bad_os=true;assert(sf_tb_native_install(&ops)==-EOVERFLOW);assert(!sf_tb_native_ready());bad_os=false;
 assert(!sf_tb_native_install(&ops));assert(sf_tb_native_install(&ops)==-EALREADY);
 assert(!strcmp(utsname()->sysname,"host-test"));assert(!bus_register(&bus));assert(bus_register(&bus)==-EBUSY);
 assert(driver_register(NULL)==-EOPNOTSUPP);
 struct device *parent=node(0,NULL);assert(!device_add(parent));
 struct tb_port port={(struct tb_switch *)parent,1,false};
 unsigned before_pm=pm_calls,before_refs=parent->kobj.kref.refcount;
 assert(usb4_port_device_add(&port)==ERR_PTR(-EIO));
 assert(usb4_released==1 && component_calls==1 && pm_calls==before_pm);
 assert(parent->kobj.kref.refcount==before_refs);
 struct device *child=node(1,parent);
 fail_publish=true;assert(device_add(child)==-EIO);assert(parent->kobj.kref.refcount==2);fail_publish=false;
 fail_event=true;assert(device_add(child)==-EIO);assert(!child->sf_registered && !child->sf_parent_held);fail_event=false;
 child->bus=NULL; /* USB4 port children have no bus. */
 assert(!device_add(child));assert(device_add(child)==-EEXIST);assert(parent->kobj.kref.refcount==4);
 struct device *found=device_find_child(parent,child,match);assert(found==child);put_device(found);
 unsigned count=0;assert(!device_for_each_child_reverse(parent,&count,count_child));assert(count==1);
 assert(!device_init_wakeup(child,true));assert(device_may_wakeup(child));
 device_set_wakeup_capable(child,false);assert(!device_may_wakeup(child));
 device_set_wakeup_capable(child,true);assert(!device_may_wakeup(child));
 assert(!device_init_wakeup(child,true));assert(device_may_wakeup(child));
 pm_result=1;assert(pm_runtime_suspended(child));pm_result=0;assert(!pm_runtime_suspended(child));
 struct kobj_uevent_env env={0};char big[2049];memset(big,'x',sizeof(big));big[2048]=0;
 assert(add_uevent_var(&env,"%s",big)==-ENOMEM);assert(!env.count && !env.used);
 for(unsigned i=0;i<32;i++)assert(!add_uevent_var(&env,"%u",i));assert(add_uevent_var(&env,"x")==-ENOMEM);
 if(argc>1) {
  if(!strcmp(argv[1],"query")) { pm_result=2;assert(!pm_runtime_suspended(child)); }
  else { pm_result=-EIO;assert(pm_runtime_get_sync(child)==-EIO); }
  assert(!sf_tb_native_ready());
  assert(!pm_runtime_suspended(child));assert(!device_may_wakeup(child));
  unsigned calls=pm_calls;assert(pm_runtime_resume_and_get(child)==-ENODEV && pm_calls==calls);
  struct device *other=node(2,NULL);assert(device_add(other)==-ENODEV);put_device(other);
 }
 /* Callback removes both objects and drops all owner's references. The actual
  * production iterator keeps child and parent alive through callback return. */
 assert(device_for_each_child(parent,parent,unregister_child)==7);
 assert(destroyed[0] && destroyed[1]);assert(published==unpublished);
 assert(forgotten==(argc>1?4u:3u)); // Includes the failed USB4 component and never-published child.
 bus_unregister(&bus);
 printf("native device adapter: publication rollback, bounded events, parent final lifetime, traversal removal and %s power dispatch PASS\n",argc>1?"faulted":"live");return 0;
}
