/* SPDX-License-Identifier: MIT */
#include "VTInternal.h"
#define SF_DECL(prefix) \
 OSStatus prefix##VTSessionSetProperty(VTSessionRef,CFStringRef,CFTypeRef); \
 OSStatus prefix##VTSessionCopyProperty(VTSessionRef,CFStringRef,CFAllocatorRef,void *); \
 OSStatus prefix##VTSessionCopySupportedPropertyDictionary(VTSessionRef,CFDictionaryRef *);
SF_DECL(SFDecoder)
SF_DECL(SFFinch)
OSStatus VTSessionSetProperty(VTSessionRef s,CFStringRef k,CFTypeRef v) {
 return _VTGetSession(s)?SFFinchVTSessionSetProperty(s,k,v):SFDecoderVTSessionSetProperty(s,k,v);
}
OSStatus VTSessionCopyProperty(VTSessionRef s,CFStringRef k,CFAllocatorRef a,void *out) {
 return _VTGetSession(s)?SFFinchVTSessionCopyProperty(s,k,a,out):SFDecoderVTSessionCopyProperty(s,k,a,out);
}
OSStatus VTSessionCopySupportedPropertyDictionary(VTSessionRef s,CFDictionaryRef *out) {
 return _VTGetSession(s)?SFFinchVTSessionCopySupportedPropertyDictionary(s,out):SFDecoderVTSessionCopySupportedPropertyDictionary(s,out);
}
