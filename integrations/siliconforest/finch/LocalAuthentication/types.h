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
