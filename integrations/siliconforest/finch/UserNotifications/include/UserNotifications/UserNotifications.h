/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class UNNotificationContent, UNMutableNotificationContent, UNNotificationSound, UNNotificationAttachment, UNNotificationTrigger, UNPushNotificationTrigger, UNTimeIntervalNotificationTrigger, UNCalendarNotificationTrigger, UNNotificationRequest, UNNotification, UNNotificationActionIcon, UNNotificationAction, UNTextInputNotificationAction, UNNotificationCategory, UNNotificationResponse, UNTextInputNotificationResponse, UNNotificationSettings, UNNotificationServiceExtension, UNUserNotificationCenter;
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

extern NSString *const UNErrorDomain;
extern NSString *const UNLogDeveloperAPIDomainDescriptionBadging;
extern NSString *const UNLogDeveloperAPIDomainDescriptionCommunication;
extern NSString *const UNLogDeveloperAPIDomainDescriptionFirstPartyWarning;
extern NSString *const UNNotificationActionTextInputButtonTitleKey;
extern NSString *const UNNotificationActionTextInputPlaceholderKey;
extern NSString *const UNNotificationAttachmentOptionsHiddenFromDefaultExpandedViewKey;
extern NSString *const UNNotificationAttachmentOptionsThumbnailClippingRectKey;
extern NSString *const UNNotificationAttachmentOptionsThumbnailHiddenKey;
extern NSString *const UNNotificationAttachmentOptionsThumbnailTimeKey;
extern NSString *const UNNotificationAttachmentOptionsTypeHintKey;
extern NSString *const UNNotificationAttachmentUserInfoDataKey;
extern NSString *const UNNotificationAttachmentUserInfoIdentityImageStyleKey;
extern NSString *const UNNotificationAttachmentUserInfoPurposeKey;
extern NSString *const UNNotificationAttachmentUserInfoRelocationUUIDKey;
extern NSString *const UNNotificationDefaultActionIdentifier;
extern NSString *const UNNotificationDismissActionIdentifier;
extern NSString *const UNNotificationFollowActivityActionIdentifier;
extern NSString *const UNNotificationIconCalendarKey;
extern NSString *const UNNotificationIconDateComponentsKey;
extern NSString *const UNNotificationIconDateFormatKey;
extern NSString *const UNNotificationSandboxExtensionClassGenericReadOnly;
extern NSString *const UNNotificationSandboxExtensionClassGenericReadWrite;
extern NSString *const UNNotificationSilenceActionIdentifier;
extern NSString *const UNOneTimeCodeMachServiceName;
extern NSString *const UNUserNotificationDaemonServiceName;
extern NSString *const UNUserNotificationServiceName;
extern NSString *const _UNNotificationContentTypeAirDrop;
extern NSString *const _UNNotificationContentTypeCallOther;
extern NSString *const _UNNotificationContentTypeDefault;
extern NSString *const _UNNotificationContentTypeIncomingCall;
extern NSString *const _UNNotificationContentTypeIntercom;
extern NSString *const _UNNotificationContentTypeMessagingDirect;
extern NSString *const _UNNotificationContentTypeMessagingGroup;
extern NSString *const _UNNotificationContentTypeMissedCall;
extern NSString *const _UNNotificationContentTypeVoicemail;
extern NSString *const kUNResponseContextArchivedUserNotificationKey;

@interface UNNotificationContent : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)init;
- (NSArray *)attachments;
- (NSNumber *)badge;
- (NSString *)body;
- (NSString *)categoryIdentifier;
- (NSString *)launchImageName;
- (UNNotificationSound *)sound;
- (NSString *)subtitle;
- (NSString *)threadIdentifier;
- (NSString *)title;
- (NSDictionary *)userInfo;
- (NSString *)summaryArgument;
- (NSUInteger)summaryArgumentCount;
- (NSString *)targetContentIdentifier;
- (UNNotificationInterruptionLevel)interruptionLevel;
- (double)relevanceScore;
- (NSString *)filterCriteria;
- (void)_finchCopyInto:(UNNotificationContent *)c;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;
- (UNNotificationContent *)contentByUpdatingWithProvider:(id<UNNotificationContentProviding>)provider error:(NSError **)outError;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (NSString *)description;
@end

@interface UNMutableNotificationContent : UNNotificationContent
- (void)setAttachments:(NSArray *)v;
- (void)setBadge:(NSNumber *)v;
- (void)setBody:(NSString *)v;
- (void)setCategoryIdentifier:(NSString *)v;
- (void)setLaunchImageName:(NSString *)v;
- (void)setSound:(UNNotificationSound *)v;
- (void)setSubtitle:(NSString *)v;
- (void)setThreadIdentifier:(NSString *)v;
- (void)setTitle:(NSString *)v;
- (void)setUserInfo:(NSDictionary *)v;
- (void)setSummaryArgument:(NSString *)v;
- (void)setSummaryArgumentCount:(NSUInteger)v;
- (void)setTargetContentIdentifier:(NSString *)v;
- (void)setInterruptionLevel:(UNNotificationInterruptionLevel)v;
- (void)setRelevanceScore:(double)v;
- (void)setFilterCriteria:(NSString *)v;
@end

@interface UNNotificationSound : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)_finchSound:(NSString *)name critical:(BOOL)critical volume:(float)volume;
+ (UNNotificationSound *)defaultSound;
+ (UNNotificationSound *)defaultRingtoneSound;
+ (UNNotificationSound *)defaultCriticalSound;
+ (UNNotificationSound *)defaultCriticalSoundWithAudioVolume:(float)volume;
+ (UNNotificationSound *)soundNamed:(UNNotificationSoundName)name;
+ (UNNotificationSound *)ringtoneSoundNamed:(UNNotificationSoundName)name;
+ (UNNotificationSound *)criticalSoundNamed:(UNNotificationSoundName)name;
+ (UNNotificationSound *)criticalSoundNamed:(UNNotificationSoundName)name withAudioVolume:(float)volume;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (NSString *)description;
@end

@interface UNNotificationAttachment : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)attachmentWithIdentifier:(NSString *)identifier URL:(NSURL *)URL options:(NSDictionary *)options error:(NSError **)error;
- (NSString *)identifier;
- (NSURL *)URL;
- (NSString *)type;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface UNNotificationTrigger : NSObject
+ (BOOL)supportsSecureCoding;
- (BOOL)repeats;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (NSDate *)_finchNextDateAfter:(NSDate *)date;
@end

@interface UNPushNotificationTrigger : UNNotificationTrigger

@end

@interface UNTimeIntervalNotificationTrigger : UNNotificationTrigger
+ (instancetype)triggerWithTimeInterval:(NSTimeInterval)timeInterval repeats:(BOOL)repeats;
- (NSTimeInterval)timeInterval;
- (NSDate *)nextTriggerDate;
- (NSDate *)_finchNextDateAfter:(NSDate *)now;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface UNCalendarNotificationTrigger : UNNotificationTrigger
+ (instancetype)triggerWithDateMatchingComponents:(NSDateComponents *)dateComponents repeats:(BOOL)repeats;
- (NSDateComponents *)dateComponents;
- (NSDate *)nextTriggerDate;
- (NSDate *)_finchNextDateAfter:(NSDate *)now;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface UNNotificationRequest : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)requestWithIdentifier:(NSString *)identifier content:(UNNotificationContent *)content trigger:(UNNotificationTrigger *)trigger;
- (NSString *)identifier;
- (UNNotificationContent *)content;
- (UNNotificationTrigger *)trigger;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (NSString *)description;
@end

@interface UNNotification : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)_finchNotificationWithRequest:(UNNotificationRequest *)request date:(NSDate *)date;
- (UNNotificationRequest *)request;
- (NSDate *)date;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface UNNotificationActionIcon : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)iconWithTemplateImageName:(NSString *)templateImageName;
+ (instancetype)iconWithSystemImageName:(NSString *)systemImageName;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface UNNotificationAction : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)actionWithIdentifier:(NSString *)identifier title:(NSString *)title options:(UNNotificationActionOptions)options;
+ (instancetype)actionWithIdentifier:(NSString *)identifier title:(NSString *)title options:(UNNotificationActionOptions)options icon:(UNNotificationActionIcon *)icon;
- (NSString *)identifier;
- (NSString *)title;
- (UNNotificationActionOptions)options;
- (UNNotificationActionIcon *)icon;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface UNTextInputNotificationAction : UNNotificationAction
+ (instancetype)actionWithIdentifier:(NSString *)identifier title:(NSString *)title options:(UNNotificationActionOptions)options textInputButtonTitle:(NSString *)textInputButtonTitle textInputPlaceholder:(NSString *)textInputPlaceholder;
+ (instancetype)actionWithIdentifier:(NSString *)identifier title:(NSString *)title options:(UNNotificationActionOptions)options icon:(UNNotificationActionIcon *)icon textInputButtonTitle:(NSString *)textInputButtonTitle textInputPlaceholder:(NSString *)textInputPlaceholder;
- (NSString *)textInputButtonTitle;
- (NSString *)textInputPlaceholder;
@end

@interface UNNotificationCategory : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)categoryWithIdentifier:(NSString *)identifier actions:(NSArray *)actions intentIdentifiers:(NSArray *)intentIdentifiers options:(UNNotificationCategoryOptions)options;
+ (instancetype)categoryWithIdentifier:(NSString *)identifier actions:(NSArray *)actions intentIdentifiers:(NSArray *)intentIdentifiers hiddenPreviewsBodyPlaceholder:(NSString *)hiddenPreviewsBodyPlaceholder options:(UNNotificationCategoryOptions)options;
+ (instancetype)categoryWithIdentifier:(NSString *)identifier actions:(NSArray *)actions intentIdentifiers:(NSArray *)intentIdentifiers hiddenPreviewsBodyPlaceholder:(NSString *)hiddenPreviewsBodyPlaceholder categorySummaryFormat:(NSString *)categorySummaryFormat options:(UNNotificationCategoryOptions)options;
- (NSString *)identifier;
- (NSArray *)actions;
- (NSArray *)intentIdentifiers;
- (UNNotificationCategoryOptions)options;
- (NSString *)hiddenPreviewsBodyPlaceholder;
- (NSString *)categorySummaryFormat;
- (id)copyWithZone:(NSZone *)zone;
- (NSUInteger)hash;
- (BOOL)isEqual:(id)other;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface UNNotificationResponse : NSObject
+ (BOOL)supportsSecureCoding;
- (UNNotification *)notification;
- (NSString *)actionIdentifier;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface UNTextInputNotificationResponse : UNNotificationResponse
- (NSString *)userText;
@end

@interface UNNotificationSettings : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)_finchSettingsWithStatus:(UNAuthorizationStatus)status;
- (UNAuthorizationStatus)authorizationStatus;
- (UNNotificationSetting)_on;
- (UNNotificationSetting)soundSetting;
- (UNNotificationSetting)badgeSetting;
- (UNNotificationSetting)alertSetting;
- (UNNotificationSetting)notificationCenterSetting;
- (UNNotificationSetting)lockScreenSetting;
- (UNNotificationSetting)carPlaySetting;
- (UNAlertStyle)alertStyle;
- (UNShowPreviewsSetting)showPreviewsSetting;
- (UNNotificationSetting)criticalAlertSetting;
- (BOOL)providesAppNotificationSettings;
- (UNNotificationSetting)announcementSetting;
- (UNNotificationSetting)timeSensitiveSetting;
- (UNNotificationSetting)scheduledDeliverySetting;
- (UNNotificationSetting)directMessagesSetting;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface UNNotificationServiceExtension : NSObject
- (void)didReceiveNotificationRequest:(UNNotificationRequest *)request withContentHandler:(void (^)(UNNotificationContent *))contentHandler;
- (void)serviceExtensionTimeWillExpire;
@end

@interface UNUserNotificationCenter : NSObject
+ (UNUserNotificationCenter *)currentNotificationCenter;
- (id<UNUserNotificationCenterDelegate>)delegate;
- (void)setDelegate:(id<UNUserNotificationCenterDelegate>)delegate;
- (BOOL)supportsContentExtensions;
- (void)requestAuthorizationWithOptions:(UNAuthorizationOptions)options completionHandler:(void (^)(BOOL, NSError *))completionHandler;
- (void)getNotificationSettingsWithCompletionHandler:(void (^)(UNNotificationSettings *))completionHandler;
- (void)setNotificationCategories:(NSSet *)categories;
- (void)getNotificationCategoriesWithCompletionHandler:(void (^)(NSSet<UNNotificationCategory *> *))completionHandler;
- (void)_finchDeliver:(UNNotificationRequest *)request;
- (void)_finchSchedule:(UNNotificationRequest *)request;
- (void)addNotificationRequest:(UNNotificationRequest *)request withCompletionHandler:(void (^)(NSError *))completionHandler;
- (void)getPendingNotificationRequestsWithCompletionHandler:(void (^)(NSArray<UNNotificationRequest *> *))completionHandler;
- (void)removePendingNotificationRequestsWithIdentifiers:(NSArray *)identifiers;
- (void)removeAllPendingNotificationRequests;
- (void)getDeliveredNotificationsWithCompletionHandler:(void (^)(NSArray<UNNotification *> *))completionHandler;
- (void)removeDeliveredNotificationsWithIdentifiers:(NSArray *)identifiers;
- (void)removeAllDeliveredNotifications;
- (void)setBadgeCount:(NSInteger)newBadgeCount withCompletionHandler:(void (^)(NSError *))completionHandler;
@end
