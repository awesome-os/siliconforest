// SPDX-License-Identifier: MIT
/* Host-only BSD devres layout/allocation fixture; actual group code below. */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
typedef int gfp_t;
struct list_head { struct list_head *next,*prev; };
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define LIST_HEAD(name) struct list_head name={&name,&name}
#define INIT_LIST_HEAD(h) ((h)->next=(h),(h)->prev=(h))
static void list_link(struct list_head *n,struct list_head *prev,struct list_head *next) { n->prev=prev;n->next=next;prev->next=n;next->prev=n; }
static void list_add(struct list_head *n,struct list_head *h) { list_link(n,h,h->next); }
static void list_add_tail(struct list_head *n,struct list_head *h) { list_link(n,h->prev,h); }
static void list_del_init(struct list_head *n) { n->prev->next=n->next;n->next->prev=n->prev;INIT_LIST_HEAD(n); }
static bool list_empty(struct list_head *h) { return h->next==h; }
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
#define list_for_each_entry(pos,h,m) for(struct list_head *node=(h)->next;node!=(h) && (((pos)=container_of(node,__typeof__(*(pos)),m)),1);node=node->next)
#define spin_lock(p) assert(!pthread_mutex_lock(p))
#define spin_unlock(p) assert(!pthread_mutex_unlock(p))
struct device { pthread_mutex_t devres_lock;struct list_head devres_head; };
struct devres { struct list_head entry;void (*release)(struct device *,void *);unsigned char __drdata[0] __attribute__((aligned(64))); };
static unsigned alloc_calls,fail_at,live_allocations,faults;
static int last_fault;
static void sf_tb_native_fault(int error) { ++faults;last_fault=error; }
static void *allocate(size_t n) {
 ++alloc_calls;if(fail_at && alloc_calls==fail_at)return NULL;
 void *p=NULL;assert(!posix_memalign(&p,64,(n+63)&~(size_t)63));memset(p,0,n);++live_allocations;return p;
}
static void *kzalloc(size_t n,gfp_t f) { (void)f;return allocate(n); }
static void kfree(void *p) { if(p){assert(live_allocations);--live_allocations;free(p);} }
static void *lkpi_devres_alloc(void (*release)(struct device *,void *),size_t n,gfp_t flags) {
 (void)flags;struct devres *dr=allocate(sizeof(*dr)+n);if(!dr)return NULL;
 INIT_LIST_HEAD(&dr->entry);dr->release=release;return dr+1;
}
static void lkpi_devres_free_dr(struct devres *dr) { assert(list_empty(&dr->entry));kfree(dr); }
static void lkpi_devres_free(void *p) { if(p)lkpi_devres_free_dr(container_of(p,struct devres,__drdata)); }
static void lkpi_devres_add(struct device *dev,void *p) {
 struct devres *dr=container_of(p,struct devres,__drdata);spin_lock(&dev->devres_lock);list_add(&dr->entry,&dev->devres_head);spin_unlock(&dev->devres_lock);
}
#include "sf_tb_devres_groups.inc"
struct resource { int value;bool reenter; };
static int released[8192];static unsigned nreleased;
static void add_resource(struct device *,int,bool);
static void resource_release(struct device *dev,void *data) {
 struct resource *r=data;assert(nreleased<8192);released[nreleased++]=r->value;
 assert(!pthread_mutex_trylock(&dev->devres_lock));assert(!pthread_mutex_unlock(&dev->devres_lock));
 if(r->reenter)add_resource(dev,100,false);
}
static void add_resource(struct device *dev,int n,bool reenter) {
 struct resource *r=lkpi_devres_alloc(resource_release,sizeof(*r),0);assert(r);r->value=n;r->reenter=reenter;lkpi_devres_add(dev,r);
}
static void init(struct device *dev) { assert(!live_allocations);nreleased=0;INIT_LIST_HEAD(&dev->devres_head);assert(!pthread_mutex_init(&dev->devres_lock,NULL)); }
static void finish(struct device *dev) {
 while(!list_empty(&dev->devres_head)) { struct devres *dr=list_first_entry(&dev->devres_head,struct devres,entry);list_del_init(&dr->entry);if(dr->release)dr->release(dev,dr+1);lkpi_devres_free_dr(dr); }
 assert(!live_allocations);assert(!pthread_mutex_destroy(&dev->devres_lock));
}
static unsigned list_count(struct device *dev) { unsigned n=0;for(struct list_head *p=dev->devres_head.next;p!=&dev->devres_head;p=p->next)++n;return n; }
int main(void) {
 struct device dev;
 init(&dev);add_resource(&dev,0,false);void *a=devres_open_group(&dev,NULL,0);assert(a);add_resource(&dev,1,false);
 void *b=devres_open_group(&dev,NULL,0);assert(b);add_resource(&dev,2,false);devres_close_group(&dev,NULL);add_resource(&dev,3,false);devres_close_group(&dev,a);add_resource(&dev,4,false);
 assert(devres_release_group(&dev,a)==3);assert(nreleased==3 && released[0]==3 && released[1]==2 && released[2]==1);
 assert(list_count(&dev)==2);finish(&dev);assert(nreleased==5 && released[3]==4 && released[4]==0);
 init(&dev);a=devres_open_group(&dev,&dev,0);add_resource(&dev,5,false);devres_remove_group(&dev,a);assert(!nreleased && list_count(&dev)==1);finish(&dev);
 init(&dev);a=devres_open_group(&dev,NULL,0);add_resource(&dev,6,false);b=devres_open_group(&dev,NULL,0);add_resource(&dev,7,false);
 unsigned before=list_count(&dev),old_faults=faults;devres_close_group(&dev,a);assert(faults==old_faults+1 && last_fault==-EINVAL && list_count(&dev)==before && !nreleased);
 assert(devres_release_group(&dev,NULL)==1 && released[0]==7);devres_close_group(&dev,a);assert(devres_release_group(&dev,a)==1 && released[1]==6);finish(&dev);
 init(&dev);a=devres_open_group(&dev,NULL,0);add_resource(&dev,8,true);devres_close_group(&dev,a);assert(devres_release_group(&dev,a)==1 && list_count(&dev)==1 && released[0]==8);finish(&dev);assert(released[1]==100);
 for(unsigned fail=1;fail<=3;fail++) { init(&dev);add_resource(&dev,9,false);fail_at=alloc_calls+fail;assert(!devres_open_group(&dev,NULL,0));fail_at=0;assert(list_count(&dev)==1 && !nreleased);finish(&dev); }
 for(unsigned cycle=0;cycle<500;cycle++) {
  init(&dev);void *groups[4];unsigned count=0;
  for(unsigned depth=0;depth<4;depth++) { groups[depth]=devres_open_group(&dev,NULL,0);assert(groups[depth]);for(unsigned i=0;i<1+(cycle+depth)%8;i++)add_resource(&dev,(int)count++,false); }
  for(unsigned depth=4;depth;depth--)devres_close_group(&dev,groups[depth-1]);
  assert(devres_release_group(&dev,groups[0])==(int)count && nreleased==count && list_empty(&dev.devres_head));
  for(unsigned i=0;i<count;i++)assert(released[i]==(int)(count-i-1));finish(&dev);
 }
 init(&dev);a=devres_open_group(&dev,NULL,0);add_resource(&dev,10,false);b=devres_open_group(&dev,NULL,0);add_resource(&dev,11,false);finish(&dev);assert(nreleased==2 && released[0]==11 && released[1]==10);
 init(&dev);old_faults=faults;assert(devres_release_group(&dev,&dev)==-ENOENT && faults==old_faults+1);finish(&dev);
 puts("transactional devres: 500 nested rollback/reverse-order cycles, closed/open groups, removed markers, allocation failures, crossing-close refusal and reentrant release callbacks PASS");
}
