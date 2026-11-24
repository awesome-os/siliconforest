/* SPDX-License-Identifier: MIT */
/* Own declarations for the ColorSync dependency used by Finch vImage. */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
typedef CFTypeRef ColorSyncProfileRef, ColorSyncTransformRef;
typedef unsigned ColorSyncDataDepth, ColorSyncDataLayout;
enum { kColorSync32BitFloat=7, kColorSyncByteOrderDefault=0 };
extern const CFStringRef kColorSyncSRGBProfile, kColorSyncGenericGrayGamma22Profile;
extern const CFStringRef kColorSyncDisplayP3Profile;
extern const CFStringRef kColorSyncGenericCMYKProfile, kColorSyncGenericLabProfile, kColorSyncGenericXYZProfile;
extern const CFStringRef kColorSyncProfile, kColorSyncRenderingIntent, kColorSyncTransformTag;
extern const CFStringRef kColorSyncRenderingIntentAbsolute, kColorSyncRenderingIntentRelative;
extern const CFStringRef kColorSyncRenderingIntentSaturation, kColorSyncRenderingIntentPerceptual;
extern const CFStringRef kColorSyncTransformDeviceToPCS, kColorSyncTransformPCSToDevice;
ColorSyncProfileRef ColorSyncProfileCreateWithName(CFStringRef);
ColorSyncProfileRef ColorSyncProfileCreate(CFDataRef, CFErrorRef *);
ColorSyncTransformRef ColorSyncTransformCreate(CFArrayRef, CFDictionaryRef);
bool ColorSyncTransformConvert(ColorSyncTransformRef,size_t,size_t,void *,ColorSyncDataDepth,ColorSyncDataLayout,size_t,const void *,ColorSyncDataDepth,ColorSyncDataLayout,size_t,CFDictionaryRef);
