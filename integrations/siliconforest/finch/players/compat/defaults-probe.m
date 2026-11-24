/* SPDX-License-Identifier: MIT */
/* M1-only process-cache test; never run against macOS cfprefsd. */
#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
@interface NSUserDefaults(SFSuiteProbe)
- (void)addSuiteNamed:(NSString*)name;
- (void)removeSuiteNamed:(NSString*)name;
@end
static void need(BOOL good,const char *what){if(!good){fprintf(stderr,"DEFAULTS_FAIL %s\n",what);exit(1);}}
int main(void){@autoreleasepool{
 need(getenv("SF_SCHEME_PROBE_PD_ONLY")&&access("/dev/sfavd",F_OK)==0,"M1-only guard");
 need(dlopen("/System/Library/Frameworks/Foundation.framework/Foundation",RTLD_NOW|RTLD_GLOBAL)!=NULL,"private facade");
 NSString *appID=[NSString stringWithFormat:@"org.siliconforest.defaults.consumer.%d",getpid()];
 NSString *suiteID=[NSString stringWithFormat:@"org.siliconforest.defaults.suite.%d",getpid()];
 NSUserDefaults *consumer=[[NSUserDefaults alloc] initWithSuiteName:appID];
 NSUserDefaults *suite=[[NSUserDefaults alloc] initWithSuiteName:suiteID];NSString *key=@"SFFinchOwnSuiteValue";
 need([consumer objectForKey:key]==nil,"initial isolated domain");[suite setObject:@"suite" forKey:key];
 [consumer addSuiteNamed:suiteID];need([[consumer objectForKey:key] isEqual:@"suite"],"suite fallback");
 [consumer setObject:@"application" forKey:key];need([[consumer objectForKey:key] isEqual:@"application"],"application priority");
 [consumer removeObjectForKey:key];need([[consumer objectForKey:key] isEqual:@"suite"],"suite visible again");
 [consumer removeSuiteNamed:suiteID];need([consumer objectForKey:key]==nil,"suite removed");
 [suite removeObjectForKey:key];[consumer release];[suite release];
 puts("DEFAULTS suite-fallback=1 app-priority=1 removal=1 scope=named-instance no-synchronize");return 0;
}}
