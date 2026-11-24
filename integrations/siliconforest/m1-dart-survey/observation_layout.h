/* The existing V1 diagnostic wire layout; no sampler behavior changes. */
#ifndef DLS_OBSERVATION_LAYOUT_H
#define DLS_OBSERVATION_LAYOUT_H
#include "survey.h"
#ifdef __cplusplus
#define DLS_LAYOUT_ASSERT(test) static_assert((test), #test)
#define DLS_LAYOUT_ALIGN(type) alignof(type)
#else
#define DLS_LAYOUT_ASSERT(test) _Static_assert((test), #test)
#define DLS_LAYOUT_ALIGN(type) _Alignof(type)
#endif
DLS_LAYOUT_ASSERT(sizeof(struct dls_sample)==8);
DLS_LAYOUT_ASSERT(offsetof(struct dls_sample,offset)==0);
DLS_LAYOUT_ASSERT(offsetof(struct dls_sample,value)==4);
DLS_LAYOUT_ASSERT(sizeof(struct dls_snapshot)==764);
DLS_LAYOUT_ASSERT(DLS_LAYOUT_ALIGN(struct dls_snapshot)==4);
DLS_LAYOUT_ASSERT(offsetof(struct dls_snapshot,version)==0);
DLS_LAYOUT_ASSERT(offsetof(struct dls_snapshot,sample_count)==4);
DLS_LAYOUT_ASSERT(offsetof(struct dls_snapshot,reads_attempted)==8);
DLS_LAYOUT_ASSERT(offsetof(struct dls_snapshot,failure_region)==12);
DLS_LAYOUT_ASSERT(offsetof(struct dls_snapshot,failure_offset)==16);
DLS_LAYOUT_ASSERT(offsetof(struct dls_snapshot,result)==20);
DLS_LAYOUT_ASSERT(offsetof(struct dls_snapshot,power)==24);
DLS_LAYOUT_ASSERT(offsetof(struct dls_snapshot,clocks)==40);
DLS_LAYOUT_ASSERT(offsetof(struct dls_snapshot,samples)==60);
#undef DLS_LAYOUT_ALIGN
#undef DLS_LAYOUT_ASSERT
#endif
