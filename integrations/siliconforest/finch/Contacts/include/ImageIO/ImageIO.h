/* SPDX-License-Identifier: MIT */
/* The ImageIO functions used by Finch Contacts are implemented by PD CoreGraphics. */
#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <CoreGraphics/CGImageSource.h>
#include <CoreGraphics/CGImageDestination.h>
/* Additional public ImageIO surface referenced by Finch. The installed root must
 * supply these; declarations alone do not supply the implementations. */
extern const CFStringRef kCGImageSourceCreateThumbnailFromImageAlways;
CGImageRef CGImageSourceCreateThumbnailAtIndex(CGImageSourceRef source, size_t index, CFDictionaryRef options);
extern const CFStringRef kCGImageSourceThumbnailMaxPixelSize;
extern const CFStringRef kCGImageSourceCreateThumbnailWithTransform;
