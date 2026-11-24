/* SPDX-License-Identifier: MIT */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <libavutil/frame.h>
/* +1 private lifetime object (not a mutable attachment). It bridges producer usecount through callback
 * lifetime and holds the actual AVFrame until all coordinated consumers/fences
 * have dropped their surface usecounts. Count alone does not create a fence. */
CFTypeRef SFVTCreateFrameLease(const AVFrame *frame,IOSurfaceRef surface);
void SFVTFrameLeaseCounts(uint64_t *issued,uint64_t *released,uint64_t *deferred);
typedef struct SFFrameLeaseGroup SFFrameLeaseGroup;
/* Private invalidated-session control, ONLY for separately owned external GPU
 * storage. Retire after serialized decoder/frame/device teardown, never during
 * decode/flush. It drops AVFrame pool holds but retains IOSurface/usecount holds
 * until the existing consumer/GPU-completion contract permits release.
 * Caller serializes CreateGroupedFrameLease with Retire (VT session mutex).
 * Consumer destruction may race retirement; the internal list lock covers it. */
SFFrameLeaseGroup *SFVTFrameLeaseGroupCreate(void);
CFTypeRef SFVTCreateGroupedFrameLease(const AVFrame *,IOSurfaceRef,SFFrameLeaseGroup *);
void SFVTFrameLeaseGroupRetire(SFFrameLeaseGroup *);
