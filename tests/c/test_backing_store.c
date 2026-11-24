#include "backing_store.h"
#ifdef NDEBUG
#error "These tests require assertions, including side-effecting checks"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OK(x) assert((x)==DL_OK)
#define IS(x,y) assert((x)==(y))
static unsigned tests;
typedef struct {
    unsigned allocs, frees, live, failures;
    bool fail_alloc, fail_free, reenter;
    dl_store *store;
} host;
static void *host_alloc(void *c, uint64_t bytes, uint64_t align) {
    host *h=c; h->allocs++;
    if (h->reenter) {
        dl_buffer b;
        IS(dl_store_allocate(h->store,1,&b),DL_BAD_STATE);
        IS(dl_store_mark_device_lost(h->store),DL_BAD_STATE);
    }
    if (h->fail_alloc) return NULL;
    assert(bytes <= SIZE_MAX && align <= SIZE_MAX);
    /* Real host allocation is a test backend, NOT wired/device memory. */
    void *p=aligned_alloc((size_t)align,(size_t)bytes);
    assert(p); memset(p,0,(size_t)bytes); h->live++; return p;
}
static bool host_free(void *c, void *p, uint64_t bytes) {
    host *h=c; (void)bytes;
    if (h->reenter) {
        dl_collection result;
        IS(dl_store_collect(h->store,&result),DL_BAD_STATE);
    }
    if (h->fail_free) { h->failures++; return false; }
    assert(p && h->live); free(p); h->frees++; h->live--; return true;
}
static void init(dl_store *s,host *h) {
    *h=(host){.store=s};
    OK(dl_store_init(s,1,16384,16*16384,(dl_backing_ops){host_alloc,host_free},h));
}
static dl_buffer allocate(dl_store *s) {
    dl_buffer b; OK(dl_store_allocate(s,1,&b)); return b;
}
static dl_lease hold(dl_store *s, dl_buffer b, dl_lease_kind kind) {
    dl_lease l; OK(dl_store_hold(s,b,kind,&l)); return l;
}
static void finish(dl_store *s,host *h,const char *name) {
    OK(dl_store_check_invariants(s)); assert(h->live==0 && s->resident_bytes==0);
    printf("ok %u - %s\n",++tests,name);
}
static void ordinary(void) {
    dl_store s; host h; init(&s,&h); dl_buffer b=allocate(&s);
    assert(h.allocs==1 && h.live==1 && s.resident_bytes==16384);
    dl_collection c; OK(dl_store_collect(&s,&c)); assert(c.retained==1 && !h.frees);
    OK(dl_store_close_owner(&s,b)); assert(!h.frees);
    OK(dl_store_collect(&s,&c)); assert(c.reclaimed==1 && c.reclaimed_bytes==16384);
    OK(dl_store_collect(&s,&c)); assert(!c.reclaimed && h.frees==1);
    finish(&s,&h,"explicit collection frees exactly once");
}
static void host_leases(void) {
    dl_store s; host h; init(&s,&h); dl_buffer b=allocate(&s);
    dl_lease a=hold(&s,b,DL_CPU_MAP),k=hold(&s,b,DL_KERNEL_MAP),r=hold(&s,b,DL_SHARED_REF);
    OK(dl_store_close_owner(&s,b)); dl_collection c;
    OK(dl_store_collect(&s,&c)); assert(c.retained==1);
    OK(dl_store_release_host(&s,a)); OK(dl_store_release_host(&s,k));
    OK(dl_store_collect(&s,&c)); assert(c.retained==1);
    OK(dl_store_release_host(&s,r)); assert(!h.frees);
    OK(dl_store_collect(&s,&c)); finish(&s,&h,"all host leases retain real backing");
}
static void device_leases(void) {
    dl_store s; host h; init(&s,&h); dl_buffer b=allocate(&s);
    dl_lease bind=hold(&s,b,DL_GPU_BIND),job=hold(&s,b,DL_IN_FLIGHT),fw=hold(&s,b,DL_FIRMWARE);
    OK(dl_store_close_owner(&s,b)); dl_collection c;
    IS(dl_store_release_host(&s,job),DL_INVALID);
    OK(dl_store_complete_job(&s,job)); assert(!h.frees);
    OK(dl_store_collect(&s,&c)); assert(c.retained==1);
    OK(dl_store_unbind_gpu(&s,bind)); OK(dl_store_collect(&s,&c)); assert(c.retained==1);
    OK(dl_store_ack_firmware_release(&s,fw)); assert(!h.frees);
    OK(dl_store_collect(&s,&c)); finish(&s,&h,"fence callbacks never destroy backing inline");
}
static void timeout(void) {
    dl_store s; host h; init(&s,&h); dl_buffer b=allocate(&s);
    dl_lease job=hold(&s,b,DL_IN_FLIGHT),map=hold(&s,b,DL_CPU_MAP);
    OK(dl_store_close_owner(&s,b)); OK(dl_store_timeout(&s,job)); dl_collection c;
    IS(dl_store_collect(&s,&c),DL_DEVICE_LOST); assert(!h.frees);
    IS(dl_store_complete_job(&s,job),DL_DEVICE_LOST);
    OK(dl_store_backend_ack_quiescence(&s,1));
    OK(dl_store_collect(&s,&c)); assert(c.retained==1);
    IS(dl_store_complete_job(&s,job),DL_STALE);
    OK(dl_store_release_host(&s,map)); OK(dl_store_collect(&s,&c));
    finish(&s,&h,"timeout retains backing and reset preserves CPU mappings");
}
static void allocation_failure(void) {
    dl_store s; host h; init(&s,&h); h.fail_alloc=true;
    dl_buffer out={99,88,77};
    IS(dl_store_allocate(&s,1,&out),DL_STORE_BACKEND_ERROR);
    assert(out.index==99 && !s.resident_bytes && !h.live);
    OK(dl_store_check_invariants(&s)); h.fail_alloc=false;
    dl_buffer b=allocate(&s); assert(b.serial==2);
    OK(dl_store_close_owner(&s,b)); dl_collection c; OK(dl_store_collect(&s,&c));
    finish(&s,&h,"failed allocation rolls back without publishing a token");
}
static void release_failure(void) {
    dl_store s; host h; init(&s,&h); dl_buffer b=allocate(&s);
    OK(dl_store_close_owner(&s,b)); h.fail_free=true; dl_collection c;
    IS(dl_store_collect(&s,&c),DL_STORE_BACKEND_ERROR);
    assert(c.backend_failures==1 && c.retained==1 && h.live==1 && s.resident_bytes==16384);
    OK(dl_store_check_invariants(&s)); h.fail_free=false;
    OK(dl_store_collect(&s,&c)); finish(&s,&h,"failed release keeps ownership for retry");
}
static void budget(void) {
    dl_store s; host h; init(&s,&h); s.byte_limit=16384;
    dl_buffer b=allocate(&s),out;
    IS(dl_store_allocate(&s,1,&out),DL_CAPACITY); assert(h.allocs==1);
    IS(dl_store_allocate(&s,UINT64_MAX,&out),DL_OVERFLOW);
    OK(dl_store_close_owner(&s,b)); dl_collection c; OK(dl_store_collect(&s,&c));
    b=allocate(&s); OK(dl_store_close_owner(&s,b)); OK(dl_store_collect(&s,&c));
    finish(&s,&h,"byte budget counts retained backing and rejects overflow");
}
static void capacity(void) {
    dl_store s; host h; init(&s,&h); dl_buffer b[DL_BUFFER_CAPACITY],out;
    for(unsigned i=0;i<DL_BUFFER_CAPACITY;i++) b[i]=allocate(&s);
    IS(dl_store_allocate(&s,1,&out),DL_CAPACITY); assert(h.allocs==DL_BUFFER_CAPACITY);
    for(unsigned i=0;i<DL_BUFFER_CAPACITY;i++) OK(dl_store_close_owner(&s,b[i]));
    dl_collection c; OK(dl_store_collect(&s,&c)); assert(c.reclaimed==DL_BUFFER_CAPACITY);
    finish(&s,&h,"bounded table never invokes backend after capacity exhaustion");
}
static void stale(void) {
    dl_store s; host h; init(&s,&h); dl_buffer b=allocate(&s);
    dl_lease old=hold(&s,b,DL_CPU_MAP); OK(dl_store_release_host(&s,old));
    OK(dl_store_close_owner(&s,b)); dl_collection c; OK(dl_store_collect(&s,&c));
    dl_buffer newer=allocate(&s); dl_lease current=hold(&s,newer,DL_CPU_MAP);
    assert(b.index==newer.index && b.serial!=newer.serial);
    IS(dl_store_close_owner(&s,b),DL_STALE);
    IS(dl_store_release_host(&s,old),DL_STALE);
    OK(dl_store_close_owner(&s,newer)); OK(dl_store_collect(&s,&c)); assert(c.retained==1);
    OK(dl_store_release_host(&s,current)); OK(dl_store_collect(&s,&c));
    finish(&s,&h,"stale callbacks cannot release reused slots");
}
static void reentrant(void) {
    dl_store s; host h; init(&s,&h); h.reenter=true;
    dl_buffer b=allocate(&s); OK(dl_store_close_owner(&s,b));
    dl_collection c; OK(dl_store_collect(&s,&c));
    finish(&s,&h,"backend callback reentrancy rejected");
}
static void invalid(void) {
    dl_store s; host h; init(&s,&h); dl_buffer out; dl_collection c;
    IS(dl_store_allocate(NULL,1,&out),DL_INVALID);
    IS(dl_store_allocate(&s,0,&out),DL_INVALID);
    IS(dl_store_allocate(&s,1,NULL),DL_INVALID);
    IS(dl_store_collect(&s,NULL),DL_INVALID);
    IS(dl_store_init(&s,1,3,100,(dl_backing_ops){host_alloc,host_free},&h),DL_INVALID);
    IS(dl_store_init(&s,1,4096,100,(dl_backing_ops){host_alloc,host_free},&h),DL_INVALID);
    OK(dl_store_mark_device_lost(&s)); IS(dl_store_allocate(&s,1,&out),DL_DEVICE_LOST);
    IS(dl_store_collect(&s,&c),DL_DEVICE_LOST);
    OK(dl_store_backend_ack_quiescence(&s,1));
    finish(&s,&h,"invalid inputs rejected before backend entry");
}
static void order_independent(void) {
    dl_store s; host h; init(&s,&h); dl_buffer a=allocate(&s),b=allocate(&s);
    dl_lease job=hold(&s,a,DL_IN_FLIGHT); OK(dl_store_close_owner(&s,a));
    OK(dl_store_close_owner(&s,b)); dl_collection c; OK(dl_store_collect(&s,&c));
    assert(c.reclaimed==1 && c.retained==1 && h.live==1);
    OK(dl_store_complete_job(&s,job)); OK(dl_store_collect(&s,&c));
    finish(&s,&h,"one busy buffer does not block independent reclamation");
}
static void repeated_cycles(void) {
    dl_store s; host h; init(&s,&h); dl_collection c;
    for(unsigned i=0;i<10000;i++) {
        dl_buffer b=allocate(&s);
        dl_lease job=hold(&s,b,DL_IN_FLIGHT),map=hold(&s,b,DL_CPU_MAP);
        OK(dl_store_close_owner(&s,b));
        if(i%7==0) {
            OK(dl_store_timeout(&s,job));
            IS(dl_store_collect(&s,&c),DL_DEVICE_LOST);
            OK(dl_store_backend_ack_quiescence(&s,(uint64_t)i+1));
        } else OK(dl_store_complete_job(&s,job));
        OK(dl_store_collect(&s,&c)); assert(c.retained==1);
        OK(dl_store_release_host(&s,map)); OK(dl_store_collect(&s,&c));
        OK(dl_store_check_invariants(&s));
    }
    assert(h.allocs==10000 && h.frees==10000);
    finish(&s,&h,"10000 real-allocation lifecycles including simulated timeouts");
}
int main(void) {
    ordinary(); host_leases(); device_leases(); timeout(); allocation_failure();
    release_failure(); budget(); capacity(); stale(); reentrant(); invalid();
    order_independent(); repeated_cycles();
    printf("1..%u\n",tests); return 0;
}
