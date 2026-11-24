// SPDX-License-Identifier: MIT
// CoreServices additions (wrapper over CoreServices, re-exported): the Unicode converter's
// text-to-Unicode calls, on CFString's converters.
#include <CoreFoundation/CoreFoundation.h>
#include <stdlib.h>
typedef int32_t OSStatus;
typedef uint32_t TextEncoding;
enum { paramErr = -50, memFullErr = -108, kTextUnsupportedEncodingErr = -8738, kTECOutputBufferFullStatus = -8785, kTextMalformedInputErr = -8739 };
struct SFTextToUnicodeInfo { CFStringEncoding encoding; };

const CFStringRef kUTTypeSymLink = CFSTR("public.symlink");

OSStatus CreateTextToUnicodeInfoByEncoding(TextEncoding encoding, struct SFTextToUnicodeInfo **info)
{
    if (info == NULL) return paramErr;
    *info = NULL;
    if (!CFStringIsEncodingAvailable(encoding & 0xffff)) return kTextUnsupportedEncodingErr;
    *info = calloc(1, sizeof(**info));
    if (*info == NULL) return memFullErr;
    (*info)->encoding = encoding & 0xffff;
    return 0;
}
struct SFUnicodeMapping { TextEncoding unicodeEncoding, otherEncoding; int32_t mappingVersion; };
OSStatus CreateTextToUnicodeInfo(const struct SFUnicodeMapping *mapping, struct SFTextToUnicodeInfo **info)
{
    if (mapping == NULL) return paramErr;
    return CreateTextToUnicodeInfoByEncoding(mapping->otherEncoding, info);
}
OSStatus DisposeTextToUnicodeInfo(struct SFTextToUnicodeInfo **info)
{
    if (info == NULL || *info == NULL) return paramErr;
    free(*info); *info = NULL;
    return 0;
}
OSStatus ConvertFromTextToUnicode(struct SFTextToUnicodeInfo *info, unsigned long sourceLen, const void *source, uint32_t flags,
    unsigned long offsetCount, const unsigned long *offsets, unsigned long *outOffsetCount, unsigned long *outOffsets,
    unsigned long outputBufLen, unsigned long *sourceRead, unsigned long *unicodeLen, UniChar *unicode)
{
    if (info == NULL || (source == NULL && sourceLen) || unicode == NULL) return paramErr;
    if (outOffsetCount) *outOffsetCount = 0;
    if (sourceRead) *sourceRead = 0;
    if (unicodeLen) *unicodeLen = 0;
    // the longest prefix that is whole characters in the encoding
    unsigned long take = sourceLen;
    CFStringRef s = NULL;
    while (take > 0 && (s = CFStringCreateWithBytes(NULL, source, (CFIndex)take, info->encoding, false)) == NULL && sourceLen - take < 8) take--;
    if (s == NULL) return sourceLen ? kTextMalformedInputErr : 0;
    CFIndex n = CFStringGetLength(s);
    OSStatus status = 0;
    if ((unsigned long)n * sizeof(UniChar) > outputBufLen) { n = (CFIndex)(outputBufLen / sizeof(UniChar)); status = kTECOutputBufferFullStatus; take = 0; }
    CFStringGetCharacters(s, CFRangeMake(0, n), unicode);
    CFRelease(s);
    if (unicodeLen) *unicodeLen = (unsigned long)n * sizeof(UniChar);
    if (sourceRead) *sourceRead = take;
    return status;
}
