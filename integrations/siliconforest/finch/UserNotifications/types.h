/* SPDX-License-Identifier: MIT */
#include <math.h>
typedef NSUInteger UNAuthorizationOptions, UNNotificationActionOptions, UNNotificationCategoryOptions;
typedef NSUInteger UNNotificationPresentationOptions;
typedef NSInteger UNAuthorizationStatus, UNNotificationSetting, UNAlertStyle, UNShowPreviewsSetting;
typedef NSInteger UNNotificationInterruptionLevel;
typedef NSString *UNNotificationSoundName;
enum { UNAuthorizationStatusNotDetermined = 0, UNAuthorizationStatusDenied = 1,
       UNAuthorizationStatusAuthorized = 2, UNAuthorizationStatusProvisional = 3,
       UNAuthorizationOptionProvisional = 1UL << 6,
       UNNotificationSettingNotSupported = 0, UNNotificationSettingDisabled = 1,
       UNNotificationSettingEnabled = 2, UNAlertStyleNone = 0, UNAlertStyleBanner = 1,
       UNShowPreviewsSettingAlways = 0, UNNotificationInterruptionLevelActive = 1,
       UNErrorCodeNotificationsNotAllowed = 1, UNErrorCodeAttachmentInvalidURL = 100,
       UNErrorCodeNotificationInvalidNoContent = 1401 };
@protocol UNNotificationContentProviding <NSObject>
@end
@protocol UNUserNotificationCenterDelegate <NSObject>
@optional
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
      willPresentNotification:(UNNotification *)notification
        withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completion;
@end
@interface NSCoder (SFFinchUserNotificationsDeclarations)
- (id)decodeObjectOfClass:(Class)cls forKey:(NSString *)key;
- (id)decodeObjectOfClasses:(NSSet *)classes forKey:(NSString *)key;
@end
@interface NSCalendar (SFFinchUserNotificationsDeclarations)
- (NSDate *)nextDateAfterDate:(NSDate *)date matchingComponents:(NSDateComponents *)components options:(NSUInteger)options;
@end

enum { NSCalendarMatchNextTime = 1UL << 10 };
@interface NSURL (SFFinchUserNotificationsDeclarations)
- (BOOL)getResourceValue:(id *)value forKey:(NSString *)key error:(NSError **)error;
@end
@interface NSDateComponents (SFFinchUserNotificationsDeclarations)
- (NSCalendar *)calendar;
@end
@interface NSScanner (SFFinchUserNotificationsDeclarations)
@property(readonly, getter=isAtEnd) BOOL atEnd;
@end
