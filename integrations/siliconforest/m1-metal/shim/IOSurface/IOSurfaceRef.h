// SPDX-License-Identifier: MIT
// Our own declarations (SiliconForest m1-metal) of the IOSurface functions darling-metal calls, as
// the system's IOSurface.framework (SiliconForest's) exports them; no Apple header was used.
#ifndef SF_IOSURFACEREF_H
#define SF_IOSURFACEREF_H
#include <stddef.h>
#include <stdint.h>
#include <CoreFoundation/CoreFoundation.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct __IOSurface *IOSurfaceRef;
typedef uint32_t IOSurfaceID;
enum { kIOSurfaceLockReadOnly = 0x1, kIOSurfaceLockAvoidSync = 0x2 };
typedef uint32_t IOSurfaceLockOptions;
// weak: Metal.framework links them by dynamic lookup (no link stub of the system's IOSurface here);
// a process that never touches an IOSurface-backed texture need not load IOSurface.framework
#define SF_IOS __attribute__((weak_import))
SF_IOS int IOSurfaceLock(IOSurfaceRef surface, IOSurfaceLockOptions options, uint32_t *seed);
SF_IOS int IOSurfaceUnlock(IOSurfaceRef surface, IOSurfaceLockOptions options, uint32_t *seed);
SF_IOS uint32_t IOSurfaceGetSeed(IOSurfaceRef surface);
SF_IOS void *IOSurfaceGetBaseAddress(IOSurfaceRef surface);
SF_IOS size_t IOSurfaceGetBytesPerRow(IOSurfaceRef surface);
SF_IOS size_t IOSurfaceGetPlaneCount(IOSurfaceRef surface);
SF_IOS size_t IOSurfaceGetWidthOfPlane(IOSurfaceRef surface, size_t plane);
SF_IOS size_t IOSurfaceGetHeightOfPlane(IOSurfaceRef surface, size_t plane);
SF_IOS size_t IOSurfaceGetBytesPerRowOfPlane(IOSurfaceRef surface, size_t plane);
SF_IOS size_t IOSurfaceGetBytesPerElementOfPlane(IOSurfaceRef surface, size_t plane);
SF_IOS void *IOSurfaceGetBaseAddressOfPlane(IOSurfaceRef surface, size_t plane);
SF_IOS size_t IOSurfaceGetWidth(IOSurfaceRef surface);
SF_IOS size_t IOSurfaceGetHeight(IOSurfaceRef surface);
#ifdef __cplusplus
}
#endif
#endif
