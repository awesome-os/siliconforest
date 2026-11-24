/* SPDX-License-Identifier: MIT */
/* Public IOSurface declarations used by Finch; implementation stays PD's. */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
typedef struct __IOSurface *IOSurfaceRef;
typedef uint32_t IOSurfaceID,IOSurfaceLockOptions;
enum { kIOSurfaceLockReadOnly = 1 };
CFTypeID IOSurfaceGetTypeID(void);
IOSurfaceRef IOSurfaceCreate(CFDictionaryRef properties);
IOSurfaceID IOSurfaceGetID(IOSurfaceRef surface);
void *IOSurfaceGetBaseAddress(IOSurfaceRef surface);
void *IOSurfaceGetBaseAddressOfPlane(IOSurfaceRef surface,size_t plane);
size_t IOSurfaceGetWidth(IOSurfaceRef),IOSurfaceGetHeight(IOSurfaceRef),IOSurfaceGetBytesPerRow(IOSurfaceRef),IOSurfaceGetAllocSize(IOSurfaceRef),IOSurfaceGetPlaneCount(IOSurfaceRef);
size_t IOSurfaceGetWidthOfPlane(IOSurfaceRef,size_t),IOSurfaceGetHeightOfPlane(IOSurfaceRef,size_t),IOSurfaceGetBytesPerRowOfPlane(IOSurfaceRef,size_t);
uint32_t IOSurfaceGetPixelFormat(IOSurfaceRef);
int IOSurfaceLock(IOSurfaceRef,IOSurfaceLockOptions,uint32_t*);
int IOSurfaceUnlock(IOSurfaceRef,IOSurfaceLockOptions,uint32_t*);
extern const CFStringRef kIOSurfaceBytesPerElement;
extern const CFStringRef kIOSurfaceAllocSize;
extern const CFStringRef kIOSurfaceBytesPerRow;
extern const CFStringRef kIOSurfaceElementWidth;
extern const CFStringRef kIOSurfaceHeight;
extern const CFStringRef kIOSurfacePixelFormat;
extern const CFStringRef kIOSurfacePlaneBytesPerElement;
extern const CFStringRef kIOSurfacePlaneBytesPerRow;
extern const CFStringRef kIOSurfacePlaneElementWidth;
extern const CFStringRef kIOSurfacePlaneHeight;
extern const CFStringRef kIOSurfacePlaneInfo;
extern const CFStringRef kIOSurfacePlaneOffset;
extern const CFStringRef kIOSurfacePlaneSize;
extern const CFStringRef kIOSurfacePlaneWidth;
extern const CFStringRef kIOSurfaceWidth;
