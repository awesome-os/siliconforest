/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class CLFloor, CLLocationSourceInformation, CLLocation, CLHeading, CLVisit, CLRegion, CLCircularRegion, CLBeaconRegion, CLPlacemark, CLGeocoder, CLLocationManager;
/* SPDX-License-Identifier: MIT */
#include <math.h>
#include <stdint.h>
typedef double CLHeadingComponentValue;
typedef double CLLocationDegrees, CLLocationDistance, CLLocationAccuracy;
typedef double CLLocationDirection, CLLocationDirectionAccuracy, CLLocationSpeed, CLLocationSpeedAccuracy;
typedef struct { CLLocationDegrees latitude, longitude; } CLLocationCoordinate2D;
typedef int CLAuthorizationStatus;
typedef NSInteger CLAccuracyAuthorization, CLActivityType, CLDeviceOrientation;
@class CLBeaconIdentityConstraint;
typedef void (^CLGeocodeCompletionHandler)(NSArray<CLPlacemark *> *, NSError *);
enum { kCLAuthorizationStatusNotDetermined = 0, kCLAuthorizationStatusDenied = 2,
       CLAccuracyAuthorizationReducedAccuracy = 1, CLActivityTypeOther = 1,
       CLDeviceOrientationPortrait = 1, kCLErrorDenied = 1, kCLErrorNetwork = 2,
       kCLErrorRegionMonitoringDenied = 4 };
@protocol CLLocationManagerDelegate <NSObject>
@optional
- (void)locationManagerDidChangeAuthorization:(CLLocationManager *)manager;
- (void)locationManager:(CLLocationManager *)manager didChangeAuthorizationStatus:(CLAuthorizationStatus)status;
- (void)locationManager:(CLLocationManager *)manager didFailWithError:(NSError *)error;
- (void)locationManager:(CLLocationManager *)manager monitoringDidFailForRegion:(CLRegion *)region withError:(NSError *)error;
@end
BOOL CLLocationCoordinate2DIsValid(CLLocationCoordinate2D coordinate);
CLLocationCoordinate2D CLLocationCoordinate2DMake(CLLocationDegrees latitude, CLLocationDegrees longitude);
extern const CLLocationCoordinate2D kCLLocationCoordinate2DInvalid;
@interface NSCoder (SFFinchCoreLocationDeclarations)
- (id)decodeObjectOfClass:(Class)cls forKey:(NSString *)key;
@end

extern NSString *const CLAppResetChangedNotification;
extern NSString *const CLAppStatusChangedNotification;
extern NSString *const CLAuthStatusChangedNotification;
extern NSString *const CLAuthorizationStatusChangedNotification;
extern NSString *const CLBeaconRegionAnyIdentifier;
extern NSString *const CLDaemonShutdownNotification;
extern NSString *const CLDaemonStartedNotification;
extern NSString *const CLDurianNBConfigDictionaryKey;
extern NSString *const CLDurianNBRangingAcqDitherMaxKey;
extern NSString *const CLDurianNBRangingAcqRetryIntervalKey;
extern NSString *const CLDurianNBRangingAcqTimeoutKey;
extern NSString *const CLDurianNBRangingDebugFlagsKey;
extern NSString *const CLDurianNBRangingIntervalKey;
extern NSString *const CLDurianNBRangingMMSFragCountryCodeKey;
extern NSString *const CLDurianNBRangingMMSGapKey;
extern NSString *const CLDurianNBRangingMMSPSRKey;
extern NSString *const CLDurianNBRangingMMSSequenceIdxKey;
extern NSString *const CLDurianNBRangingMMSSlotSizeKey;
extern NSString *const CLDurianNBRangingNBChannelSelectMaskKey;
extern NSString *const CLDurianNBRangingNBCountryCodesKey;
extern NSString *const CLDurianNBRangingNBMaskNapChannelKey;
extern NSString *const CLDurianNBRangingRangingTimeoutKey;
extern NSString *const CLDurianNBRangingSarCountryCodesKey;
extern NSString *const CLDurianNBRangingVersionKey;
extern NSString *const CLDurianRangingIRKKey;
extern NSString *const CLGnssBandsChangedNotification;
extern NSString *const CLLocationPushServiceErrorDomain;
extern NSString *const CLShouldDisplayStatusBarIconChangedNotification;
extern NSString *const CLTechStatusChangedNotification;
extern NSString *const UTDegradationReasonSignificantLocationsOffKey;
extern NSString *const UTDegradationReasonWifiOffKey;
extern NSString *const UTDisableReasonAirplaneModeOnKey;
extern NSString *const UTDisableReasonBTOffKey;
extern NSString *const UTDisableReasonFindMyDeletedKey;
extern NSString *const UTDisableReasonLocationServicesOffKey;
extern NSString *const UTDisableReasonTrackingNotificationsDisallowedKey;
extern NSString *const UTEnablementStatusKey;
extern NSString *const kCLCommonBackgroundLocationCapability;
extern NSString *const kCLCommonLocationAlwaysAndWhenInUseUsageDescriptionKey;
extern NSString *const kCLCommonLocationAlwaysUsageDescriptionKey;
extern NSString *const kCLCommonLocationAuthorizationPurposeDescriptionDictionaryKey;
extern NSString *const kCLCommonLocationDefaultAccuracyReducedKey;
extern NSString *const kCLCommonLocationExplicitServiceSessionKey;
extern NSString *const kCLCommonLocationInterestZonesKey;
extern NSString *const kCLCommonLocationRestricted;
extern NSString *const kCLCommonLocationTemporaryUsageDescriptionDictionaryKey;
extern NSString *const kCLCommonLocationUsageDescriptionKey;
extern NSString *const kCLCommonLocationWhenInUseUsageDescriptionKey;
extern NSString *const kCLCommonLocationWildernessSafetyUsageDescriptionDictionaryKey;
extern NSString *const kCLCommonNSWidgetWantsLocationKey;
extern NSString *const kCLCommonTranscriptSessionStartDateKey;
extern NSString *const kCLCommonWKRunsIndependentlyOfCompanionAppKey;
extern NSString *const kCLCommonWKWatchOnlyKey;
extern NSString *const kCLErrorDomain;
extern NSString *const kCLErrorDomainPrivate;
extern NSString *const kCLErrorUserInfoAlternateRegionKey;
extern const double kCLHeadingFilterNone;
extern NSString *const kEEDPartnerIDKey;
extern NSString *const kFeedbackCallReasonKey;
extern NSString *const kFeedbackCallRelatedToEmergencyKey;
extern NSString *const kFeedbackCallTriggerKey;
extern NSString *const kFeedbackCallerResponsiveKey;
extern NSString *const kFeedbackRespondersDispatchedForCallKey;
extern NSString *const kFeedbackVersionKey;
extern NSString *const kRequestTypeFeedbackCollection;
extern NSString *const kRequestTypeMediaUpload;
extern NSString *const kRequestTypeVideoStreaming;
extern const double kCLLocationAccuracyBest;
extern const double kCLLocationAccuracyHundredMeters;
extern const double kCLLocationAccuracyBestForNavigation;
extern const double kCLLocationAccuracyNearestTenMeters;
extern const double kCLLocationAccuracyKilometer;
extern const double kCLLocationAccuracyThreeKilometers;
extern const double kCLLocationAccuracyReduced;
extern const double kCLDistanceFilterNone;
extern const double CLLocationDistanceMax;
extern const double CLTimeIntervalMax;

@interface CLFloor : NSObject
+ (BOOL)supportsSecureCoding;
- (NSInteger)level;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CLLocationSourceInformation : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithSoftwareSimulationState:(BOOL)isSoftware andExternalAccessoryState:(BOOL)wasProducedByAccessory;
- (BOOL)isSimulatedBySoftware;
- (BOOL)isProducedByAccessory;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CLLocation : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithLatitude:(CLLocationDegrees)latitude longitude:(CLLocationDegrees)longitude;
- (instancetype)initWithCoordinate:(CLLocationCoordinate2D)coordinate altitude:(CLLocationDistance)altitude horizontalAccuracy:(CLLocationAccuracy)hAccuracy verticalAccuracy:(CLLocationAccuracy)vAccuracy timestamp:(NSDate *)timestamp;
- (instancetype)initWithCoordinate:(CLLocationCoordinate2D)coordinate altitude:(CLLocationDistance)altitude horizontalAccuracy:(CLLocationAccuracy)hAccuracy verticalAccuracy:(CLLocationAccuracy)vAccuracy course:(CLLocationDirection)course speed:(CLLocationSpeed)speed timestamp:(NSDate *)timestamp;
- (instancetype)initWithCoordinate:(CLLocationCoordinate2D)coordinate altitude:(CLLocationDistance)altitude horizontalAccuracy:(CLLocationAccuracy)hAccuracy verticalAccuracy:(CLLocationAccuracy)vAccuracy course:(CLLocationDirection)course courseAccuracy:(CLLocationDirectionAccuracy)courseAccuracy speed:(CLLocationSpeed)speed speedAccuracy:(CLLocationSpeedAccuracy)speedAccuracy timestamp:(NSDate *)timestamp;
- (instancetype)initWithCoordinate:(CLLocationCoordinate2D)coordinate altitude:(CLLocationDistance)altitude horizontalAccuracy:(CLLocationAccuracy)hAccuracy verticalAccuracy:(CLLocationAccuracy)vAccuracy course:(CLLocationDirection)course courseAccuracy:(CLLocationDirectionAccuracy)courseAccuracy speed:(CLLocationSpeed)speed speedAccuracy:(CLLocationSpeedAccuracy)speedAccuracy timestamp:(NSDate *)timestamp sourceInfo:(CLLocationSourceInformation *)sourceInfo;
- (CLLocationCoordinate2D)coordinate;
- (CLLocationDistance)altitude;
- (CLLocationDistance)ellipsoidalAltitude;
- (CLLocationAccuracy)horizontalAccuracy;
- (CLLocationAccuracy)verticalAccuracy;
- (CLLocationDirection)course;
- (CLLocationDirectionAccuracy)courseAccuracy;
- (CLLocationSpeed)speed;
- (CLLocationSpeedAccuracy)speedAccuracy;
- (NSDate *)timestamp;
- (CLFloor *)floor;
- (CLLocationSourceInformation *)sourceInformation;
- (CLLocationDistance)distanceFromLocation:(const CLLocation *)location;
- (CLLocationDistance)getDistanceFrom:(const CLLocation *)location;
- (NSString *)description;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CLHeading : NSObject
+ (BOOL)supportsSecureCoding;
- (CLLocationDirection)magneticHeading;
- (CLLocationDirection)trueHeading;
- (CLLocationDirection)headingAccuracy;
- (CLHeadingComponentValue)x;
- (CLHeadingComponentValue)y;
- (CLHeadingComponentValue)z;
- (NSDate *)timestamp;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CLVisit : NSObject
+ (BOOL)supportsSecureCoding;
- (NSDate *)arrivalDate;
- (NSDate *)departureDate;
- (CLLocationCoordinate2D)coordinate;
- (CLLocationAccuracy)horizontalAccuracy;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CLRegion : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initCircularRegionWithCenter:(CLLocationCoordinate2D)center radius:(CLLocationDistance)radius identifier:(NSString *)identifier;
- (CLLocationCoordinate2D)center;
- (CLLocationDistance)radius;
- (NSString *)identifier;
- (BOOL)notifyOnEntry;
- (void)setNotifyOnEntry:(BOOL)v;
- (BOOL)notifyOnExit;
- (void)setNotifyOnExit:(BOOL)v;
- (BOOL)containsCoordinate:(CLLocationCoordinate2D)coordinate;
- (NSUInteger)hash;
- (BOOL)isEqual:(id)other;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (NSString *)description;
@end

@interface CLCircularRegion : CLRegion
- (instancetype)initWithCenter:(CLLocationCoordinate2D)center radius:(CLLocationDistance)radius identifier:(NSString *)identifier;
@end

@interface CLBeaconRegion : CLRegion

@end

@interface CLPlacemark : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithPlacemark:(CLPlacemark *)placemark;
- (CLLocation *)location;
- (CLRegion *)region;
- (NSTimeZone *)timeZone;
- (NSDictionary *)addressDictionary;
- (NSString *)name;
- (NSString *)thoroughfare;
- (NSString *)subThoroughfare;
- (NSString *)locality;
- (NSString *)subLocality;
- (NSString *)administrativeArea;
- (NSString *)subAdministrativeArea;
- (NSString *)postalCode;
- (NSString *)ISOcountryCode;
- (NSString *)country;
- (NSString *)inlandWater;
- (NSString *)ocean;
- (NSArray<NSString *> *)areasOfInterest;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CLGeocoder : NSObject
- (BOOL)isGeocoding;
- (void)_finchFail:(CLGeocodeCompletionHandler)handler;
- (void)reverseGeocodeLocation:(CLLocation *)location completionHandler:(CLGeocodeCompletionHandler)completionHandler;
- (void)reverseGeocodeLocation:(CLLocation *)location preferredLocale:(NSLocale *)locale completionHandler:(CLGeocodeCompletionHandler)completionHandler;
- (void)geocodeAddressDictionary:(NSDictionary *)addressDictionary completionHandler:(CLGeocodeCompletionHandler)completionHandler;
- (void)geocodeAddressString:(NSString *)addressString completionHandler:(CLGeocodeCompletionHandler)completionHandler;
- (void)geocodeAddressString:(NSString *)addressString inRegion:(CLRegion *)region completionHandler:(CLGeocodeCompletionHandler)completionHandler;
- (void)geocodeAddressString:(NSString *)addressString inRegion:(CLRegion *)region preferredLocale:(NSLocale *)locale completionHandler:(CLGeocodeCompletionHandler)completionHandler;
- (void)geocodePostalAddress:(id)postalAddress completionHandler:(CLGeocodeCompletionHandler)completionHandler;
- (void)geocodePostalAddress:(id)postalAddress preferredLocale:(NSLocale *)locale completionHandler:(CLGeocodeCompletionHandler)completionHandler;
- (void)cancelGeocode;
@end

@interface CLLocationManager : NSObject
+ (BOOL)locationServicesEnabled;
+ (BOOL)headingAvailable;
+ (BOOL)significantLocationChangeMonitoringAvailable;
+ (BOOL)isMonitoringAvailableForClass:(Class)regionClass;
+ (BOOL)regionMonitoringAvailable;
+ (BOOL)regionMonitoringEnabled;
+ (BOOL)isRangingAvailable;
+ (BOOL)deferredLocationUpdatesAvailable;
+ (CLAuthorizationStatus)authorizationStatus;
- (instancetype)init;
- (CLAuthorizationStatus)authorizationStatus;
- (CLAccuracyAuthorization)accuracyAuthorization;
- (BOOL)isAuthorizedForWidgetUpdates;
- (id<CLLocationManagerDelegate>)delegate;
- (void)setDelegate:(id<CLLocationManagerDelegate>)delegate;
- (void)_finchTellAuthorization;
- (void)_finchFail;
- (void)requestWhenInUseAuthorization;
- (void)requestAlwaysAuthorization;
- (void)requestTemporaryFullAccuracyAuthorizationWithPurposeKey:(NSString *)purposeKey completion:(void (^)(NSError *))completion;
- (void)requestTemporaryFullAccuracyAuthorizationWithPurposeKey:(NSString *)purposeKey;
- (void)startUpdatingLocation;
- (void)stopUpdatingLocation;
- (void)requestLocation;
- (void)startUpdatingHeading;
- (void)stopUpdatingHeading;
- (void)dismissHeadingCalibrationDisplay;
- (void)startMonitoringSignificantLocationChanges;
- (void)stopMonitoringSignificantLocationChanges;
- (void)startMonitoringVisits;
- (void)stopMonitoringVisits;
- (void)startMonitoringForRegion:(CLRegion *)region;
- (void)startMonitoringForRegion:(CLRegion *)region desiredAccuracy:(CLLocationAccuracy)accuracy;
- (void)stopMonitoringForRegion:(CLRegion *)region;
- (void)requestStateForRegion:(CLRegion *)region;
- (void)startRangingBeaconsInRegion:(CLBeaconRegion *)region;
- (void)stopRangingBeaconsInRegion:(CLBeaconRegion *)region;
- (void)startRangingBeaconsSatisfyingConstraint:(CLBeaconIdentityConstraint *)constraint;
- (void)stopRangingBeaconsSatisfyingConstraint:(CLBeaconIdentityConstraint *)constraint;
- (void)allowDeferredLocationUpdatesUntilTraveled:(CLLocationDistance)distance timeout:(NSTimeInterval)timeout;
- (void)disallowDeferredLocationUpdates;
- (BOOL)locationServicesEnabled;
- (BOOL)headingAvailable;
- (NSString *)purpose;
- (void)setPurpose:(NSString *)purpose;
- (CLActivityType)activityType;
- (void)setActivityType:(CLActivityType)t;
- (CLLocationDistance)distanceFilter;
- (void)setDistanceFilter:(CLLocationDistance)d;
- (CLLocationAccuracy)desiredAccuracy;
- (void)setDesiredAccuracy:(CLLocationAccuracy)a;
- (BOOL)pausesLocationUpdatesAutomatically;
- (void)setPausesLocationUpdatesAutomatically:(BOOL)v;
- (BOOL)allowsBackgroundLocationUpdates;
- (void)setAllowsBackgroundLocationUpdates:(BOOL)v;
- (CLLocation *)location;
- (CLLocationDegrees)headingFilter;
- (void)setHeadingFilter:(CLLocationDegrees)f;
- (CLDeviceOrientation)headingOrientation;
- (void)setHeadingOrientation:(CLDeviceOrientation)o;
- (CLHeading *)heading;
- (CLLocationDistance)maximumRegionMonitoringDistance;
- (NSSet *)monitoredRegions;
- (NSSet *)rangedRegions;
- (NSSet *)rangedBeaconConstraints;
@end
