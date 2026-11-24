/* SPDX-License-Identifier: MIT */
#import <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
@class CBPeripheral, CBCentral, CBATTRequest;
@protocol CBCentralManagerDelegate <NSObject>
- (void)centralManagerDidUpdateState:(id)manager;
@end
@protocol CBPeripheralManagerDelegate <NSObject>
- (void)peripheralManagerDidUpdateState:(id)manager;
@end
typedef NSUInteger CBCharacteristicProperties;
typedef NSUInteger CBAttributePermissions;
typedef NSUInteger CBCentralManagerFeature;
typedef NSInteger CBATTError;
typedef NSInteger CBManagerState;
typedef NSInteger CBManagerAuthorization;
typedef NSInteger CBPeripheralManagerAuthorizationStatus;
typedef NSInteger CBPeripheralManagerConnectionLatency;
typedef uint16_t CBL2CAPPSM;
enum { CBManagerStateUnsupported = 2, CBManagerAuthorizationNotDetermined = 0,
       CBPeripheralManagerAuthorizationStatusAuthorized = 3, CBErrorOperationNotSupported = 13 };

enum { CBCharacteristicPropertyRead = 1UL << 1, CBCharacteristicPropertyNotify = 1UL << 4,
       CBAttributePermissionsReadable = 1UL << 0 };
