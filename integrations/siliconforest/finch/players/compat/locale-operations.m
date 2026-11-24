/* SPDX-License-Identifier: MIT */
/* Locale direction uses the existing ICU-backed CFLocale implementation. */
#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>
@interface NSLocale(SFDirectionDeclarations)
+ (NSUInteger)sfCharacterDirectionForLanguage:(NSString *)language;
+ (NSUInteger)sfLineDirectionForLanguage:(NSString *)language;
@end
@implementation NSLocale(SFDirections)
+ (void)load {
 const char *names[]={"characterDirectionForLanguage:","lineDirectionForLanguage:"};
 const char *sources[]={"sfCharacterDirectionForLanguage:","sfLineDirectionForLanguage:"};
 for(unsigned i=0;i<2;i++){
  SEL selector=sel_registerName(names[i]);
  if(!class_getClassMethod(self,selector)){
   Method method=class_getClassMethod(self,sel_registerName(sources[i]));
   class_addMethod(object_getClass(self),selector,method_getImplementation(method),method_getTypeEncoding(method));
  }
 }
}
+ (NSUInteger)sfCharacterDirectionForLanguage:(NSString *)language {
 return [language isKindOfClass:[NSString class]]?(NSUInteger)CFLocaleGetLanguageCharacterDirection((CFStringRef)language):0;
}
+ (NSUInteger)sfLineDirectionForLanguage:(NSString *)language {
 return [language isKindOfClass:[NSString class]]?(NSUInteger)CFLocaleGetLanguageLineDirection((CFStringRef)language):0;
}
@end
