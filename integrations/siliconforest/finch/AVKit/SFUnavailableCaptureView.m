/* SPDX-License-Identifier: MIT */
/* Native metadata establishes NSView inheritance and these initializer ABIs.
 * There is no capture UI/backend here; fail creation rather than exposing a
 * view that appears to record or preview a nonexistent device/session. */
#import <AppKit/NSView.h>
#include <pthread.h>
#include <stdio.h>
static pthread_once_t reportOnce=PTHREAD_ONCE_INIT;
static void reportUnavailable(void) {
    fputs("AVKit: capture view unavailable; capture preview/recording pipeline is not implemented\n",stderr);
}
@interface AVCaptureView : NSView @end
@implementation AVCaptureView
- (id)init {
    pthread_once(&reportOnce,reportUnavailable);[self release];return nil;
}
- (id)initWithFrame:(NSRect)frame {
    (void)frame;pthread_once(&reportOnce,reportUnavailable);[self release];return nil;
}
- (id)initWithCoder:(NSCoder *)coder {
    (void)coder;pthread_once(&reportOnce,reportUnavailable);[self release];return nil;
}
@end
