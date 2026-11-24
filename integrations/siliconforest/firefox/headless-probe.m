/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#include <sandbox.h>
#include <stdio.h>
#include <sys/resource.h>
#include <time.h>
// SiliconForest's public AppKit ABI; no platform SDK headers are needed.
@interface NSApplication : NSObject
+(id)sharedApplication;
-(NSArray *)windows;
-(id)nextEventMatchingMask:(NSUInteger)mask untilDate:(NSDate *)date inMode:(NSString *)mode dequeue:(BOOL)dequeue;
@end
static int fired;
@interface FXTimerTarget : NSObject
-(void)fire:(NSTimer *)timer;
@end
@implementation FXTimerTarget
-(void)fire:(NSTimer *)timer { fired++; }
@end
static double cpu(void) {
    struct rusage u;getrusage(RUSAGE_SELF,&u);
    return u.ru_utime.tv_sec+u.ru_utime.tv_usec*1e-6+u.ru_stime.tv_sec+u.ru_stime.tv_usec*1e-6;
}
int main(void) { @autoreleasepool {
    char *error=NULL;
    /* Read resources normally, refuse all networking including the Wayland socket. */
    int status=sandbox_init("(version 1)(allow default)(deny network*)",0,&error);
    printf("FXHEADLESS applied=%d error=%s\n",status,error?error:"none");sandbox_free_error(error);
    if(status)return 2;
    NSApplication *app=[NSApplication sharedApplication];
    FXTimerTarget *target=[FXTimerTarget new];
    [NSTimer scheduledTimerWithTimeInterval:.05 target:target selector:@selector(fire:) userInfo:nil repeats:NO];
    double before=cpu();struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
    [app nextEventMatchingMask:(NSUInteger)-1 untilDate:[NSDate dateWithTimeIntervalSinceNow:.25] inMode:NSDefaultRunLoopMode dequeue:YES];
    clock_gettime(CLOCK_MONOTONIC,&b);double consumed=cpu()-before;
    printf("FXHEADLESS timers=%d wall=%.6f cpu=%.6f windows=%lu\n",fired,(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)*1e-9,consumed,(unsigned long)[[app windows] count]);
    [target release];return fired==1 && [[app windows] count]==0 ? 0:1;
} }
