/* SPDX-License-Identifier: MIT
 * The retained provider honestly exposes no capture devices. Its missing
 * authorization API must report that platform restriction, never authorize a
 * nonexistent camera/microphone or pretend to display a permission prompt.
 * Declarations are our own public ABI declarations, not Apple SDK material.
 */
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>
@interface AVCaptureDevice : NSObject
@end
@implementation AVCaptureDevice(SFFXUnsupportedCaptureAuthorization)
+(NSInteger)authorizationStatusForMediaType:(NSString *)type {
    (void)type;
    return 1; /* AVAuthorizationStatusRestricted */
}
+(void)requestAccessForMediaType:(NSString *)type completionHandler:(void (^)(BOOL))handler {
    (void)type;
    if(handler)dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0),^{handler(NO);});
}
@end
