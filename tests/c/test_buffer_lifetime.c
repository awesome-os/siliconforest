#include "buffer_lifetime.h"
#ifdef NDEBUG
#error "These tests require assertions, including side-effecting checks"
#endif
#include <assert.h>
#include <stdio.h>

static dl_domain d;
static dl_buffer b;
static unsigned tests;
#define OK(expr) assert((expr)==DL_OK)
#define IS(expr,expected) assert((expr)==(expected))
static void setup(void) {OK(dl_init(&d,1)); OK(dl_allocate(&d,100,16384,&b));}
static dl_lease hold(dl_lease_kind kind) {dl_lease l; OK(dl_hold(&d,b,kind,&l)); return l;}
static void done(const char *name) {OK(dl_check_invariants(&d)); printf("ok %u - %s\n",++tests,name);}

static void ordinary_lifetime(void) {
    setup(); dl_view v; OK(dl_inspect(&d,b,&v)); assert(v.bytes==16384);
    IS(dl_reclaim(&d,b),DL_BUSY); OK(dl_close_owner(&d,b)); OK(dl_reclaim(&d,b));
    IS(dl_reclaim(&d,b),DL_STALE); done("owner closure and rounded size");
}
static void host_maps(void) {
    for (unsigned kind=DL_CPU_MAP;kind<=DL_SHARED_REF;kind++) {
        setup(); dl_lease l=hold((dl_lease_kind)kind); OK(dl_close_owner(&d,b));
        IS(dl_reclaim(&d,b),DL_BUSY); OK(dl_release_host(&d,l));
        IS(dl_release_host(&d,l),DL_STALE); OK(dl_reclaim(&d,b));
    }
    done("CPU/kernel/shared references retain backing");
}
static void device_leases(void) {
    setup(); dl_lease binding=hold(DL_GPU_BIND), job=hold(DL_IN_FLIGHT), fw=hold(DL_FIRMWARE);
    OK(dl_close_owner(&d,b)); IS(dl_release_host(&d,job),DL_INVALID);
    IS(dl_complete_job(&d,binding),DL_INVALID); IS(dl_reclaim(&d,b),DL_BUSY);
    OK(dl_complete_job(&d,job)); IS(dl_reclaim(&d,b),DL_BUSY);
    OK(dl_unbind_gpu(&d,binding)); IS(dl_reclaim(&d,b),DL_BUSY);
    OK(dl_ack_firmware_release(&d,fw)); OK(dl_reclaim(&d,b));
    done("device lease kinds cannot be confused");
}
static void client_death(void) {
    setup(); dl_lease job=hold(DL_IN_FLIGHT), another;
    OK(dl_close_owner(&d,b)); IS(dl_close_owner(&d,b),DL_OWNER_GONE);
    IS(dl_hold(&d,b,DL_CPU_MAP,&another),DL_OWNER_GONE);
    IS(dl_reclaim(&d,b),DL_BUSY); OK(dl_complete_job(&d,job)); OK(dl_reclaim(&d,b));
    done("client death never completes outstanding work");
}
static void timeout_and_reset(void) {
    setup(); dl_lease cpu=hold(DL_CPU_MAP), kernel=hold(DL_KERNEL_MAP),
        shared=hold(DL_SHARED_REF), job=hold(DL_IN_FLIGHT), fw=hold(DL_FIRMWARE),
        bind=hold(DL_GPU_BIND);
    OK(dl_close_owner(&d,b)); OK(dl_timeout(&d,job));
    IS(dl_complete_job(&d,job),DL_DEVICE_LOST);
    IS(dl_ack_firmware_release(&d,fw),DL_DEVICE_LOST);
    IS(dl_unbind_gpu(&d,bind),DL_DEVICE_LOST);
    IS(dl_reclaim(&d,b),DL_DEVICE_LOST);
    dl_view v; OK(dl_inspect(&d,b,&v)); assert(v.device==3 && v.host==3);
    IS(dl_backend_ack_quiescence(&d,0),DL_STALE);
    OK(dl_backend_ack_quiescence(&d,1));
    OK(dl_inspect(&d,b,&v)); assert(v.device==0 && v.host==3);
    IS(dl_complete_job(&d,job),DL_STALE); IS(dl_reclaim(&d,b),DL_BUSY);
    OK(dl_release_host(&d,cpu)); OK(dl_release_host(&d,kernel));
    OK(dl_release_host(&d,shared)); OK(dl_reclaim(&d,b));
    done("timeout quarantines; verified quiescence does not drop host holds");
}
static void lost_stops_new_work(void) {
    setup(); OK(dl_mark_device_lost(&d)); dl_buffer x; dl_lease l;
    IS(dl_allocate(&d,1,4096,&x),DL_DEVICE_LOST);
    IS(dl_hold(&d,b,DL_IN_FLIGHT,&l),DL_DEVICE_LOST);
    done("lost domain rejects allocations and new work");
}
static void host_cleanup_while_lost(void) {
    setup(); dl_lease l=hold(DL_CPU_MAP); OK(dl_mark_device_lost(&d));
    OK(dl_close_owner(&d,b)); OK(dl_release_host(&d,l));
    IS(dl_reclaim(&d,b),DL_DEVICE_LOST); OK(dl_backend_ack_quiescence(&d,4));
    OK(dl_reclaim(&d,b)); done("host cleanup does not bypass quarantine");
}
static void reset_order(void) {
    setup(); IS(dl_backend_ack_quiescence(&d,1),DL_BAD_STATE);
    OK(dl_mark_device_lost(&d)); OK(dl_backend_ack_quiescence(&d,4));
    OK(dl_mark_device_lost(&d)); IS(dl_backend_ack_quiescence(&d,4),DL_STALE);
    IS(dl_backend_ack_quiescence(&d,3),DL_STALE); OK(dl_backend_ack_quiescence(&d,5));
    done("reset acknowledgements must be ordered");
}
static void reuse_rejects_stale_tokens(void) {
    setup(); dl_buffer old=b; dl_lease l=hold(DL_CPU_MAP);
    OK(dl_release_host(&d,l)); OK(dl_close_owner(&d,b)); OK(dl_reclaim(&d,b));
    OK(dl_allocate(&d,10,4096,&b)); assert(b.index==old.index && b.serial!=old.serial);
    dl_lease new_l=hold(DL_CPU_MAP); assert(new_l.index==l.index && new_l.serial!=l.serial);
    IS(dl_release_host(&d,l),DL_STALE); IS(dl_close_owner(&d,old),DL_STALE);
    dl_view v; OK(dl_inspect(&d,b,&v)); assert(v.host==1);
    done("slot reuse rejects stale handles and leases");
}
static void sessions(void) {
    setup(); dl_buffer old=b; dl_lease l=hold(DL_CPU_MAP);
    dl_domain second; OK(dl_init(&second,2)); dl_buffer other;
    OK(dl_allocate(&second,10,4096,&other));
    IS(dl_close_owner(&second,old),DL_STALE); IS(dl_release_host(&second,l),DL_STALE);
    OK(dl_check_invariants(&second)); done("foreign-session tokens rejected");
}
static void allocation_validation(void) {
    setup(); dl_buffer out;
    IS(dl_allocate(&d,0,4096,&out),DL_INVALID);
    IS(dl_allocate(&d,1,0,&out),DL_INVALID); IS(dl_allocate(&d,1,3,&out),DL_INVALID);
    IS(dl_allocate(&d,UINT64_MAX,16384,&out),DL_OVERFLOW);
    IS(dl_allocate(&d,1,4096,NULL),DL_INVALID);
    OK(dl_allocate(&d,UINT64_MAX,1,&out)); done("invalid sizes and rounding overflow rejected");
}
static void serial_overflow(void) {
    setup(); d.next_buffer=UINT64_MAX; dl_buffer out;
    IS(dl_allocate(&d,1,4096,&out),DL_OVERFLOW);
    d.next_lease=UINT64_MAX; dl_lease l;
    IS(dl_hold(&d,b,DL_CPU_MAP,&l),DL_OVERFLOW); done("serial numbers never wrap");
}
static void capacity(void) {
    setup(); dl_buffer x; for (unsigned i=1;i<DL_BUFFER_CAPACITY;i++) OK(dl_allocate(&d,1,4096,&x));
    IS(dl_allocate(&d,1,4096,&x),DL_CAPACITY);
    dl_lease l; for (unsigned i=0;i<DL_LEASE_CAPACITY;i++) OK(dl_hold(&d,b,DL_CPU_MAP,&l));
    IS(dl_hold(&d,b,DL_CPU_MAP,&l),DL_CAPACITY); done("bounded tables fail without eviction");
}
static void invalid_inputs(void) {
    setup(); dl_buffer fake={DL_BUFFER_CAPACITY,1,1}; dl_lease l={DL_LEASE_CAPACITY,1,1};
    IS(dl_init(NULL,1),DL_INVALID); dl_domain blank={0};
    IS(dl_init(&blank,0),DL_INVALID); IS(dl_check_invariants(&blank),DL_INVALID);
    IS(dl_close_owner(&d,fake),DL_STALE); IS(dl_release_host(&d,l),DL_STALE);
    IS(dl_hold(&d,b,DL_KIND_COUNT,&l),DL_INVALID);
    IS(dl_hold(&d,b,DL_CPU_MAP,NULL),DL_INVALID);
    dl_lease cpu=hold(DL_CPU_MAP); IS(dl_timeout(&d,cpu),DL_INVALID);
    done("malformed tokens and kinds rejected");
}
static void out_of_order_completions(void) {
    setup(); dl_lease a=hold(DL_IN_FLIGHT), c=hold(DL_IN_FLIGHT);
    OK(dl_close_owner(&d,b)); OK(dl_complete_job(&d,c)); IS(dl_reclaim(&d,b),DL_BUSY);
    IS(dl_complete_job(&d,c),DL_STALE); OK(dl_complete_job(&d,a)); OK(dl_reclaim(&d,b));
    done("out-of-order completions retain other work");
}
static uint32_t rng=0xC0FFEEu;
static uint32_t random32(void) {rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng;}
static void randomized(void) {
    setup(); dl_buffer handles[64]={{0}}; dl_lease leases[128]={{0}}; uint64_t reset=1;
    for (unsigned i=0;i<100000;i++) {
        unsigned bi=random32()%64, li=random32()%128;
        switch(random32()%11) {
        case 0: (void)dl_allocate(&d,(random32()%65536)+1,16384,&handles[bi]);break;
        case 1: (void)dl_hold(&d,handles[bi],(dl_lease_kind)(random32()%DL_KIND_COUNT),&leases[li]);break;
        case 2: (void)dl_close_owner(&d,handles[bi]);break;
        case 3: (void)dl_release_host(&d,leases[li]);break;
        case 4: (void)dl_complete_job(&d,leases[li]);break;
        case 5: (void)dl_ack_firmware_release(&d,leases[li]);break;
        case 6: (void)dl_unbind_gpu(&d,leases[li]);break;
        case 7: (void)dl_timeout(&d,leases[li]);break;
        case 8: (void)dl_mark_device_lost(&d);break;
        case 9: (void)dl_backend_ack_quiescence(&d,reset++);break;
        case 10:(void)dl_reclaim(&d,handles[bi]);break;
        }
        OK(dl_check_invariants(&d));
        /* Also check the no-reclamation invariant against every live slot. */
        for (uint32_t j=0;j<DL_BUFFER_CAPACITY;j++) if(d.buffers[j].occupied) {
            dl_buffer h={j,d.buffers[j].serial,d.session};dl_view v;OK(dl_inspect(&d,h,&v));
            if(d.device_lost || v.owner_alive || v.host || v.device)
                assert(dl_reclaim(&d,h)!=DL_OK);
        }
    }
    done("100000 deterministic adversarial transitions preserve invariants");
}
int main(void) {
    ordinary_lifetime(); host_maps(); device_leases(); client_death(); timeout_and_reset();
    lost_stops_new_work(); host_cleanup_while_lost(); reset_order(); reuse_rejects_stale_tokens();
    sessions(); allocation_validation(); serial_overflow(); capacity(); invalid_inputs();
    out_of_order_completions(); randomized(); printf("1..%u\n",tests); return 0;
}
