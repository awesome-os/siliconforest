// SPDX-License-Identifier: MIT
// ColorSync.framework: named profiles and transforms, as Terminal.app imports them. (The
// tree's own ColorSync.c, the display UUID calls Firefox uses, is the Cocoa stream's build and
// is not on the disk root; the two belong in one framework.) No colour management engine exists on this system: a profile
// by name is an object that knows its name, and a transform cannot be made (callers get NULL
// and keep their unconverted colours).
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
const CFStringRef kColorSyncGenericLabProfile = CFSTR("com.apple.ColorSync.GenericLab");
const CFStringRef kColorSyncSRGBProfile = CFSTR("com.apple.ColorSync.sRGB");
const CFStringRef kColorSyncProfile = CFSTR("com.apple.ColorSync.Profile");
const CFStringRef kColorSyncRenderingIntent = CFSTR("com.apple.ColorSync.RenderingIntent");
const CFStringRef kColorSyncRenderingIntentPerceptual = CFSTR("com.apple.ColorSync.RenderingIntentPerceptual");
const CFStringRef kColorSyncTransformTag = CFSTR("com.apple.ColorSync.TransformTag");
const CFStringRef kColorSyncTransformDeviceToPCS = CFSTR("com.apple.ColorSync.TransformDeviceToPCS");
const CFStringRef kColorSyncTransformPCSToDevice = CFSTR("com.apple.ColorSync.TransformPCSToDevice");
CFTypeRef ColorSyncProfileCreateWithName(CFStringRef name) { return name ? CFStringCreateCopy(NULL, name) : NULL; }
CFTypeRef ColorSyncTransformCreate(CFArrayRef profileSequence, CFDictionaryRef options) { return NULL; }
bool ColorSyncTransformConvert(CFTypeRef transform, size_t width, size_t height, void *dst, int dstDepth, int dstLayout, size_t dstBytesPerRow,
    const void *src, int srcDepth, int srcLayout, size_t srcBytesPerRow, CFDictionaryRef options) { return false; }
