/* SPDX-License-Identifier: MIT
 * Private diagnostic only. No installed provider or owner launcher changes.
 * AppKit enumeration alone did not change Gecko's selected fonts. Test the
 * CoreText family-list and family-descriptor path independently instead.
 */
#import <Foundation/Foundation.h>
#include <CoreText/CoreText.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>

static CFArrayRef (*realFamilies)(void);
static CTFontDescriptorRef (*realDescriptor)(CFDictionaryRef);
static pthread_once_t once=PTHREAD_ONCE_INIT;
static const char *aliases[]={"Georgia","Times","Times New Roman","serif","DejaVu Serif","DejaVuSerif"};
static void resolve(void) {
    realFamilies=dlsym(RTLD_NEXT,"CTFontManagerCopyAvailableFontFamilyNames");
    realDescriptor=dlsym(RTLD_NEXT,"CTFontDescriptorCreateWithAttributes");
}
static BOOL hasFamily(CFArrayRef families, NSString *name) {
    for(NSString *family in (NSArray *)families)
        if([family caseInsensitiveCompare:name]==NSOrderedSame)return YES;
    return NO;
}
static CFArrayRef copyFamilies(void) {
    pthread_once(&once,resolve);
    if(!realFamilies)return NULL;
    CFArrayRef physical=realFamilies();
    if(!physical || !hasFamily(physical,@"Nimbus Roman"))return physical;
    CFMutableArrayRef result=CFArrayCreateMutableCopy(NULL,0,physical);
    if(result)for(unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);i++){
        NSString *name=[NSString stringWithUTF8String:aliases[i]];
        if(!hasFamily(result,name))CFArrayAppendValue(result,(CFStringRef)name);
    }
    CFRelease(physical);
    return result;
}
static CTFontDescriptorRef createDescriptor(CFDictionaryRef attributes) {
    pthread_once(&once,resolve);
    if(!realDescriptor)return NULL;
    CFStringRef family=attributes?CFDictionaryGetValue(attributes,kCTFontFamilyNameAttribute):NULL;
    if(!family || CFGetTypeID(family)!=CFStringGetTypeID() ||
       CFDictionaryContainsKey(attributes,kCTFontNameAttribute) || !realFamilies)
        return realDescriptor(attributes);
    BOOL alias=NO;
    for(unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);i++)
        if([(NSString *)family caseInsensitiveCompare:[NSString stringWithUTF8String:aliases[i]]]==NSOrderedSame)alias=YES;
    if(!alias)return realDescriptor(attributes);
    CFArrayRef physical=realFamilies();
    BOOL replace=physical && !hasFamily(physical,(NSString *)family) && hasFamily(physical,@"Nimbus Roman");
    if(physical)CFRelease(physical);
    if(!replace)return realDescriptor(attributes);
    CFMutableDictionaryRef copy=CFDictionaryCreateMutableCopy(NULL,0,attributes);
    if(!copy)return realDescriptor(attributes);
    CFDictionarySetValue(copy,kCTFontFamilyNameAttribute,CFSTR("Nimbus Roman"));
    CTFontDescriptorRef result=realDescriptor(copy);
    CFRelease(copy);
    return result;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct{const void *replacement,*original;} interpose[]={
    {copyFamilies,CTFontManagerCopyAvailableFontFamilyNames},
    {createDescriptor,CTFontDescriptorCreateWithAttributes},
};
