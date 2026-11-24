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
