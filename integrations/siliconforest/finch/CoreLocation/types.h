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
