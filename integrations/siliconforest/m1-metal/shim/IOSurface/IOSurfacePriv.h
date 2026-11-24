// SPDX-License-Identifier: MIT
// Darling's private IOSurface interface as darling-metal uses it (dma-buf export of a surface, GPU
// write-back callbacks). The system's IOSurface (SiliconForest's) has neither: here a surface never has a
// dma-buf (darling-metal then copies) and the callbacks are not registered anywhere. Ours, SiliconForest m1-metal.
#ifndef SF_IOSURFACEPRIV_H
#define SF_IOSURFACEPRIV_H
#include <stdbool.h>
#include <IOSurface/IOSurfaceRef.h>
#define _IOSURFACE_DMABUF_VERSION 1
typedef struct { uint32_t version; int fd; uint64_t offset; uint32_t bytesPerRow; } _IOSurfaceDMABuf;
#define _IOSURFACE_GPU_CALLBACKS_VERSION 1
typedef struct { uint32_t version; void (*writeback)(IOSurfaceRef); void (*forget)(IOSurfaceRef); } _IOSurfaceGPUCallbacks;
static inline bool _IOSurfaceGetDMABuf(IOSurfaceRef s, size_t plane, _IOSurfaceDMABuf *b) { (void)s; (void)plane; (void)b; return false; }
static inline void _IOSurfaceSetGPUCallbacks(const _IOSurfaceGPUCallbacks *c) { (void)c; }
#endif
