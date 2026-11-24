/* Original Darwin Lab code. License selection pending.
 * Pure classification of supplied observations, not kernel execution evidence.
 * Header-only so memory_probe_smoke.c remains a single-file build. */
#ifndef DARWIN_LAB_MEMORY_PROBE_RESULT_H
#define DARWIN_LAB_MEMORY_PROBE_RESULT_H
#include "memory_probe_protocol.h"
#include <stdbool.h>
#include <stddef.h>

enum dlmp_result {
    DLMP_RESULT_PASSED = 0, DLMP_RESULT_FAILED = 1,
    DLMP_RESULT_WAIT = 2, DLMP_RESULT_BLOCKED = 77
};
enum dlmp_checkpoint {
    DLMP_CHECK_PARENT_LIVE = 1u,
    DLMP_CHECK_CHILD_LIVE = 2u,
    DLMP_CHECK_CHILD_EXITED = 4u,
    DLMP_CHECK_COMPLETE = 7u
};

static inline bool dlmp_result_stats_valid(const struct dlmp_stats *s) {
    /* Observations come from the unbound monitor session. */
    return s && s->version==DLMP_VERSION && !s->flags &&
           !s->own_object && !s->own_bytes;
}
static inline bool dlmp_result_baseline(const struct dlmp_stats *s) {
    /* Require copyout evidence: the input initializer has open_sessions == 0.
     * Historical publication/drain/reclamation totals need not start at zero. */
    return dlmp_result_stats_valid(s) && s->open_sessions==2 &&
           !s->resident_bytes && !s->live_buffers && !s->pending_maps &&
           !s->quarantined_buffers && s->published_maps==s->drained_maps;
}
static inline bool dlmp_result_capabilities(uint64_t capabilities) {
    const uint64_t required=DLMP_CAP_RAM_ONLY|DLMP_CAP_KERNEL_BACKEND;
    return (capabilities & required)==required &&
           !(capabilities & ~(required|DLMP_CAP_MAPPED_RECLAIM));
}
static inline bool dlmp_result_collection_valid(const struct dlmp_collect *c) {
    return c && c->version==DLMP_VERSION && !c->flags && c->drained<=2 &&
           c->reclaimed<=1 && c->retained<=1 && c->backend_failures<=1 &&
           c->quarantined<=c->backend_failures;
}
static inline bool dlmp_result_delta(uint64_t now,uint64_t baseline,uint64_t wanted) {
    return now>=baseline && now-baseline==wanted;
}
static inline bool dlmp_result_counters(const struct dlmp_stats *baseline,
                                       const struct dlmp_stats *s,
                                       uint64_t published,uint64_t drained,uint64_t reclaimed) {
    return published>=drained &&
           dlmp_result_delta(s->published_maps,baseline->published_maps,published) &&
           dlmp_result_delta(s->drained_maps,baseline->drained_maps,drained) &&
           dlmp_result_delta(s->reclaimed_buffers,baseline->reclaimed_buffers,reclaimed) &&
           s->pending_maps==published-drained;
}
static inline bool dlmp_result_allocated(uint64_t bytes,const struct dlmp_stats *baseline,
                                        const struct dlmp_stats *s) {
    return bytes && dlmp_result_baseline(baseline) && dlmp_result_stats_valid(s) &&
           s->open_sessions==2 && s->resident_bytes==bytes && s->live_buffers==1 &&
           !s->quarantined_buffers && dlmp_result_counters(baseline,s,0,0,0);
}
static inline bool dlmp_result_live(uint64_t capabilities,uint64_t bytes,
                                   const struct dlmp_stats *baseline,
                                   const struct dlmp_stats *s,const struct dlmp_collect *c) {
    const uint64_t quarantined=(capabilities & DLMP_CAP_MAPPED_RECLAIM)?0:1;
    return dlmp_result_capabilities(capabilities) && bytes &&
           dlmp_result_baseline(baseline) && dlmp_result_stats_valid(s) &&
           dlmp_result_collection_valid(c) &&
           s->open_sessions==1 && s->resident_bytes==bytes && s->live_buffers==1 &&
           s->quarantined_buffers==quarantined && dlmp_result_counters(baseline,s,2,0,0) &&
           c->retained==1 && !c->reclaimed && !c->drained &&
           !c->quarantined && !c->backend_failures;
}
static inline bool dlmp_result_unmapped(const struct dlmp_stats *baseline,
                                       const struct dlmp_stats *s,const struct dlmp_collect *c) {
    return dlmp_result_baseline(baseline) && dlmp_result_stats_valid(s) &&
           dlmp_result_collection_valid(c) && s->open_sessions==1 &&
           !s->resident_bytes && !s->live_buffers && !s->quarantined_buffers &&
           dlmp_result_counters(baseline,s,0,0,1) && c->reclaimed==1 &&
           !c->retained && !c->drained && !c->quarantined && !c->backend_failures;
}
static inline enum dlmp_result dlmp_result_mapped(
    uint64_t capabilities,uint64_t bytes,const struct dlmp_stats *baseline,
    const struct dlmp_stats *s,const struct dlmp_collect *c,
    uint64_t total_drained,uint64_t total_reclaimed,unsigned checkpoints) {
    if(checkpoints!=DLMP_CHECK_COMPLETE || !bytes ||
       !dlmp_result_capabilities(capabilities) || !dlmp_result_baseline(baseline) ||
       !dlmp_result_stats_valid(s) || !dlmp_result_collection_valid(c) ||
       s->open_sessions!=1 || total_drained>2 || total_reclaimed>1 ||
       !dlmp_result_counters(baseline,s,2,total_drained,total_reclaimed) ||
       c->drained>total_drained || c->reclaimed>total_reclaimed)
        return DLMP_RESULT_FAILED;

    const bool can_reclaim=(capabilities & DLMP_CAP_MAPPED_RECLAIM)!=0;
    if(s->quarantined_buffers!=(can_reclaim?0u:1u)) return DLMP_RESULT_FAILED;

    if(total_drained<2) {
        if(total_reclaimed || c->reclaimed || c->backend_failures || c->quarantined ||
           c->retained!=1 || s->resident_bytes!=bytes || s->live_buffers!=1)
            return DLMP_RESULT_FAILED;
        return DLMP_RESULT_WAIT;
    }
    if(!can_reclaim) {
        /* Reclamation without the capability is a violation, never a PASS. */
        if(total_reclaimed || c->reclaimed || c->retained!=1 ||
           c->quarantined!=1 || c->backend_failures!=1 ||
           s->resident_bytes!=bytes || s->live_buffers!=1)
            return DLMP_RESULT_FAILED;
        return DLMP_RESULT_BLOCKED;
    }
    if(c->backend_failures || c->quarantined) return DLMP_RESULT_FAILED;
    if(total_reclaimed==1) {
        if(s->resident_bytes || s->live_buffers || c->retained) return DLMP_RESULT_FAILED;
        return DLMP_RESULT_PASSED;
    }
    /* The serialized engine attempts reclamation in the same collection that
     * drains the final hold. Retention without a reported failure is invalid. */
    return DLMP_RESULT_FAILED;
}
#endif
