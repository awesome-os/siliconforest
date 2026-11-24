/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Window forms follow Linux apple-dart.c (6a08a08e).
 * Copyright (C) The Asahi Linux Contributors */
#ifndef SF_DART_SPEC_H
#define SF_DART_SPEC_H
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#define SF_DART_WINDOW_LO 0x10000000ULL
#define SF_DART_WINDOW_HI 0x90000000ULL
/* Native allocator uses this fixed inherited-table aperture. Support Linux's
 * one-cell SID and five-cell SID/start64/length64 forms, including consumer
 * override. Refuse narrower windows instead of silently allocating outside
 * them. All values are host-endian OF cells. Output unchanged on failure. */
static inline int sf_dart_window_valid(uint64_t start,uint64_t length)
{
    return length && !((start|length)&0x3fff) && start<(1ULL<<32) && length<=(1ULL<<32)-start;
}
static inline int sf_dart_spec(const uint32_t *args,unsigned count,
                               const uint64_t *consumer_window,uint32_t *sid)
{
    if(!args || !sid || (count!=1 && count!=5) || args[0]>=16)return -EINVAL;
    uint64_t start=0,length=1ULL<<32;
    if(count==5){start=(uint64_t)args[1]<<32|args[2];length=(uint64_t)args[3]<<32|args[4];}
    if(!sf_dart_window_valid(start,length))return -EINVAL;
    if(consumer_window){start=consumer_window[0];length=consumer_window[1];}
    if(!sf_dart_window_valid(start,length))return -EINVAL;
    if(start>SF_DART_WINDOW_LO || start+length<SF_DART_WINDOW_HI)return -EOPNOTSUPP;
    *sid=args[0];return 0;
}
#endif
