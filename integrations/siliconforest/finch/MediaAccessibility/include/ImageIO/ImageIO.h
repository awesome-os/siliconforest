/* SPDX-License-Identifier: MIT */
/* Own declarations for Finch's metadata calls; implementations must be supplied. */
#pragma once
#include <CoreGraphics/CGImageSource.h>
#include <CoreGraphics/CGImageDestination.h>
typedef struct __CGImageMetadata *CGMutableImageMetadataRef;
typedef const struct __CGImageMetadata *CGImageMetadataRef;
typedef const struct __CGImageMetadataTag *CGImageMetadataTagRef;
typedef CFIndex CGImageMetadataType;
enum { kCGImageMetadataTypeString = 1 };
CGImageMetadataTagRef CGImageMetadataCopyTagWithPath(CGImageMetadataRef, CGImageMetadataTagRef, CFStringRef);
CFTypeRef CGImageMetadataTagCopyValue(CGImageMetadataTagRef);
CGImageMetadataRef CGImageSourceCopyMetadataAtIndex(CGImageSourceRef, size_t, CFDictionaryRef);
CGMutableImageMetadataRef CGImageMetadataCreateMutableCopy(CGImageMetadataRef);
CGMutableImageMetadataRef CGImageMetadataCreateMutable(void);
CGImageMetadataTagRef CGImageMetadataTagCreate(CFStringRef, CFStringRef, CFStringRef, CGImageMetadataType, CFTypeRef);
bool CGImageMetadataSetTagWithPath(CGMutableImageMetadataRef, CGImageMetadataTagRef, CFStringRef, CGImageMetadataTagRef);
extern const CFStringRef kCGImageDestinationMetadata;
