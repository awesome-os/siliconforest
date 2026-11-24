#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Private one-shot buffer lifetime slot, independent of mutable attachments."""
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
a='    CVPixelBufferPoolRef pool;\n';assert s.count(a)==1
s=s.replace(a,a+'    _Atomic(CFTypeRef) sfLifetime;\n')
a='    _FinchCVBufferInit(&b->cv);\n';assert s.count(a)==1
s=s.replace(a,a+'    atomic_init(&b->sfLifetime, NULL);\n')
a='    _FinchCVBufferFinalize(&b->cv);\n';assert s.count(a)==1
s=s.replace(a,a+'    CFTypeRef lifetime = atomic_load(&b->sfLifetime);\n    if (lifetime) CFRelease(lifetime);\n')
# Planar row rounding advertises dataSize, which can exceed the last plane's
# end. Reserve the advertised bytes, while preserving the native 512-byte
# planar header and every existing plane offset.
a='''        b->dataSize = b->bytesPerRow * b->height;
        return kCVReturnSuccess;''';assert s.count(a)==1
s=s.replace(a,'''        if (b->bytesPerRow && b->height > SIZE_MAX / b->bytesPerRow)
            return kCVReturnInvalidArgument;
        b->dataSize = b->bytesPerRow * b->height;
        if (b->dataSize < at) return kCVReturnInvalidArgument;
        b->allocSize = b->dataSize;
        return kCVReturnSuccess;''')
a='        SET(kIOSurfaceBytesPerRow, b->planes[0].bytesPerRow);';assert s.count(a)==1
s=s.replace(a,'''        /* A planar surface's top-level row is the flattened CV row, not Y's
         * stride. Per-plane consumers must use the per-plane row getters. */
        SET(kIOSurfaceBytesPerRow, b->bytesPerRow);
        if (b->allocSize > SIZE_MAX - 64) { CFRelease(p); return NULL; }
        size_t requested = dict_size(p, kIOSurfaceAllocSize, 0);
        if (requested < b->allocSize + 64)
            SET(kIOSurfaceAllocSize, b->allocSize + 64);''')
a='    CFDictionaryRef surfaceProperties = attributes ? CFDictionaryGetValue(attributes, kCVPixelBufferIOSurfacePropertiesKey) : NULL;';assert s.count(a)==1
s=s.replace(a,'''    if (!b->allocSize || b->allocSize > SIZE_MAX - 64)
        return kCVReturnInvalidArgument;
'''+a)
s+='''
/* Private producer API. Set exactly once, before publication, on a non-pooled
 * IOSurface wrapper. Removing/copying public attachments cannot drop it. */
CVReturn CVPixelBufferSFSetLifetimeObject(CVPixelBufferRef b, CFTypeRef lifetime)
{
    if (!_FinchCVIsBuffer(b) || !lifetime || !b->surface || b->pool)
        return kCVReturnInvalidArgument;
    CFRetain(lifetime);
    CFTypeRef expected = NULL;
    if (!atomic_compare_exchange_strong(&b->sfLifetime, &expected, lifetime)) {
        CFRelease(lifetime);
        return kCVReturnInvalidArgument;
    }
    return kCVReturnSuccess;
}
'''
p.write_text(s)
