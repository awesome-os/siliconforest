/* SPDX-License-Identifier: MIT */
#import <CoreGraphics/CoreGraphics.h>
#import <dlfcn.h>
#include <math.h>
static inline CFStringRef SFCICopyColorSpaceName(CGColorSpaceRef space) {
    typedef CFStringRef (*CopyName)(CGColorSpaceRef);
    CopyName copy=(CopyName)dlsym(RTLD_DEFAULT,"CGColorSpaceCopyName");
    if (copy) return copy(space);
    CFStringRef name=CGColorSpaceGetName(space);
    return name ? CFRetain(name) : NULL;
}
static inline CGColorRef SFCICopyColorMatched(CGColorSpaceRef space,CGColorRenderingIntent intent,CGColorRef color,CFDictionaryRef options) {
    typedef CGColorRef (*Match)(CGColorSpaceRef,CGColorRenderingIntent,CGColorRef,CFDictionaryRef);
    Match match=(Match)dlsym(RTLD_DEFAULT,"CGColorCreateCopyByMatchingToColorSpace");
    if (match) return match(space,intent,color,options);
    /* The retained CG provider has no general matcher. Its unprofiled gray
     * space is also used by CGColorCreateGenericGray. Adopt Generic Gray as
     * the default device-gray profile, and implement its neutral tone curve
     * analytically. Do not reinterpret an ICC-tagged space or invent a CMYK
     * conversion. This narrow path is independent of ICC quantization. */
    CGColorSpaceRef source=CGColorGetColorSpace(color);
    if (!source || CGColorSpaceGetModel(source)!=kCGColorSpaceModelMonochrome ||
        CGColorGetNumberOfComponents(color)!=2 || !space ||
        CGColorSpaceGetModel(space)!=kCGColorSpaceModelRGB) return NULL;
    typedef CFDataRef (*CopyICC)(CGColorSpaceRef);
    CopyICC copyICC=(CopyICC)dlsym(RTLD_DEFAULT,"CGColorSpaceCopyICCData");
    CFDataRef icc=copyICC?copyICC(source):NULL;
    if (icc) { CFRelease(icc); return NULL; }
    CFStringRef from=SFCICopyColorSpaceName(source),to=SFCICopyColorSpaceName(space);
    double gamma=0; bool linear=false;
    if (!from || CFEqual(from,kCGColorSpaceGenericGray)) gamma=1.8;
    else if (CFEqual(from,kCGColorSpaceGenericGrayGamma2_2) || CFEqual(from,kCGColorSpaceExtendedGray)) gamma=2.2;
    else if (CFEqual(from,kCGColorSpaceLinearGray) || CFEqual(from,kCGColorSpaceExtendedLinearGray)) gamma=1;
    if (to && (CFEqual(to,kCGColorSpaceLinearSRGB) || CFEqual(to,kCGColorSpaceExtendedLinearSRGB))) linear=true;
    else if (!to || (!CFEqual(to,kCGColorSpaceSRGB) && !CFEqual(to,kCGColorSpaceExtendedSRGB))) gamma=0;
    if (from)CFRelease(from);if(to)CFRelease(to);
    const CGFloat *components=CGColorGetComponents(color);
    if (!gamma || !isfinite(components[0]) || components[0]<0 || components[0]>1) return NULL;
    double value=pow(components[0],gamma);
    if(!linear)value=value<=0.0031308?12.92*value:1.055*pow(value,1.0/2.4)-0.055;
    CGFloat output[4]={value,value,value,components[1]};
    return CGColorCreate(space,output);
}

static inline BOOL SFCIIsGenericXYZ(CFStringRef name) {
    const CFStringRef *value=(const CFStringRef *)dlsym(RTLD_DEFAULT,"kCGColorSpaceGenericXYZ");
    return value && *value && CFEqual(name,*value);
}
static inline BOOL SFCIIsColorSpace(CFTypeRef value) {
    typedef CFTypeID (*TypeID)(void);
    TypeID typeID=(TypeID)dlsym(RTLD_DEFAULT,"CGColorSpaceGetTypeID");
    if (typeID) return value && CFGetTypeID(value)==typeID();
    /* PD's public CG wrapper returns real O2ColorSpace NSObject instances. */
    Class provider=NSClassFromString(@"O2ColorSpace");
    return value && provider && [(id)value isKindOfClass:provider];
}
