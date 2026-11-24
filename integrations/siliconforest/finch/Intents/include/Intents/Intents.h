/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
@class INIntentResponse, INBooleanResolutionResult, INSendMessageAttachment, INRelevantShortcut, INVoiceShortcut;
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class INPerson, INPersonHandle, INObject, INAirline, INSpeakableString, INFocusStatus, INIntentResolutionResult, INShortcut, INInteraction, INImage, INFile, INPaymentMethod, INFocusStatusCenter, INIntent, INPreferences, INVoiceShortcutCenter, INRelevantShortcutStore, INAnswerCallIntent, INAnswerCallIntentResponse;
/* SPDX-License-Identifier: MIT */
/* Own model declarations for classes named in Finch Intents.m and its tests.
 * Numeric ABI facts checked against objc2-intents 0.3.2. */
typedef NSInteger INPersonHandleType, INCallAudioRoute, INAnswerCallIntentResponseCode;
typedef NSInteger INFocusStatusAuthorizationStatus, INSiriAuthorizationStatus;
typedef NSInteger INIntentHandlingStatus, INInteractionDirection, INIntentErrorCode, INPaymentMethodType;
enum { INCallAudioRouteUnknown = 0, INAnswerCallIntentResponseCodeReady = 1,
       INIntentHandlingStatusReady = 1, INFocusStatusAuthorizationStatusNotDetermined = 0,
       INSiriAuthorizationStatusNotDetermined = 0, INPaymentMethodTypeApplePay = 2,
       INIntentErrorInteractionOperationNotSupported = 1900, INIntentErrorDonatingInteraction = 1901,
       INIntentErrorDeletingAllInteractions = 1902, INIntentErrorDeletingInteractionWithIdentifiers = 1903,
       INIntentErrorDeletingInteractionWithGroupIdentifier = 1904 };
@interface NSPersonNameComponents : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSString *namePrefix, *givenName, *middleName, *familyName, *nameSuffix, *nickname;
@property(copy) NSPersonNameComponents *phoneticRepresentation;
@end
typedef NSInteger NSPersonNameComponentsFormatterStyle;
enum { NSPersonNameComponentsFormatterStyleDefault = 0 };
@interface NSPersonNameComponentsFormatter : NSFormatter
+ (NSString *)localizedStringFromPersonNameComponents:(NSPersonNameComponents *)components style:(NSPersonNameComponentsFormatterStyle)style options:(NSUInteger)options;
@end
@interface NSKeyedArchiver (SFFinchIntentsArchive)
+ (NSData *)archivedDataWithRootObject:(id)object requiringSecureCoding:(BOOL)secure error:(NSError **)error;
@end
@interface NSKeyedUnarchiver (SFFinchIntentsArchive)
+ (id)unarchivedObjectOfClasses:(NSSet *)classes fromData:(NSData *)data error:(NSError **)error;
+ (id)unarchivedObjectOfClass:(Class)cls fromData:(NSData *)data error:(NSError **)error;
@end
@interface NSUserActivity (SFFinchIntentsActivity)
@property(copy) NSString *targetContentIdentifier;
@end
@interface NSDateFormatter (SFFinchIntentsDateFormatter)
@property(copy) NSTimeZone *timeZone;
@end

extern NSString * const INIntentErrorDomain;
extern NSString * const INAnswerCallIntentIdentifier;
extern NSString * const INHangUpCallIntentIdentifier;
extern NSString * const INPersonHandleLabelHome;
extern NSString * const INPersonHandleLabelWork;
extern NSString * const INPersonHandleLabeliPhone;
extern NSString * const INPersonHandleLabelMobile;
extern NSString * const INPersonHandleLabelMain;
extern NSString * const INPersonHandleLabelHomeFax;
extern NSString * const INPersonHandleLabelWorkFax;
extern NSString * const INPersonHandleLabelPager;
extern NSString * const INPersonHandleLabelOther;
extern NSString * const INPersonHandleLabelSchool;
extern NSString * const INPersonRelationshipSon;
extern NSString * const INPersonRelationshipDaughter;

@interface INPerson : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSString *displayName;
@property(copy) INPersonHandle *personHandle;
@property(copy) NSPersonNameComponents *nameComponents;
@property(copy) NSString *contactIdentifier;
@property(copy) NSString *customIdentifier;
- (instancetype)initWithPersonHandle:(INPersonHandle *)personHandle nameComponents:(NSPersonNameComponents *)nameComponents displayName:(NSString *)displayName image:(INImage *)image contactIdentifier:(NSString *)contactIdentifier customIdentifier:(NSString *)customIdentifier;
- (NSString *)displayName;
- (BOOL)isContactSuggestion;
- (NSString *)description;
@end

@interface INPersonHandle : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSString *value;
@property INPersonHandleType type;
@property(copy) NSString *label;
- (instancetype)initWithValue:(NSString *)value type:(INPersonHandleType)type label:(NSString *)label;
- (NSString *)description;
@end

@interface INObject : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSString *identifier;
@property(copy) NSString *displayString;
@property(copy) NSString *subtitleString;
@property(copy) NSString *pronunciationHint;
- (instancetype)initWithIdentifier:(NSString *)identifier displayString:(NSString *)displayString subtitleString:(NSString *)subtitleString displayImage:(INImage *)displayImage;
- (NSString *)description;
- (NSString *)spokenPhrase;
- (NSString *)vocabularyIdentifier;
@end

@interface INAirline : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSString *name;
@property(copy) NSString *iataCode;
@property(copy) NSString *icaoCode;
- (instancetype)initWithName:(NSString *)name iataCode:(NSString *)iataCode icaoCode:(NSString *)icaoCode;
- (NSString *)description;
@end

@interface INSpeakableString : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSString *vocabularyIdentifier;
@property(copy) NSString *spokenPhrase;
@property(copy) NSString *pronunciationHint;
- (instancetype)initWithVocabularyIdentifier:(NSString *)vocabularyIdentifier spokenPhrase:(NSString *)spokenPhrase pronunciationHint:(NSString *)pronunciationHint;
- (NSString *)description;
@end

@interface INFocusStatus : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSNumber *isFocused;
- (instancetype)initWithIsFocused:(NSNumber *)isFocused;
- (NSString *)description;
@end

@interface INIntentResolutionResult : NSObject <NSCopying, NSSecureCoding>
- (NSString *)description;
@end

@interface INShortcut : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSUserActivity *userActivity;
@property(copy) INIntent *intent;
- (instancetype)initWithUserActivity:(NSUserActivity *)userActivity;
- (instancetype)initWithIntent:(INIntent *)intent;
- (id)copyWithZone:(NSZone *)zone;
- (NSString *)description;
@end

@interface INInteraction : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSDateInterval *dateInterval;
@property(copy) INIntent *intent;
@property(copy) INIntentResponse *intentResponse;
@property(copy) NSString *identifier;
@property(copy) NSString *groupIdentifier;
@property INIntentHandlingStatus intentHandlingStatus;
@property INInteractionDirection direction;
- (NSString *)description;
- (instancetype)initWithIntent:(INIntent *)intent response:(INIntentResponse *)response;
- (void)donateInteractionWithCompletion:(void (^)(NSError *))completion;
+ (void)deleteAllInteractionsWithCompletion:(void (^)(NSError *))completion;
+ (void)deleteInteractionsWithIdentifiers:(NSArray<NSString *> *)identifiers completion:(void (^)(NSError *))completion;
+ (void)deleteInteractionsWithGroupIdentifier:(NSString *)groupIdentifier completion:(void (^)(NSError *))completion;
@end

@interface INImage : NSObject <NSCopying, NSSecureCoding>
+ (instancetype)imageNamed:(NSString *)name;
+ (instancetype)imageWithImageData:(NSData *)imageData;
+ (instancetype)imageWithURL:(NSURL *)URL;
+ (instancetype)imageWithURL:(NSURL *)URL width:(double)width height:(double)height;
@end

@interface INFile : NSObject <NSCopying, NSSecureCoding>
@property(readonly, copy) NSString *filename, *typeIdentifier;
+ (INFile *)fileWithData:(NSData *)data filename:(NSString *)filename typeIdentifier:(NSString *)typeIdentifier;
+ (INFile *)fileWithFileURL:(NSURL *)fileURL filename:(NSString *)filename typeIdentifier:(NSString *)typeIdentifier;
- (NSData *)data;
@end

@interface INPaymentMethod : NSObject <NSCopying, NSSecureCoding>
@property INPaymentMethodType type;
+ (instancetype)applePayPaymentMethod;
@end

@interface INFocusStatusCenter : NSObject
+ (INFocusStatusCenter *)defaultCenter;
- (INFocusStatus *)focusStatus;
- (INFocusStatusAuthorizationStatus)authorizationStatus;
- (void)requestAuthorizationWithCompletionHandler:(void (^)(INFocusStatusAuthorizationStatus))completionHandler;
@end

@interface INIntent : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSString *suggestedInvocationPhrase;
- (void)setImage:(INImage *)image forParameterNamed:(NSString *)parameterName;
- (INImage *)imageForParameterNamed:(NSString *)parameterName;
- (INImage *)keyImage;
@end

@interface INPreferences : NSObject
+ (INSiriAuthorizationStatus)siriAuthorizationStatus;
+ (void)requestSiriAuthorization:(void (^)(INSiriAuthorizationStatus))handler;
+ (NSString *)siriLanguageCode;
@end

@interface INVoiceShortcutCenter : NSObject
+ (INVoiceShortcutCenter *)sharedCenter;
- (void)getAllVoiceShortcutsWithCompletion:(void (^)(NSArray<INVoiceShortcut *> *, NSError *))completionHandler;
- (void)getVoiceShortcutWithIdentifier:(NSUUID *)identifier completion:(void (^)(INVoiceShortcut *, NSError *))completionHandler;
- (void)setShortcutSuggestions:(NSArray<INShortcut *> *)suggestions;
@end

@interface INRelevantShortcutStore : NSObject
+ (INRelevantShortcutStore *)defaultStore;
- (void)setRelevantShortcuts:(NSArray<INRelevantShortcut *> *)shortcuts completionHandler:(void (^)(NSError *))completionHandler;
@end

@interface INAnswerCallIntent : INIntent <NSCopying, NSSecureCoding>
@property INCallAudioRoute audioRoute;
@property(copy) NSString *callIdentifier;
- (instancetype)initWithAudioRoute:(INCallAudioRoute)audioRoute callIdentifier:(NSString *)callIdentifier;
- (NSString *)description;
@end

@interface INIntentResponse : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSUserActivity *userActivity;
@end

@interface INAnswerCallIntentResponse : INIntentResponse <NSCopying, NSSecureCoding>
@property INAnswerCallIntentResponseCode code;
@property(copy) NSArray *callRecords;
- (instancetype)initWithCode:(INAnswerCallIntentResponseCode)code userActivity:(NSUserActivity *)userActivity;
- (NSString *)description;
@end

/* SPDX-License-Identifier: MIT */
/* Own declarations for the models explicitly used in Finch source and tests. */


@interface INBooleanResolutionResult : INIntentResolutionResult
+ (instancetype)successWithResolvedValue:(BOOL)resolvedValue;
@end
@interface INSendMessageAttachment : NSObject <NSCopying, NSSecureCoding>
@property(copy) INFile *audioMessageFile;
+ (instancetype)attachmentWithAudioMessageFile:(INFile *)audioMessageFile;
@end
@interface INVoiceShortcut : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSUUID *identifier;
@property(copy) INShortcut *shortcut;
@end
@interface INRelevantShortcut : NSObject <NSCopying, NSSecureCoding>
@property(copy) INShortcut *shortcut;
- (instancetype)initWithShortcut:(INShortcut *)shortcut;
@end

@interface NSString (SFFinchIntentsLocalization)
+ (NSString *)deferredLocalizedIntentsStringWithFormat:(NSString *)format, ...;
+ (NSString *)deferredLocalizedIntentsStringWithFormat:(NSString *)format fromTable:(NSString *)table, ...;
+ (NSString *)deferredLocalizedIntentsStringWithFormat:(NSString *)format fromTable:(NSString *)table arguments:(va_list)arguments;
@end
