/* Synthetic hostile observations only. These tests do not run an XNU device. */
#include "memory_probe_result.h"
#ifdef NDEBUG
#error "memory probe result tests require active assertions"
#endif
#include <assert.h>
#include <stdio.h>

static unsigned tests;
typedef struct {
    uint64_t capabilities,bytes,total_drained,total_reclaimed;
    unsigned checkpoints;
    struct dlmp_stats baseline,stats;
    struct dlmp_collect collect;
} scenario;
static scenario blocked(void) {
    scenario x={0};
    x.capabilities=DLMP_CAP_RAM_ONLY|DLMP_CAP_KERNEL_BACKEND;
    x.bytes=32768;x.total_drained=2;x.checkpoints=DLMP_CHECK_COMPLETE;
    x.baseline=(struct dlmp_stats){.version=DLMP_VERSION,.open_sessions=2,
        .published_maps=9,.drained_maps=9,.reclaimed_buffers=5};
    x.stats=x.baseline;x.stats.open_sessions=1;x.stats.resident_bytes=x.bytes;
    x.stats.live_buffers=1;x.stats.published_maps+=2;x.stats.drained_maps+=2;
    x.stats.quarantined_buffers=1;
    x.collect=(struct dlmp_collect){.version=DLMP_VERSION,.retained=1,
        .backend_failures=1,.drained=2,.quarantined=1};
    return x;
}
static scenario live(void) {
    scenario x=blocked();x.total_drained=0;
    x.stats.drained_maps=x.baseline.drained_maps;x.stats.pending_maps=2;
    x.collect.drained=0;x.collect.backend_failures=0;x.collect.quarantined=0;
    return x;
}
static scenario passed(void) {
    scenario x=blocked();x.capabilities|=DLMP_CAP_MAPPED_RECLAIM;
    x.total_reclaimed=1;x.stats.reclaimed_buffers++;
    x.stats.resident_bytes=0;x.stats.live_buffers=0;x.stats.quarantined_buffers=0;
    x.collect.reclaimed=1;x.collect.retained=0;x.collect.backend_failures=0;x.collect.quarantined=0;
    return x;
}
static enum dlmp_result classify(const scenario *x) {
    return dlmp_result_mapped(x->capabilities,x->bytes,&x->baseline,&x->stats,&x->collect,
                              x->total_drained,x->total_reclaimed,x->checkpoints);
}
static bool is_live(const scenario *x) {
    return dlmp_result_live(x->capabilities,x->bytes,&x->baseline,&x->stats,&x->collect);
}
static void done(const char *label) { printf("ok %u - %s\n",++tests,label); }
#define BAD(member,value) do { scenario bad=x;bad.member=(value);assert(classify(&bad)==DLMP_RESULT_FAILED); } while(0)
#define BAD_LIVE(member,value) do { scenario bad=x;bad.member=(value);assert(!is_live(&bad)); } while(0)

static void baseline_copyout(void) {
    scenario x=blocked();assert(dlmp_result_baseline(&x.baseline));
    struct dlmp_stats untouched={.version=DLMP_VERSION};assert(!dlmp_result_baseline(&untouched));
    for(uint64_t sessions=0;sessions<4;sessions++) {
        struct dlmp_stats b=x.baseline;b.open_sessions=sessions;
        assert(dlmp_result_baseline(&b)==(sessions==2));
    }
    BAD(baseline.resident_bytes,1);BAD(baseline.live_buffers,1);
    BAD(baseline.pending_maps,1);BAD(baseline.quarantined_buffers,1);
    BAD(baseline.drained_maps,8);BAD(baseline.drained_maps,10);
    BAD(baseline.own_object,1);BAD(baseline.own_bytes,1);
    BAD(baseline.version,0);BAD(baseline.flags,1);
    done("baseline requires copyout, two sessions and no unfinished historical work");
}
static void exact_blocked(void) {
    scenario x=blocked();assert(classify(&x)==DLMP_RESULT_BLOCKED);
    /* Last poll can report one drain after the preceding poll drained the other. */
    x.collect.drained=1;assert(classify(&x)==DLMP_RESULT_BLOCKED);
    x.collect.drained=0;assert(classify(&x)==DLMP_RESULT_BLOCKED);
    done("BLOCKED requires two cumulative drains and exact quarantine accounting");
}
static void early_notification(void) {
    scenario x=live();assert(is_live(&x));
    for(unsigned checkpoint=DLMP_CHECK_PARENT_LIVE;checkpoint<=DLMP_CHECK_CHILD_LIVE;checkpoint++) {
        scenario early=x;early.checkpoints=checkpoint;
        early.stats.pending_maps=1;early.stats.drained_maps++;
        early.collect.drained=1;early.total_drained=1;
        assert(!is_live(&early));
        early.stats.pending_maps=0;early.stats.drained_maps++;
        early.collect.drained=2;early.collect.quarantined=1;early.collect.backend_failures=1;
        /* Backing is still retained: quarantine must not mask an early callback. */
        assert(early.stats.resident_bytes==early.bytes&&early.stats.live_buffers==1);
        assert(!is_live(&early));
    }
    BAD_LIVE(stats.pending_maps,1);BAD_LIVE(collect.drained,1);
    BAD_LIVE(stats.drained_maps,x.baseline.drained_maps+1);
    done("parent-live and child-live observations reject early notification despite retained pages");
}
static void live_accounting(void) {
    scenario x=live();assert(is_live(&x));
    BAD_LIVE(stats.open_sessions,0);BAD_LIVE(stats.open_sessions,2);
    BAD_LIVE(stats.resident_bytes,x.bytes-1);BAD_LIVE(stats.live_buffers,0);
    BAD_LIVE(stats.live_buffers,2);BAD_LIVE(stats.published_maps,x.baseline.published_maps+1);
    BAD_LIVE(stats.quarantined_buffers,0);BAD_LIVE(stats.quarantined_buffers,2);
    BAD_LIVE(stats.reclaimed_buffers,x.baseline.reclaimed_buffers+1);
    BAD_LIVE(collect.reclaimed,1);BAD_LIVE(collect.retained,0);
    BAD_LIVE(collect.backend_failures,1);BAD_LIVE(collect.quarantined,1);
    x.capabilities|=DLMP_CAP_MAPPED_RECLAIM;x.stats.quarantined_buffers=0;
    assert(is_live(&x));BAD_LIVE(stats.quarantined_buffers,1);
    done("live checkpoints require exact sessions, maps, bytes and zero collection side effects");
}
static void no_capability_no_pass(void) {
    scenario x=passed();x.capabilities&=~DLMP_CAP_MAPPED_RECLAIM;
    assert(classify(&x)==DLMP_RESULT_FAILED);
    x.stats.quarantined_buffers=1;assert(classify(&x)==DLMP_RESULT_FAILED);
    x=blocked();x.collect.reclaimed=1;x.total_reclaimed=1;x.stats.reclaimed_buffers++;
    assert(classify(&x)==DLMP_RESULT_FAILED);
    done("reclamation without MAPPED_RECLAIM fails even when reported as successful cleanup");
}
static void identity(void) {
    scenario x=blocked();
    BAD(capabilities,0);BAD(capabilities,DLMP_CAP_RAM_ONLY);
    BAD(capabilities,DLMP_CAP_KERNEL_BACKEND);BAD(capabilities,x.capabilities|UINT64_C(8));
    BAD(stats.version,0);BAD(stats.flags,1);BAD(stats.own_object,123);BAD(stats.own_bytes,4096);
    BAD(collect.version,0);BAD(collect.flags,1);BAD(bytes,0);
    done("unknown capabilities and malformed observation headers fail closed");
}
static void publication_and_totals(void) {
    scenario x=blocked();
    BAD(stats.published_maps,x.baseline.published_maps+1);
    BAD(stats.published_maps,x.baseline.published_maps+3);
    BAD(stats.published_maps,x.baseline.published_maps-1);
    BAD(stats.pending_maps,1);BAD(stats.pending_maps,UINT64_MAX);
    BAD(stats.drained_maps,x.baseline.drained_maps+1);
    BAD(stats.drained_maps,x.baseline.drained_maps+3);
    BAD(total_drained,1);BAD(total_drained,3);BAD(total_reclaimed,1);
    BAD(collect.drained,3);BAD(collect.reclaimed,1);
    done("exact publication deltas and collection totals reject missing, extra and duplicate drains");
}
static void quarantine_receipt(void) {
    scenario x=blocked();
    BAD(stats.resident_bytes,0);BAD(stats.resident_bytes,x.bytes+4096);
    BAD(stats.live_buffers,0);BAD(stats.live_buffers,2);
    BAD(stats.open_sessions,0);BAD(stats.open_sessions,2);
    BAD(stats.quarantined_buffers,0);BAD(stats.quarantined_buffers,2);
    BAD(collect.retained,0);BAD(collect.retained,2);
    BAD(collect.backend_failures,0);BAD(collect.backend_failures,2);
    BAD(collect.quarantined,0);BAD(collect.quarantined,2);
    done("BLOCKED cannot disguise lost bytes, leaked sessions or unrelated backend failures");
}
static void polling(void) {
    for(unsigned capability=0;capability<2;capability++) {
        scenario x=live();
        if(capability) { x.capabilities|=DLMP_CAP_MAPPED_RECLAIM;x.stats.quarantined_buffers=0; }
        assert(classify(&x)==DLMP_RESULT_WAIT);
        x.total_drained=1;x.collect.drained=1;x.stats.drained_maps++;x.stats.pending_maps--;
        assert(classify(&x)==DLMP_RESULT_WAIT);
        BAD(collect.backend_failures,1);BAD(collect.quarantined,1);
        BAD(stats.resident_bytes,0);BAD(stats.live_buffers,0);BAD(stats.pending_maps,0);
        BAD(stats.reclaimed_buffers,x.baseline.reclaimed_buffers+1);
        x.collect.drained=0;assert(classify(&x)==DLMP_RESULT_WAIT);
        x.stats.drained_maps--;x.stats.pending_maps++;assert(classify(&x)==DLMP_RESULT_FAILED);
    }
    done("bounded polling permits delayed notifications but rejects counter regression or early reclamation");
}
static void future_pass(void) {
    scenario x=passed();assert(classify(&x)==DLMP_RESULT_PASSED);
    BAD(stats.resident_bytes,1);BAD(stats.live_buffers,1);BAD(collect.retained,1);
    BAD(stats.quarantined_buffers,1);BAD(collect.backend_failures,1);BAD(collect.quarantined,1);
    BAD(stats.reclaimed_buffers,x.baseline.reclaimed_buffers);
    BAD(stats.reclaimed_buffers,x.baseline.reclaimed_buffers+2);
    BAD(total_reclaimed,0);BAD(total_reclaimed,2);BAD(total_drained,1);
    x.collect.reclaimed=0;x.collect.drained=0;assert(classify(&x)==DLMP_RESULT_PASSED);
    x=blocked();x.capabilities|=DLMP_CAP_MAPPED_RECLAIM;x.stats.quarantined_buffers=0;
    x.collect.backend_failures=0;x.collect.quarantined=0;
    assert(classify(&x)==DLMP_RESULT_FAILED);
    done("future mapped PASS requires capability and complete reclamation with no failure or quarantine");
}
static void checkpoints(void) {
    scenario x=blocked();
    for(unsigned mask=0;mask<16;mask++) {
        x.checkpoints=mask;
        assert(classify(&x)==(mask==DLMP_CHECK_COMPLETE?DLMP_RESULT_BLOCKED:DLMP_RESULT_FAILED));
    }
    x=passed();
    BAD(checkpoints,DLMP_CHECK_PARENT_LIVE|DLMP_CHECK_CHILD_EXITED);
    BAD(checkpoints,DLMP_CHECK_CHILD_LIVE|DLMP_CHECK_CHILD_EXITED);
    BAD(checkpoints,DLMP_CHECK_PARENT_LIVE|DLMP_CHECK_CHILD_LIVE);
    done("neither BLOCKED nor PASS is available before both live checkpoints and successful child exit");
}
static void historical_counters(void) {
    scenario x=blocked();
    x.baseline.published_maps=UINT64_MAX-2;x.baseline.drained_maps=UINT64_MAX-2;
    x.stats.published_maps=UINT64_MAX;x.stats.drained_maps=UINT64_MAX;
    x.baseline.reclaimed_buffers=UINT64_MAX;x.stats.reclaimed_buffers=UINT64_MAX;
    assert(classify(&x)==DLMP_RESULT_BLOCKED);
    x.baseline.published_maps=UINT64_MAX-1;x.baseline.drained_maps=UINT64_MAX-1;
    x.stats.published_maps=0;x.stats.drained_maps=0;assert(classify(&x)==DLMP_RESULT_FAILED);
    x=passed();x.baseline.reclaimed_buffers=UINT64_MAX;x.stats.reclaimed_buffers=0;
    assert(classify(&x)==DLMP_RESULT_FAILED);
    assert(!dlmp_result_delta(0,UINT64_MAX,1));
    done("historical deltas accept nonzero totals without accepting unsigned wraparound");
}
static void allocated_and_unmapped(void) {
    scenario x=blocked();x.stats=x.baseline;x.stats.live_buffers=1;x.stats.resident_bytes=x.bytes;
    assert(dlmp_result_allocated(x.bytes,&x.baseline,&x.stats));
    x.stats.resident_bytes=x.bytes-1;assert(!dlmp_result_allocated(x.bytes,&x.baseline,&x.stats));
    x.stats.resident_bytes=x.bytes;x.stats.open_sessions=1;
    assert(!dlmp_result_allocated(x.bytes,&x.baseline,&x.stats));
    x.stats.open_sessions=2;x.stats.pending_maps=1;
    assert(!dlmp_result_allocated(x.bytes,&x.baseline,&x.stats));
    x.stats.pending_maps=0;x.stats.quarantined_buffers=1;
    assert(!dlmp_result_allocated(x.bytes,&x.baseline,&x.stats));
    x.stats.quarantined_buffers=0;x.stats.published_maps++;
    assert(!dlmp_result_allocated(x.bytes,&x.baseline,&x.stats));
    x.stats.published_maps--;x.stats.own_object=1;
    assert(!dlmp_result_allocated(x.bytes,&x.baseline,&x.stats));
    x.stats=x.baseline;x.stats.open_sessions=1;x.stats.reclaimed_buffers++;
    x.collect=(struct dlmp_collect){.version=DLMP_VERSION,.reclaimed=1};
    assert(dlmp_result_unmapped(&x.baseline,&x.stats,&x.collect));
    x.collect.backend_failures=1;x.collect.retained=1;
    x.stats.live_buffers=1;x.stats.resident_bytes=x.bytes;
    assert(!dlmp_result_unmapped(&x.baseline,&x.stats,&x.collect));
    x.collect.backend_failures=0;x.collect.retained=0;
    assert(!dlmp_result_unmapped(&x.baseline,&x.stats,&x.collect));
    x.stats.live_buffers=0;x.stats.resident_bytes=0;
    x.collect.quarantined=1;x.collect.backend_failures=1;
    assert(!dlmp_result_unmapped(&x.baseline,&x.stats,&x.collect));
    x.collect.quarantined=0;x.collect.backend_failures=0;x.stats.drained_maps++;
    assert(!dlmp_result_unmapped(&x.baseline,&x.stats,&x.collect));
    x.stats.drained_maps--;x.stats.reclaimed_buffers=1;
    assert(!dlmp_result_unmapped(&x.baseline,&x.stats,&x.collect));
    done("allocation and unmapped cleanup validate rounding, copyout and historical deltas");
}
static void absent_observations(void) {
    scenario x=blocked();
    assert(!dlmp_result_baseline(NULL));
    assert(dlmp_result_mapped(x.capabilities,x.bytes,NULL,&x.stats,&x.collect,2,0,7)==DLMP_RESULT_FAILED);
    assert(dlmp_result_mapped(x.capabilities,x.bytes,&x.baseline,NULL,&x.collect,2,0,7)==DLMP_RESULT_FAILED);
    assert(dlmp_result_mapped(x.capabilities,x.bytes,&x.baseline,&x.stats,NULL,2,0,7)==DLMP_RESULT_FAILED);
    done("missing observations cannot manufacture a successful result");
}
int main(void) {
    baseline_copyout();exact_blocked();early_notification();live_accounting();
    no_capability_no_pass();identity();publication_and_totals();quarantine_receipt();
    polling();future_pass();checkpoints();historical_counters();allocated_and_unmapped();absent_observations();
    printf("1..%u\n",tests);return 0;
}
