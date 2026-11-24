/* SPDX-License-Identifier: MIT */
/* M1-only: LSRegisterURL changes only PD's process-local scan cache. Do not
 * run this program on macOS, where registration persists in the user's lsd. */
#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
static void need(BOOL value,const char *what){if(!value){fprintf(stderr,"SCHEME_FAIL %s\n",what);exit(1);}}
int main(void){@autoreleasepool{
 struct utsname system;need(uname(&system)==0,"uname");
 /* The runner supplies this guard only on the reviewed SiliconForest stage.
  * Also require the retained CoreServices child's own in-process API. */
 need(getenv("SF_SCHEME_PROBE_PD_ONLY")!=NULL&&access("/dev/sfavd",F_OK)==0,"M1-only explicit guard");
 void *image=dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices",RTLD_NOW);
 need(image!=NULL,"provider");
 CFArrayRef (*all)(CFStringRef)=dlsym(image,"LSCopyAllHandlersForURLScheme");
 int32_t (*add)(CFURLRef,Boolean)=dlsym(image,"LSRegisterURL");
 void (*refresh)(void)=dlsym(image,"LSRefreshApplicationRegistry");
 need(all&&add&&refresh,"PD registry functions");
 char temporary[]="sf-scheme.XXXXXX";need(mkdtemp(temporary)!=NULL,"own directory");
 NSString *base=[[[NSFileManager defaultManager] currentDirectoryPath] stringByAppendingPathComponent:[NSString stringWithUTF8String:temporary]];
 NSArray *identifiers=@[@"org.siliconforest.scheme.first",@"org.siliconforest.scheme.second",@"org.siliconforest.scheme.first"];
 for(unsigned i=0;i<3;i++){
  NSString *app=[base stringByAppendingPathComponent:[NSString stringWithFormat:@"App%u.app",i]];
  NSString *contents=[app stringByAppendingPathComponent:@"Contents"];
  need([[NSFileManager defaultManager] createDirectoryAtPath:contents withIntermediateDirectories:YES attributes:nil error:NULL],"bundle directory");
  NSDictionary *plist=@{@"CFBundleIdentifier":[identifiers objectAtIndex:i],@"CFBundlePackageType":@"APPL",
   @"CFBundleURLTypes":@[@{@"CFBundleURLSchemes":@[@"sffinch-own-scheme",@"SFFINCH-OWN-SCHEME"]}]};
  need([plist writeToFile:[contents stringByAppendingPathComponent:@"Info.plist"] atomically:YES],"own plist");
  need(add((CFURLRef)[NSURL fileURLWithPath:app isDirectory:YES],false)==0,"register process-local bundle");
 }
 CFArrayRef registered=all(CFSTR("SFFINCH-Own-Scheme"));
 need(registered&&CFArrayGetCount(registered)==2,"case-insensitive matching and duplicate identifiers");
 need(CFArrayContainsValue(registered,CFRangeMake(0,2),CFSTR("org.siliconforest.scheme.first"))&&
      CFArrayContainsValue(registered,CFRangeMake(0,2),CFSTR("org.siliconforest.scheme.second")),"both actual bundle identifiers");
 CFRelease(registered);
 CFArrayRef unknown=all(CFSTR("sffinch-nonexistent-6f91d8e7"));need(unknown&&CFArrayGetCount(unknown)==0,"unknown scheme empty");CFRelease(unknown);
 need(all(NULL)==NULL&&all(CFSTR(""))==NULL&&all((CFStringRef)kCFBooleanTrue)==NULL,"invalid input rejected");
 refresh();
 need([[NSFileManager defaultManager] removeItemAtPath:base error:NULL],"remove own bundles");
 puts("SCHEME actual-registered=2 duplicate-ids=deduplicated matching=case-insensitive unknown=empty invalid=rejected");return 0;
}}
