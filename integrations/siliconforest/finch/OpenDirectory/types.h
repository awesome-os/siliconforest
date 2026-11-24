/* SPDX-License-Identifier: MIT */
#import <CFOpenDirectory/CFOpenDirectory.h>
@protocol ODQueryDelegate <NSObject>
- (void)query:(ODQuery *)query foundResults:(NSArray *)results error:(NSError *)error;
@end
extern NSString *const ODFrameworkErrorDomain;
extern NSString *const ODSessionProxyAddress;
extern NSString *const ODSessionProxyPassword;
extern NSString *const ODSessionProxyPort;
extern NSString *const ODSessionProxyUsername;
extern NSString *const ODTrustTypeAnonymous;
extern NSString *const ODTrustTypeJoined;
extern NSString *const ODTrustTypeUsingCredentials;
