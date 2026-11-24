/* SPDX-License-Identifier: MIT */
/* Private-prefix bridge to PD's actual CFPreferences suite search list. Core
 * integration belongs in NSUserDefaults.m with direct _applicationID access.
 * Never replace a method already provided by Foundation. */
#import <Foundation/Foundation.h>
#include <CoreFoundation/CFPreferences.h>
#include <objc/runtime.h>
#include <string.h>
static CFStringRef application(NSUserDefaults *defaults){
 Ivar field=class_getInstanceVariable([NSUserDefaults class],"_applicationID");
 ptrdiff_t offset=field?ivar_getOffset(field):-1;CFStringRef identifier=NULL;
 if(offset>=(ptrdiff_t)sizeof(void*)&&(size_t)offset+sizeof(identifier)<=class_getInstanceSize([defaults class]))
  memcpy(&identifier,(const char*)defaults+offset,sizeof(identifier));
 if(!identifier)[NSException raise:NSInternalInconsistencyException format:@"Unavailable NSUserDefaults application domain"];
 return identifier;
}
static void add(NSUserDefaults *self,SEL command,NSString *suite){
 (void)command;
 if(![suite isKindOfClass:[NSString class]]||![suite length])
  [NSException raise:NSInvalidArgumentException format:@"A preferences suite needs a nonempty name"];
 CFPreferencesAddSuitePreferencesToApp(application(self),(CFStringRef)suite);
}
static void removeSuite(NSUserDefaults *self,SEL command,NSString *suite){
 (void)command;
 if(![suite isKindOfClass:[NSString class]]||![suite length])
  [NSException raise:NSInvalidArgumentException format:@"A preferences suite needs a nonempty name"];
 CFPreferencesRemoveSuitePreferencesFromApp(application(self),(CFStringRef)suite);
}
@interface SFDefaultsSuiteMethods:NSObject @end
@implementation SFDefaultsSuiteMethods
+ (void)load{
 Class target=[NSUserDefaults class];
 class_addMethod(target,sel_registerName("addSuiteNamed:"),(IMP)add,"v@:@");
 class_addMethod(target,sel_registerName("removeSuiteNamed:"),(IMP)removeSuite,"v@:@");
}
@end
