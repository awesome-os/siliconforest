/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class LAContext, LAEnvironment;
/* SPDX-License-Identifier: MIT */
#import <Security/SecAccessControl.h>
@class LADomainState, LAEnvironmentState;
@protocol LAEnvironmentObserver <NSObject>
@end
typedef NS_ENUM(NSInteger, LAPolicy) {
    LAPolicyDeviceOwnerAuthenticationWithBiometrics = 1,
    LAPolicyDeviceOwnerAuthentication = 2,
    LAPolicyDeviceOwnerAuthenticationWithWatch = 3,
    LAPolicyDeviceOwnerAuthenticationWithBiometricsOrWatch = 4
};
typedef NSInteger LAAccessControlOperation;
typedef NSInteger LACredentialType;
typedef NS_ENUM(NSInteger, LABiometryType) {
    LABiometryTypeNone = 0, LABiometryTypeTouchID = 1, LABiometryTypeFaceID = 2
};
enum { LAErrorBiometryNotAvailable = -6, LAErrorAppCancel = -9, LAErrorNotInteractive = -1004 };

extern NSString *const LAAvailableMechanisms;
extern NSString *const LABiometryDatabaseHash;
extern NSString *const LAEnabledBiometryUsers;
extern NSString *const LAErrorDomain;
extern NSString *const LAErrorInfoBiometryType;
extern NSString *const LAErrorInfoTCCServerPrompt;
extern NSString *const LAErrorInfoTCCService;
extern NSString *const LAErrorNonceKey;
extern NSString *const LAErrorSubcodeKey;
extern NSString *const LAMechanismTree;
extern NSString *const LAPasswordRetryTime;
extern NSString *const LAStorageErrorDomain;
extern const double LATouchIDAuthenticationMaximumAllowableReuseDuration;
extern NSString *const LAUserErrorDomain;
extern NSString *const kAPKekPassword;
extern NSString *const kAPMasterKey;

@interface LAContext : NSObject
- (BOOL)canEvaluatePolicy:(LAPolicy)policy error:(NSError **)error;
- (void)evaluatePolicy:(LAPolicy)policy localizedReason:(NSString *)localizedReason reply:(void (^)(BOOL, NSError *))reply;
- (void)evaluateAccessControl:(SecAccessControlRef)accessControl operation:(LAAccessControlOperation)operation localizedReason:(NSString *)localizedReason reply:(void (^)(BOOL, NSError *))reply;
- (void)invalidate;
- (BOOL)setCredential:(NSData *)credential type:(LACredentialType)type;
- (BOOL)isCredentialSet:(LACredentialType)type;
- (NSString *)localizedFallbackTitle;
- (void)setLocalizedFallbackTitle:(NSString *)t;
- (NSString *)localizedCancelTitle;
- (void)setLocalizedCancelTitle:(NSString *)t;
- (NSString *)localizedReason;
- (void)setLocalizedReason:(NSString *)r;
- (NSNumber *)maxBiometryFailures;
- (void)setMaxBiometryFailures:(NSNumber *)n;
- (NSTimeInterval)touchIDAuthenticationAllowableReuseDuration;
- (void)setTouchIDAuthenticationAllowableReuseDuration:(NSTimeInterval)d;
- (BOOL)interactionNotAllowed;
- (void)setInteractionNotAllowed:(BOOL)v;
- (LABiometryType)biometryType;
- (NSData *)evaluatedPolicyDomainState;
- (LADomainState *)domainState;
@end

@interface LAEnvironment : NSObject
+ (LAEnvironment *)currentUser;
- (instancetype)_finchInit __attribute__((objc_method_family(init)));
- (void)addObserver:(id<LAEnvironmentObserver>)observer;
- (void)removeObserver:(id<LAEnvironmentObserver>)observer;
- (LAEnvironmentState *)state;
@end
