/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class CBUUID, CBAttribute, CBService, CBMutableService, CBCharacteristic, CBMutableCharacteristic, CBDescriptor, CBMutableDescriptor, CBManager, CBCentralManager, CBPeripheralManager, CBL2CAPChannel;
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

extern NSString * const CBUUIDCharacteristicExtendedPropertiesString;
extern NSString * const CBUUIDCharacteristicUserDescriptionString;
extern NSString * const CBUUIDClientCharacteristicConfigurationString;
extern NSString * const CBUUIDServerCharacteristicConfigurationString;
extern NSString * const CBUUIDCharacteristicFormatString;
extern NSString * const CBUUIDCharacteristicAggregateFormatString;
extern NSString * const CBUUIDCharacteristicValidRangeString;
extern NSString * const CBUUIDCharacteristicObservationScheduleString;
extern NSString * const CBUUIDL2CAPPSMCharacteristicString;
extern NSString * const CBErrorDomain;
extern NSString * const CBATTErrorDomain;
extern NSString * const CBAdvertisementDataIsConnectable;
extern NSString * const CBAdvertisementDataLocalNameKey;
extern NSString * const CBAdvertisementDataManufacturerDataKey;
extern NSString * const CBAdvertisementDataOverflowServiceUUIDsKey;
extern NSString * const CBAdvertisementDataServiceDataKey;
extern NSString * const CBAdvertisementDataServiceUUIDsKey;
extern NSString * const CBAdvertisementDataSolicitedServiceUUIDsKey;
extern NSString * const CBAdvertisementDataTxPowerLevelKey;
extern NSString * const CBCentralManagerOptionRestoreIdentifierKey;
extern NSString * const CBCentralManagerOptionShowPowerAlertKey;
extern NSString * const CBCentralManagerRestoredStatePeripheralsKey;
extern NSString * const CBCentralManagerRestoredStateScanOptionsKey;
extern NSString * const CBCentralManagerRestoredStateScanServicesKey;
extern NSString * const CBCentralManagerScanOptionAllowDuplicatesKey;
extern NSString * const CBCentralManagerScanOptionSolicitedServiceUUIDsKey;
extern NSString * const CBConnectPeripheralOptionEnableAutoReconnect;
extern NSString * const CBConnectPeripheralOptionNotifyOnConnectionKey;
extern NSString * const CBConnectPeripheralOptionNotifyOnDisconnectionKey;
extern NSString * const CBConnectPeripheralOptionNotifyOnNotificationKey;
extern NSString * const CBConnectPeripheralOptionStartDelayKey;
extern NSString * const CBPeripheralManagerOptionRestoreIdentifierKey;
extern NSString * const CBPeripheralManagerOptionShowPowerAlertKey;
extern NSString * const CBPeripheralManagerRestoredStateAdvertisementDataKey;
extern NSString * const CBPeripheralManagerRestoredStateServicesKey;

@interface CBUUID : NSObject
- (instancetype)_initWithData:(NSData *)data;
+ (CBUUID *)UUIDWithString:(NSString *)string;
+ (CBUUID *)UUIDWithData:(NSData *)data;
+ (CBUUID *)UUIDWithCFUUID:(CFUUIDRef)UUID;
+ (CBUUID *)UUIDWithNSUUID:(NSUUID *)UUID;
- (NSData *)data;
- (NSString *)UUIDString;
- (BOOL)isEqual:(id)object;
- (NSUInteger)hash;
- (id)copyWithZone:(NSZone *)zone;
- (NSString *)description;
@end

@interface CBAttribute : NSObject
- (instancetype)_initWithUUID:(CBUUID *)UUID;
- (CBUUID *)UUID;
@end

@interface CBService : CBAttribute
- (CBPeripheral *)peripheral;
- (BOOL)isPrimary;
- (NSArray *)includedServices;
- (NSArray *)characteristics;
@end

@interface CBMutableService : CBService
- (instancetype)initWithType:(CBUUID *)UUID primary:(BOOL)isPrimary;
- (void)setIncludedServices:(NSArray *)services;
- (void)setCharacteristics:(NSArray *)characteristics;
@end

@interface CBCharacteristic : CBAttribute
- (CBService *)service;
- (CBCharacteristicProperties)properties;
- (NSData *)value;
- (NSArray *)descriptors;
- (BOOL)isBroadcasted;
- (BOOL)isNotifying;
- (NSArray *)subscribedCentrals;
@end

@interface CBMutableCharacteristic : CBCharacteristic
- (instancetype)initWithType:(CBUUID *)UUID properties:(CBCharacteristicProperties)properties value:(NSData *)value permissions:(CBAttributePermissions)permissions;
- (CBAttributePermissions)permissions;
- (void)setPermissions:(CBAttributePermissions)value;
- (void)setProperties:(CBCharacteristicProperties)value;
- (void)setValue:(NSData *)value;
- (void)setDescriptors:(NSArray *)descriptors;
@end

@interface CBDescriptor : CBAttribute
- (CBCharacteristic *)characteristic;
- (id)value;
@end

@interface CBMutableDescriptor : CBDescriptor
- (instancetype)initWithType:(CBUUID *)UUID value:(id)value;
@end

@interface CBManager : NSObject
- (CBManagerState)state;
+ (CBManagerAuthorization)authorization;
@end

@interface CBCentralManager : CBManager
- (instancetype)initWithDelegate:(id<CBCentralManagerDelegate>)delegate queue:(dispatch_queue_t)queue;
- (instancetype)initWithDelegate:(id<CBCentralManagerDelegate>)delegate queue:(dispatch_queue_t)queue options:(NSDictionary *)options;
- (id<CBCentralManagerDelegate>)delegate;
- (void)setDelegate:(id<CBCentralManagerDelegate>)delegate;
- (BOOL)isScanning;
+ (BOOL)supportsFeatures:(CBCentralManagerFeature)features;
- (NSArray *)retrievePeripheralsWithIdentifiers:(NSArray *)identifiers;
- (NSArray *)retrieveConnectedPeripheralsWithServices:(NSArray *)services;
- (void)scanForPeripheralsWithServices:(NSArray *)serviceUUIDs options:(NSDictionary *)options;
- (void)stopScan;
- (void)connectPeripheral:(CBPeripheral *)peripheral options:(NSDictionary *)options;
- (void)cancelPeripheralConnection:(CBPeripheral *)peripheral;
- (void)registerForConnectionEventsWithOptions:(NSDictionary *)options;
@end

@interface CBPeripheralManager : CBManager
- (instancetype)initWithDelegate:(id<CBPeripheralManagerDelegate>)delegate queue:(dispatch_queue_t)queue;
- (instancetype)initWithDelegate:(id<CBPeripheralManagerDelegate>)delegate queue:(dispatch_queue_t)queue options:(NSDictionary *)options;
- (id<CBPeripheralManagerDelegate>)delegate;
- (void)setDelegate:(id<CBPeripheralManagerDelegate>)delegate;
- (BOOL)isAdvertising;
+ (CBPeripheralManagerAuthorizationStatus)authorizationStatus;
- (void)startAdvertising:(NSDictionary *)advertisementData;
- (void)stopAdvertising;
- (void)setDesiredConnectionLatency:(CBPeripheralManagerConnectionLatency)latency forCentral:(CBCentral *)central;
- (void)addService:(CBMutableService *)service;
- (void)removeService:(CBMutableService *)service;
- (void)removeAllServices;
- (void)respondToRequest:(CBATTRequest *)request withResult:(CBATTError)result;
- (BOOL)updateValue:(NSData *)value forCharacteristic:(CBMutableCharacteristic *)characteristic onSubscribedCentrals:(NSArray *)centrals;
- (void)publishL2CAPChannelWithEncryption:(BOOL)encryptionRequired;
- (void)unpublishL2CAPChannel:(CBL2CAPPSM)PSM;
@end

@interface CBL2CAPChannel : NSObject
- (NSInputStream *)inputStream;
- (NSOutputStream *)outputStream;
- (CBPeripheral *)peer;
- (CBL2CAPPSM)PSM;
@end
