#include "mapping_ticket.h"
#ifdef NDEBUG
#error "These tests require assertions, including side-effecting checks"
#endif
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <stdatomic.h>

/* Keep the original C11 atomic_bool representation without importing the
 * host's stdatomic.h into production code. */
_Static_assert(__builtin_types_compatible_p(dl_map_atomic_bool, atomic_bool),
               "notification storage must retain its original C11 type");
_Static_assert(sizeof(dl_map_atomic_bool) == sizeof(atomic_bool), "atomic size");
_Static_assert(_Alignof(dl_map_atomic_bool) == _Alignof(atomic_bool), "atomic alignment");

#define OK(x) assert((x) == DL_OK)
#define IS(x,y) assert((x) == (y))
static unsigned tests;
typedef struct { unsigned allocations, frees; } host;
static void *allocate(void *arg, uint64_t bytes, uint64_t alignment) {
    host *h = arg;
    assert(bytes <= SIZE_MAX && alignment <= SIZE_MAX);
    void *p = aligned_alloc((size_t)alignment, (size_t)bytes);
    assert(p); memset(p, 0, (size_t)bytes); h->allocations++;
    return p;
}
static bool release(void *arg, void *cookie, uint64_t bytes) {
    host *h = arg; (void)bytes;
    assert(cookie); free(cookie); h->frees++; return true;
}
static dl_buffer setup(dl_store *s, host *h, uint64_t page) {
    *h = (host){0};
    OK(dl_store_init(s, 1, page, page * 16, (dl_backing_ops){allocate,release}, h));
    dl_buffer b; OK(dl_store_allocate(s, page * 2, &b)); return b;
}
static dl_map_request request(uint64_t page) {
    return (dl_map_request){0,page,DL_MAP_READ|DL_MAP_WRITE,DL_MAP_READ|DL_MAP_WRITE};
}
static void collect(dl_store *s, unsigned expected) {
    dl_collection c; OK(dl_store_collect(s,&c)); assert(c.reclaimed == expected);
}
static void done(dl_store *s, host *h, const char *name) {
    OK(dl_store_check_invariants(s));
    assert(h->allocations == h->frees && s->resident_bytes == 0);
    printf("ok %u - %s\n", ++tests, name);
}
static dl_map_handoff prepare(dl_store *s, dl_buffer b, dl_map_ticket *ticket) {
    OK(dl_map_prepare(s,b,request(s->page_size),ticket));
    dl_map_handoff h; OK(dl_map_publish(ticket,&h)); return h;
}
static void close_before_unmap(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384);
    dl_map_ticket ticket={0}; dl_map_handoff m=prepare(&s,b,&ticket);
    OK(dl_store_close_owner(&s,b)); collect(&s,0);
    IS(dl_map_drain(&ticket),DL_BUSY);
    m.release(m.release_arg); assert(h.frees==0); collect(&s,0);
    OK(dl_map_drain(&ticket)); assert(h.frees==0); collect(&s,1);
    done(&s,&h,"pager notification retains memory until drain and explicit collection");
}
static void abort_before_publish(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_map_ticket t={0};
    OK(dl_map_prepare(&s,b,request(s.page_size),&t));
    OK(dl_store_close_owner(&s,b)); collect(&s,0);
    OK(dl_map_abort(&t)); assert(!h.frees); collect(&s,1);
    IS(dl_map_abort(&t),DL_BAD_STATE);
    done(&s,&h,"driver-local failure aborts unpublished hold only");
}
static void failure_after_publish(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_map_ticket t={0};
    dl_map_handoff m=prepare(&s,b,&t);
    IS(dl_map_abort(&t),DL_BAD_STATE);
    /* Simulates pager setup/population failing after the driver returned. */
    m.release(m.release_arg); OK(dl_map_drain(&t));
    collect(&s,0); OK(dl_store_close_owner(&s,b)); collect(&s,1);
    done(&s,&h,"post-handoff mapping failure uses the same pager release path");
}
static void duplicate_notification(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_map_ticket t={0};
    dl_map_handoff m=prepare(&s,b,&t); OK(dl_store_close_owner(&s,b));
    for (int i=0;i<10;i++) m.release(m.release_arg);
    OK(dl_map_drain(&t)); IS(dl_map_drain(&t),DL_STALE); collect(&s,1);
    m.release(m.release_arg); IS(dl_map_drain(&t),DL_STALE);
    done(&s,&h,"duplicate notifications are harmless while ticket storage is alive");
}
static void reused_buffer(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_map_ticket old={0};
    dl_map_handoff a=prepare(&s,b,&old); OK(dl_store_close_owner(&s,b));
    a.release(a.release_arg); OK(dl_map_drain(&old)); collect(&s,1);
    dl_buffer next; OK(dl_store_allocate(&s,16384,&next));
    assert(next.index==b.index && next.serial!=b.serial);
    dl_map_ticket now={0}; dl_map_handoff c=prepare(&s,next,&now);
    OK(dl_store_close_owner(&s,next)); a.release(a.release_arg);
    IS(dl_map_drain(&old),DL_STALE); IS(dl_map_drain(&now),DL_BUSY); collect(&s,0);
    IS(dl_map_prepare(&s,next,request(s.page_size),&old),DL_BAD_STATE);
    c.release(c.release_arg); OK(dl_map_drain(&now)); collect(&s,1);
    done(&s,&h,"old callback cannot release a reused buffer slot; ticket reuse rejected");
}
static void multiple_maps(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384);
    dl_map_ticket a={0},c={0}; dl_map_handoff x=prepare(&s,b,&a),y=prepare(&s,b,&c);
    OK(dl_store_close_owner(&s,b)); y.release(y.release_arg); OK(dl_map_drain(&c)); collect(&s,0);
    x.release(x.release_arg); OK(dl_map_drain(&a)); collect(&s,1);
    done(&s,&h,"independent pager instances each retain a mapping hold");
}
static void timeout_preserves_map(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_map_ticket t={0};
    dl_map_handoff m=prepare(&s,b,&t); dl_lease job;
    OK(dl_store_hold(&s,b,DL_IN_FLIGHT,&job)); OK(dl_store_close_owner(&s,b));
    OK(dl_store_timeout(&s,job)); OK(dl_store_backend_ack_quiescence(&s,1));
    collect(&s,0); IS(dl_map_drain(&t),DL_BUSY);
    m.release(m.release_arg); OK(dl_map_drain(&t)); collect(&s,1);
    done(&s,&h,"simulated GPU quiescence does not discard the CPU mapping hold");
}
static void callback_during_loss(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_map_ticket t={0};
    dl_map_handoff m=prepare(&s,b,&t); OK(dl_store_close_owner(&s,b));
    OK(dl_store_mark_device_lost(&s)); m.release(m.release_arg);
    OK(dl_map_drain(&t)); dl_collection c;
    IS(dl_store_collect(&s,&c),DL_DEVICE_LOST); assert(!h.frees);
    OK(dl_store_backend_ack_quiescence(&s,1)); collect(&s,1);
    done(&s,&h,"notification during device loss does not authorize collection");
}
static void range_checks(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384);
    dl_map_request r=request(s.page_size); dl_map_ticket t={0};
    r.offset=1; IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.offset=0; r.bytes=0; IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.bytes=1; IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.bytes=16384; r.offset=32768; IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.offset=UINT64_MAX-16383; IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.offset=16384; r.bytes=32768; IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.bytes=16384; OK(dl_map_prepare(&s,b,r,&t)); OK(dl_map_abort(&t));
    OK(dl_store_close_owner(&s,b)); collect(&s,1);
    done(&s,&h,"aligned bounded ranges reject zero, overflow and partial-page requests");
}
static void permissions(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_map_ticket t={0};
    dl_map_request r=request(s.page_size); r.allowed_access=DL_MAP_READ;
    IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.allowed_access=7; IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.allowed_access=3; r.requested_access=DL_MAP_WRITE;
    IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.requested_access=0; IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.requested_access=5; IS(dl_map_prepare(&s,b,r,&t),DL_INVALID);
    r.requested_access=DL_MAP_READ; OK(dl_map_prepare(&s,b,r,&t)); OK(dl_map_abort(&t));
    OK(dl_store_close_owner(&s,b)); collect(&s,1);
    done(&s,&h,"bootstrap policy rejects executable and unauthorized write mappings");
}
static void capacity(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_lease leases[DL_LEASE_CAPACITY];
    for (unsigned i=0;i<DL_LEASE_CAPACITY;i++) OK(dl_store_hold(&s,b,DL_SHARED_REF,&leases[i]));
    dl_map_ticket t={0}; IS(dl_map_prepare(&s,b,request(s.page_size),&t),DL_CAPACITY);
    assert(t.phase==DL_MAP_FRESH);
    for (unsigned i=0;i<DL_LEASE_CAPACITY;i++) OK(dl_store_release_host(&s,leases[i]));
    OK(dl_store_close_owner(&s,b)); collect(&s,1); done(&s,&h,"failed preparation publishes no callback or partial ticket");
}
static void bad_state(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_map_ticket t={0};
    dl_map_handoff out={(void (*)(void *))0,&s};
    IS(dl_map_publish(&t,&out),DL_BAD_STATE); assert(out.release_arg==&s);
    IS(dl_map_abort(&t),DL_BAD_STATE); IS(dl_map_drain(&t),DL_BAD_STATE);
    IS(dl_map_prepare(NULL,b,request(s.page_size),&t),DL_INVALID);
    OK(dl_map_prepare(&s,b,request(s.page_size),&t));
    IS(dl_map_publish(&t,NULL),DL_INVALID); assert(t.phase==DL_MAP_PREPARED);
    OK(dl_map_publish(&t,&out)); IS(dl_map_publish(&t,&out),DL_BAD_STATE);
    OK(dl_store_close_owner(&s,b)); out.release(out.release_arg); OK(dl_map_drain(&t)); collect(&s,1);
    done(&s,&h,"out-of-order control operations cannot fabricate a release");
}
static void page_numbers(void) {
    for (unsigned shift=12;shift<=16;shift+=2) {
        uint32_t n=77;
        OK(dl_map_page_number((uint64_t)UINT32_MAX<<shift,shift,&n)); assert(n==UINT32_MAX);
        n=77; IS(dl_map_page_number((UINT64_C(1)<<32)<<shift,shift,&n),DL_OVERFLOW); assert(n==77);
        IS(dl_map_page_number(1,shift,&n),DL_INVALID); assert(n==77);
        OK(dl_map_page_number(0,shift,&n)); assert(n==0);
    }
    uint32_t n=55; IS(dl_map_page_number(0,63,&n),DL_INVALID); assert(n==55);
    IS(dl_map_page_number(0,14,NULL),DL_INVALID);
    printf("ok %u - page-number conversion rejects narrowing and misalignment\n",++tests);
}

typedef struct { dl_map_handoff h; atomic_bool *start; } worker;
static void *notify(void *arg) {
    worker *w=arg;
    while (!atomic_load_explicit(w->start,memory_order_acquire)) sched_yield();
    for (unsigned i=0;i<50000;i++) w->h.release(w->h.release_arg);
    return NULL;
}
static void concurrent(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_map_ticket t={0};
    dl_map_handoff m=prepare(&s,b,&t); OK(dl_store_close_owner(&s,b));
    atomic_bool start; atomic_init(&start,false);
    pthread_t threads[8]; worker w={m,&start};
    for (unsigned i=0;i<8;i++) assert(pthread_create(&threads[i],NULL,notify,&w)==0);
    atomic_store_explicit(&start,true,memory_order_release);
    int status;
    do { status=dl_map_drain(&t); if(status==DL_BUSY)sched_yield(); } while(status==DL_BUSY);
    assert(status==DL_OK); collect(&s,1);
    /* Do not destroy/reuse the ticket while callbacks can still touch it. */
    for (unsigned i=0;i<8;i++) assert(pthread_join(threads[i],NULL)==0);
    IS(dl_map_drain(&t),DL_STALE);
    done(&s,&h,"400000 threaded notifications race with one serialized drain without duplicate release");
}

typedef struct { dl_map_handoff h; unsigned words[3]; } publication;
static void *publish_payload(void *arg) {
    publication *p=arg;
    p->words[0]=17; p->words[1]=42; p->words[2]=99;
    p->h.release(p->h.release_arg);
    return NULL;
}
static void callback_publication(void) {
    dl_store s; host h; dl_buffer b=setup(&s,&h,16384); dl_map_ticket t={0};
    publication p={.h=prepare(&s,b,&t)};
    OK(dl_store_close_owner(&s,b));
    pthread_t thread; assert(pthread_create(&thread,NULL,publish_payload,&p)==0);
    int status;
    do { status=dl_map_drain(&t); if(status==DL_BUSY)sched_yield(); } while(status==DL_BUSY);
    assert(status==DL_OK);
    /* Read BEFORE join: the ticket's release/acquire notification must publish
     * the preceding writes. Thread join must not supply the synchronization. */
    assert(p.words[0]==17 && p.words[1]==42 && p.words[2]==99);
    assert(pthread_join(thread,NULL)==0);
    collect(&s,1);
    done(&s,&h,"acquire drain observes data published before the release callback");
}
int main(void) {
    close_before_unmap(); abort_before_publish(); failure_after_publish();
    duplicate_notification(); reused_buffer(); multiple_maps(); timeout_preserves_map();
    callback_during_loss(); range_checks(); permissions(); capacity(); bad_state();
    page_numbers(); concurrent(); callback_publication();
    printf("1..%u\n",tests); return 0;
}
