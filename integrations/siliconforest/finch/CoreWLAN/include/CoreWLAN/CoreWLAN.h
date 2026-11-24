/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class CWWiFiClient, CWInterface, CWNetwork, CWChannel, CWConfiguration, CWMutableConfiguration, CWNetworkProfile, CWMutableNetworkProfile;
/* SPDX-License-Identifier: MIT */
#import <Security/SecBase.h>
#import <Security/SecIdentity.h>
typedef NSInteger CWEventType;
typedef NSInteger CWSecurity;
typedef NSInteger CWPHYMode;
typedef NSInteger CWChannelWidth;
typedef NSInteger CWChannelBand;
typedef NSInteger CWKeychainDomain;
enum { kCWChannelWidthUnknown = 0, kCWChannelBandUnknown = 0, kCWNotSupportedErr = -3903 };

/* Public selector absent from PD's header; runtime remains subject to conformance checks. */
@interface NSCoder (SFFinchCoreWLANDeclarations)
- (id)decodeObjectOfClass:(Class)cls forKey:(NSString *)key;
@end

extern NSString *const Apple80211APIErrorDomain;
extern NSString *const CWBSSIDDidChangeNotification;
extern NSString *const CWBaseStationPPPCallTypeKey;
extern NSString *const CWBaseStationPPPConnectionSpeedKey;
extern NSString *const CWBaseStationPPPConnectionStatusKey;
extern NSString *const CWBaseStationPPPConnectionTimeKey;
extern NSString *const CWBaseStationPPPHardwareTypeKey;
extern NSString *const CWBaseStationPPPPhoneNumberKey;
extern NSString *const CWBaseStationPPPProtocolKey;
extern NSString *const CWBaseStationPPPRemainingTimeKey;
extern NSString *const CWBaseStationPPPStatusDidChangeNotification;
extern NSString *const CWCountryCodeDidChangeNotification;
extern NSString *const CWDHCPStatusDidChangeNotification;
extern NSString *const CWDeviceDidAttachNotification;
extern NSString *const CWDeviceDidDetachNotification;
extern NSString *const CWDynamicConfigurationDidChangeNotification;
extern NSString *const CWErrorDomain;
extern NSString *const CWIEEE80211StatusCodeDomain;
extern NSString *const CWIPStatusDidChangeNotification;
extern NSString *const CWLinkDidChangeNotification;
extern NSString *const CWLinkQualityDidChangeNotification;
extern NSString *const CWLinkQualityNotificationRSSIKey;
extern NSString *const CWLinkQualityNotificationTransmitRateKey;
extern NSString *const CWModeDidChangeNotification;
extern NSString *const CWNetworkReachabilityDidChangeNotification;
extern NSString *const CWNetworkReachabilityNotificationFlagsKey;
extern NSString *const CWPowerDidChangeNotification;
extern NSString *const CWRSNHandshakeCompleteNotification;
extern NSString *const CWSSIDDidChangeNotification;
extern NSString *const CWScanCacheDidUpdateNotification;
extern NSString *const CWScanDoneNotification;
extern NSString *const CWServiceDidChangeNotification;
extern NSString *const CoreWLANFrameworkBundleID;
extern NSString *const EAPOLErrorDomain;
extern NSString *const EAPOLSupplicantStateDidChangeNotification;
extern NSString *const WiFiErrorDomain;
extern NSString *const kCWCountryCodeDidChangeNotification;
extern NSString *const kCWLinkDidChangeNotification;
extern NSString *const kCWModeDidChangeNotification;
extern NSString *const kCWPowerDidChangeNotification;
extern NSString *const kCWSSIDDidChangeNotification;
extern NSString *const kWiFiAutoJoinPowerDiagnosticsKey;
extern NSString *const kWiFiNetworkIsAutoJoinedKey;

@interface CWWiFiClient : NSObject
+ (CWWiFiClient *)sharedWiFiClient;
- (id)delegate;
- (void)setDelegate:(id)delegate;
- (CWInterface *)interface;
- (NSArray<NSString *> *)interfaceNames;
+ (NSArray<NSString *> *)interfaceNames;
- (CWInterface *)interfaceWithName:(NSString *)interfaceName;
- (NSArray<CWInterface *> *)interfaces;
- (BOOL)startMonitoringEventWithType:(CWEventType)type error:(NSError **)error;
- (BOOL)stopMonitoringEventWithType:(CWEventType)type error:(NSError **)error;
- (BOOL)stopMonitoringAllEventsAndReturnError:(NSError **)error;
@end

@interface CWInterface : NSObject
+ (NSArray<NSString *> *)interfaceNames;
+ (instancetype)interface;
+ (instancetype)interfaceWithName:(NSString *)name;
- (NSString *)interfaceName;
- (BOOL)powerOn;
- (BOOL)serviceActive;
- (NSString *)ssid;
- (NSData *)ssidData;
- (NSString *)bssid;
- (NSInteger)rssiValue;
- (NSInteger)noiseMeasurement;
- (double)transmitRate;
- (NSInteger)transmitPower;
- (NSString *)hardwareAddress;
- (NSString *)countryCode;
- (CWChannel *)wlanChannel;
- (CWConfiguration *)configuration;
- (NSSet<CWNetwork *> *)scanForNetworksWithName:(NSString *)networkName error:(NSError **)error;
- (NSSet<CWNetwork *> *)scanForNetworksWithSSID:(NSData *)ssid error:(NSError **)error;
- (NSSet<CWNetwork *> *)cachedScanResults;
- (BOOL)setPower:(BOOL)power error:(NSError **)error;
- (BOOL)associateToNetwork:(CWNetwork *)network password:(NSString *)password error:(NSError **)error;
- (void)disassociate;
@end

@interface CWNetwork : NSObject
+ (BOOL)supportsSecureCoding;
- (NSString *)ssid;
- (NSData *)ssidData;
- (NSString *)bssid;
- (CWChannel *)wlanChannel;
- (NSInteger)rssiValue;
- (NSInteger)noiseMeasurement;
- (NSData *)informationElementData;
- (NSString *)countryCode;
- (NSInteger)beaconInterval;
- (BOOL)ibss;
- (BOOL)supportsSecurity:(CWSecurity)security;
- (BOOL)supportsPHYMode:(CWPHYMode)phyMode;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (BOOL)isEqualToNetwork:(CWNetwork *)network;
@end

@interface CWChannel : NSObject
+ (BOOL)supportsSecureCoding;
- (NSInteger)channelNumber;
- (CWChannelWidth)channelWidth;
- (CWChannelBand)channelBand;
- (BOOL)isEqualToChannel:(CWChannel *)channel;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CWConfiguration : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)configuration;
+ (instancetype)configurationWithConfiguration:(CWConfiguration *)configuration;
- (instancetype)initWithConfiguration:(CWConfiguration *)configuration;
- (NSOrderedSet<CWNetworkProfile *> *)networkProfiles;
- (BOOL)requireAdministratorForAssociation;
- (BOOL)requireAdministratorForPower;
- (BOOL)requireAdministratorForIBSSMode;
- (BOOL)rememberJoinedNetworks;
- (BOOL)isEqualToConfiguration:(CWConfiguration *)configuration;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CWMutableConfiguration : CWConfiguration
- (void)setNetworkProfiles:(NSOrderedSet<CWNetworkProfile *> *)networkProfiles;
- (void)setRequireAdministratorForAssociation:(BOOL)v;
- (void)setRequireAdministratorForPower:(BOOL)v;
- (void)setRequireAdministratorForIBSSMode:(BOOL)v;
- (void)setRememberJoinedNetworks:(BOOL)v;
@end

@interface CWNetworkProfile : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)networkProfile;
+ (instancetype)networkProfileWithNetworkProfile:(CWNetworkProfile *)networkProfile;
- (instancetype)initWithNetworkProfile:(CWNetworkProfile *)networkProfile;
- (NSString *)ssid;
- (NSData *)ssidData;
- (CWSecurity)security;
- (BOOL)isEqualToNetworkProfile:(CWNetworkProfile *)networkProfile;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CWMutableNetworkProfile : CWNetworkProfile
- (void)setSsidData:(NSData *)ssidData;
- (void)setSecurity:(CWSecurity)security;
@end
