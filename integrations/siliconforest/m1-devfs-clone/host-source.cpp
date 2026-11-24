/* SPDX-License-Identifier: MIT */
/* Actual extracted kernel/DRM functions. This does not emulate XNU scheduling or GPU DMA. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>
#define __unused __attribute__((unused))
#define dev_t sf_dev_t
using sf_dev_t=int;
using proc_t=void *;
struct proc {};
#define SF_DRI_SLOTS 1024
#define SF_DRI_SLOT(d) ((minor(d)>>8)&0xffff)
#define SF_DRI_DRM_MINOR(d) (minor(d)&0xff)
static int major(dev_t d){return (unsigned)d>>24;}
static int minor(dev_t d){return d&0xffffff;}
static dev_t makedev(int a,int b){return (a<<24)|b;}
#define DEVFS_CLONE_ALLOC 1
#define DEVFS_CLONE_FREE 0
#define DEVFS_CLONE_FREE_ON_RECLAIM 1
#define DN_DELETE 2
#define DN_CREATE 4
#define DN_CREATEWAIT 8
#define DN_CLONE_FREE_ON_RECLAIM 16
#define DEV_MAX_VNODE_RETRY 5
#define PRIBIO 1
#define PCATCH 0x100
#define VNFS_NOCACHE 1
#define VNFS_CANTCACHE 2
#define VNCREATE_FLAVOR 0
#define VCREATESIZE sizeof(vnode_fsparam)
#define VNODE_CREATE_DEFAULT 0
#define VT_DEVFS 3
#define DEV_SLNK 1
#define DEV_DIR 2
#define DEV_BDEV 3
#define DEV_CDEV 4
#define NULLVP nullptr
static unsigned held,slot_held,sel_held,wakes,refs,closes;
static int devfs_mutex,slot_lock,sel_lock;
static void *sLock=&slot_lock,*sSelLock=&sel_lock;
struct selinfo {};
static selinfo sSel[SF_DRI_SLOTS];
static bool sSlotUsed[SF_DRI_SLOTS];
static __unused unsigned sCloneUsed,sClonePeak;
static __unused unsigned long long sCloneAllocations,sCloneFrees,sCloneFailures;
static void *sSlots[SF_DRI_SLOTS];
static bool fail_open,fail_create,interrupt_wait,simulate_waiter;
static void lck_mtx_lock(void *p){unsigned &h=p==sLock?slot_held:sel_held;assert(h==0);h++;}
static void lck_mtx_unlock(void *p){unsigned &h=p==sLock?slot_held:sel_held;assert(h==1);h--;}
static void selwakeup(selinfo *){assert(sel_held==1);}
static void selthreadclear(selinfo *){assert(sel_held==1);}
struct sf_dri_ops { int (*open)(int,int,void **); void (*close)(void *); };
static struct {const sf_dri_ops *ops;int minor;} sOwner[256];
static const sf_dri_ops *owner_ops(dev_t d){return sOwner[SF_DRI_DRM_MINOR(d)].ops;}
static void *slot_cookie(dev_t d){int s=SF_DRI_SLOT(d);return s>0&&s<SF_DRI_SLOTS?sSlots[s]:nullptr;}
static int open_file(int m,int flags,void **cookie){(void)flags;assert(m==128);if(fail_open)return EIO;*cookie=(void *)0x1234;return 0;}
static void close_file(void *c){assert(c==(void *)0x1234);closes++;}
static const sf_dri_ops ops={open_file,close_file};
enum vtype { VNON, VLNK, VDIR, VBLK, VCHR };
struct devnode;
struct vnode {devnode *node;dev_t device;bool recycle;};
using vnode_t=vnode *;
struct devfsmount {void *mount;};
struct devnode {
 vnode *dn_vn;int dn_type,dn_lflags;devfsmount *dn_dvm;void **dn_ops;
 struct {dev_t dev;struct {devnode *parent;} Dir;} dn_typeinfo;
 int (*dn_clone)(dev_t,int);
};
using devnode_t=devnode;
struct vnode_fsparam {
 void *vnfs_mp;vtype vnfs_vtype;const char *vnfs_str;vnode *vnfs_dvp;
 void *vnfs_fsnode,*vnfs_cnp,*vnfs_vops;dev_t vnfs_rdev;size_t vnfs_filesize;
 unsigned vnfs_flags;int vnfs_marksystem,vnfs_markroot;
};
struct vnop_inactive_args {vnode *a_vp;};
struct vnop_reclaim_args {vnode *a_vp;};
#define VTODN(vp) ((vp)->node)
#define DEVFS_LOCK() do {assert(held==0);held++;} while(0)
#define DEVFS_UNLOCK() do {assert(held==1);held--;} while(0)
static void panic(const char *,...){assert(false);}
static void devfs_ref_node(devnode *){assert(held==1);refs++;}
static void devfs_rele_node(devnode *){assert(held==1 && refs);refs--;}
static uint32_t vnode_vid(vnode *){return 1;}
static void vnode_hold(vnode *){}
static void vnode_drop(vnode *){}
static int vnode_getwithvid_drainok(vnode *,uint32_t){return 0;}
static void vnode_put(vnode *){}
static void vnode_setneedinactive(vnode *){}
static void vnode_settag(vnode *,int){}
static void vnode_clearfsnode(vnode *v){v->node=nullptr;}
static __unused dev_t vnode_specrdev(vnode *v){return v->device;}
static void vnode_recycle(vnode *v){v->recycle=true;}
static int vnode_create_ext(int,size_t,vnode_fsparam *p,vnode **out,int){
 assert(!held);
 if(simulate_waiter)((devnode *)p->vnfs_fsnode)->dn_lflags|=DN_CREATEWAIT;
 if(fail_create)return ENOMEM;
 *out=new vnode{(devnode *)p->vnfs_fsnode,p->vnfs_rdev,false};return 0;
}
static int msleep(void *,void *lock,int flags,const char *,int){
 assert(held==1 && lock==&devfs_mutex && interrupt_wait);
 assert(flags&PCATCH);return EINTR;
}
static void wakeup(void *){assert(held==1);wakes++;}
#include SF_DEVFS_SOURCE

static devnode node;static devfsmount mount;static void *vops;
static unsigned used(){unsigned n=0;for(bool s:sSlotUsed)n+=s;return n;}
static void reset(bool opt=true){
 assert(!held && !slot_held && !sel_held);memset(sSlotUsed,0,sizeof(sSlotUsed));memset(sSlots,0,sizeof(sSlots));
 node={};node.dn_type=DEV_CDEV;node.dn_dvm=&mount;node.dn_ops=&vops;
 node.dn_typeinfo.dev=makedev(10,128);node.dn_clone=dri_clone;node.dn_lflags=opt?DN_CLONE_FREE_ON_RECLAIM:0;
 sOwner[128].ops=&ops;sOwner[128].minor=128;
 fail_open=fail_create=interrupt_wait=simulate_waiter=false;refs=wakes=closes=0;
 sCloneUsed=sClonePeak=0;sCloneAllocations=sCloneFrees=sCloneFailures=0;
}
static int lookup(vnode **v){DEVFS_LOCK();int e=devfs_dntovn(&node,v,nullptr);DEVFS_UNLOCK();return e;}
static void retire(vnode *v){
 vnop_inactive_args a={v};assert(!devfs_inactive(&a) && v->recycle);
 vnop_reclaim_args r={v};assert(!devfs_reclaim(&r));delete v;
}
static unsigned minor_one_frees;
static int clone_minor_one(dev_t d,int action){if(action==DEVFS_CLONE_ALLOC)return 1;assert(minor(d)==1);minor_one_frees++;return 0;}
int main(int argc,char **argv){
 if(argc==2 && !strcmp(argv[1],"--counterexample")){
  reset();unsigned n;
  for(n=0;n<5000;n++){vnode *v=nullptr;int e=lookup(&v);
   if(e){printf("DEVFS_SOURCE original counterexample: stat-only lookup %u returns %d; used=%u DN_CREATE=%d (no live creator)\n",n+1,e,used(),!!(node.dn_lflags&DN_CREATE));assert(e==ENOMEM && used()==1023 && (node.dn_lflags&DN_CREATE));return 1;}
   retire(v);
  }
  return 0;
 }
 reset();for(unsigned n=0;n<5000;n++){vnode *v=nullptr;assert(!lookup(&v));retire(v);assert(!used() && !refs && sCloneUsed==0 && sCloneAllocations==sCloneFrees);}
 puts("DEVFS_SOURCE PASS 5000 actual stat-only allocate/inactive/reclaim cycles");
 reset();for(unsigned n=0;n<5000;n++){vnode *v=nullptr;assert(!lookup(&v));assert(!dri_open(v->device,0,0,nullptr));assert(!dri_close(v->device,0,0,nullptr));assert(used()==1);retire(v);assert(!used() && !refs && sCloneUsed==0 && sCloneAllocations==sCloneFrees);}
 puts("DEVFS_SOURCE PASS 5000 actual open/close/reclaim cycles, reservation survives close");
 reset();fail_open=true;for(unsigned n=0;n<5000;n++){vnode *v=nullptr;assert(!lookup(&v));assert(dri_open(v->device,0,0,nullptr)==EIO && used()==1);retire(v);assert(!used() && !refs && sCloneUsed==0 && sCloneAllocations==sCloneFrees);}
 puts("DEVFS_SOURCE PASS 5000 failed-open/reclaim cycles");
 reset();std::vector<vnode *> pool;
 for(unsigned n=0;n<1023;n++){vnode *v=nullptr;assert(!lookup(&v));pool.push_back(v);}
 vnode *v=nullptr;assert(lookup(&v)==ENOMEM && !(node.dn_lflags&DN_CREATE));
 for(auto p:pool)retire(p);assert(!used() && !refs);assert(!lookup(&v));retire(v);
 puts("DEVFS_SOURCE PASS full pool failure clears creator flag, reclaim permits next lookup");
 reset();fail_create=true;simulate_waiter=true;assert(lookup(&v)==ENOMEM && !v && !used() && !refs);
 assert(!(node.dn_lflags&(DN_CREATE|DN_CREATEWAIT)) && wakes==1);
 puts("DEVFS_SOURCE PASS failed vnode creation frees actual allocated minor and wakes waiters");
 reset();node.dn_clone=clone_minor_one;fail_create=true;assert(lookup(&v)==ENOMEM && minor_one_frees==1 && !refs);
 puts("DEVFS_SOURCE PASS allocated minor equal to ALLOC action is still freed on creation failure");
 reset();interrupt_wait=true;node.dn_lflags|=DN_CREATE;assert(lookup(&v)==EINTR && !v && !refs && !used());assert(node.dn_lflags&DN_CREATE);
 puts("DEVFS_SOURCE PASS PCATCH abort returns EINTR without clearing another creator's ownership");
 reset();vnode *a=nullptr,*b=nullptr;assert(!lookup(&a));assert(!dri_open(a->device,0,0,nullptr));assert(!dri_close(a->device,0,0,nullptr));assert(!lookup(&b));assert(a->device!=b->device);assert(!dri_open(b->device,0,0,nullptr));retire(a);assert(slot_cookie(b->device)==(void *)0x1234 && used()==1);assert(!dri_close(b->device,0,0,nullptr));retire(b);assert(!used());
 puts("DEVFS_SOURCE PASS delayed old-vnode reclaim cannot release a newer live file's slot");
 reset(false);assert(!lookup(&v));retire(v);assert(used()==1);
 puts("DEVFS_SOURCE PASS legacy clone consumer has no new reclaim callback");
 reset();assert(!lookup(&v));assert(!dri_open(v->device,0,0,nullptr));assert(dri_clone(v->device,DEVFS_CLONE_FREE)==EBUSY && slot_cookie(v->device)==(void *)0x1234);assert(!dri_close(v->device,0,0,nullptr));retire(v);
 puts("DEVFS_SOURCE PASS reclaim refusal retains a live DRM cookie; no GPU backing released");
 puts("DEVFS_SOURCE all actual-source cases PASS (host only)");
}
