/* SPDX-License-Identifier: MIT */
/* Own declarations from Finch source and public API type/numeric facts. */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct ColorSyncProfile *ColorSyncProfileRef, *ColorSyncMutableProfileRef;
typedef struct ColorSyncTransform *ColorSyncTransformRef;
typedef struct ColorSyncCMM *ColorSyncCMMRef;
typedef unsigned ColorSyncDataDepth, ColorSyncDataLayout;
typedef struct { uint8_t digest[16]; } ColorSyncMD5;
typedef bool (*ColorSyncProfileIterateCallback)(CFDictionaryRef,void *);
typedef bool (*ColorSyncDeviceProfileIterateCallback)(CFDictionaryRef,void *);
typedef bool (*ColorSyncCMMIterateCallback)(ColorSyncCMMRef,void *);
enum {
    kColorSync10BitInteger=8,
    kColorSync32BitNamedColorIndex=6,
    kColorSyncByteOrder16Little=4096,
    kColorSyncByteOrder32Little=8192,
    kColorSyncByteOrderDefault=0,
    kColorSync16BitFloat=4,
    kColorSync16BitInteger=3,
    kColorSync1BitGamut=1,
    kColorSync32BitFloat=7,
    kColorSync32BitInteger=5,
    kColorSync8BitInteger=2,
    kColorSyncAlphaFirst=4,
    kColorSyncAlphaInfoMask=31,
    kColorSyncAlphaLast=3,
    kColorSyncAlphaNone=0,
    kColorSyncAlphaNoneSkipFirst=6,
    kColorSyncAlphaNoneSkipLast=5,
    kColorSyncAlphaPremultipliedFirst=2,
    kColorSyncAlphaPremultipliedLast=1,
    kColorSyncByteOrder16Big=12288,
    kColorSyncByteOrder32Big=16384,
    kColorSyncByteOrderMask=28672,
};
#ifndef SF_COLORSYNC_PREPROCESS
#include <ColorSync/Functions.h>
#endif
