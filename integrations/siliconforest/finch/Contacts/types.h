/* SPDX-License-Identifier: MIT */
/* Own declarations for the values and methods used by Finch; no SDK input. */
typedef NSInteger CNContactType, CNContainerType, CNContactSortOrder, CNEntityType;
typedef NSInteger CNContactFormatterStyle, CNContactDisplayNameOrder, CNPostalAddressFormatterStyle;
typedef NSInteger CNAuthorizationStatus, CNErrorCode;
enum { CNContactTypePerson = 0, CNContactTypeOrganization = 1, CNContainerTypeLocal = 1,
       CNContactSortOrderNone = 0, CNContactSortOrderUserDefault = 1,
       CNContactSortOrderGivenName = 2, CNContactSortOrderFamilyName = 3,
       CNContactFormatterStyleFullName = 0, CNContactFormatterStylePhoneticFullName = 1,
       CNContactDisplayNameOrderUserDefault = 0, CNContactDisplayNameOrderGivenNameFirst = 1,
       CNContactDisplayNameOrderFamilyNameFirst = 2, CNPostalAddressFormatterStyleMailingAddress = 0,
       CNAuthorizationStatusAuthorized = 3, CNEntityTypeContacts = 0,
       CNErrorCodeDataAccessError = 2, CNErrorCodeRecordDoesNotExist = 200,
       CNErrorCodeInsertedRecordAlreadyExists = 201, CNErrorCodeContainmentCycle = 202,
       CNErrorCodeParentRecordDoesNotExist = 204, CNErrorCodePredicateInvalid = 400 };
@protocol CNKeyDescriptor <NSObject, NSCopying, NSSecureCoding>
@end
@interface NSString (SFFinchContactKeyDescriptor) <CNKeyDescriptor>
@end
@protocol CNChangeHistoryEventVisitor <NSObject>
- (void)visitDropEverythingEvent:(CNChangeHistoryDropEverythingEvent *)event;
- (void)visitAddContactEvent:(CNChangeHistoryAddContactEvent *)event;
- (void)visitUpdateContactEvent:(CNChangeHistoryUpdateContactEvent *)event;
- (void)visitDeleteContactEvent:(CNChangeHistoryDeleteContactEvent *)event;
@optional
- (void)visitAddGroupEvent:(CNChangeHistoryAddGroupEvent *)event;
- (void)visitUpdateGroupEvent:(CNChangeHistoryUpdateGroupEvent *)event;
- (void)visitDeleteGroupEvent:(CNChangeHistoryDeleteGroupEvent *)event;
- (void)visitAddMemberToGroupEvent:(CNChangeHistoryAddMemberToGroupEvent *)event;
- (void)visitRemoveMemberFromGroupEvent:(CNChangeHistoryRemoveMemberFromGroupEvent *)event;
- (void)visitAddSubgroupToGroupEvent:(CNChangeHistoryAddSubgroupToGroupEvent *)event;
- (void)visitRemoveSubgroupFromGroupEvent:(CNChangeHistoryRemoveSubgroupFromGroupEvent *)event;
@end
@interface NSCoder (SFFinchContactsDeclarations)
- (id)decodeObjectOfClass:(Class)cls forKey:(NSString *)key;
- (id)decodeObjectOfClasses:(NSSet *)classes forKey:(NSString *)key;
@end
@interface NSKeyedArchiver (SFFinchContactsDeclarations)
+ (NSData *)archivedDataWithRootObject:(id)object requiringSecureCoding:(BOOL)secure error:(NSError **)error;
@end
@interface NSKeyedUnarchiver (SFFinchContactsDeclarations)
+ (id)unarchivedObjectOfClasses:(NSSet *)classes fromData:(NSData *)data error:(NSError **)error;
@end
@interface NSDateComponents (SFFinchContactsDeclarations)
@property NSInteger year, month, day, era;
@property(copy) NSCalendar *calendar;
@end
#ifndef NSDateComponentUndefined
#define NSDateComponentUndefined NSIntegerMax
#endif
@interface NSScanner (SFFinchContactsDeclarations)
@property(readonly, getter=isAtEnd) BOOL atEnd;
@end

@class FinchContactsDB;
#ifndef NS_NOESCAPE
#define NS_NOESCAPE __attribute__((noescape))
#endif
@interface NSString (SFFinchContactsComparison)
- (NSComparisonResult)localizedStandardCompare:(NSString *)other;
@end
@interface NSSet (SFFinchContactsSet)
- (NSSet *)setByAddingObject:(id)object;
@end

enum { NSDiacriticInsensitiveSearch = 1UL << 7 };
/* The archive format uses this literal key; PD does not export the alias. */
#define NSKeyedArchiveRootObjectKey @"root"
@interface NSKeyedUnarchiver (SFFinchContactsSecureCoding)
@property BOOL requiresSecureCoding;
@end
/* arm64 cross target is little endian. */
static inline unsigned long long NSSwapHostLongLongToLittle(unsigned long long x) { return x; }
static inline unsigned long long NSSwapLittleLongLongToHost(unsigned long long x) { return x; }
@interface NSCalendar (SFFinchContactsCalendar)
+ (instancetype)calendarWithIdentifier:(NSString *)identifier;
@property(readonly, copy) NSString *calendarIdentifier;
@end
@interface NSString (SFFinchContactsLines)
- (void)enumerateLinesUsingBlock:(void (^)(NSString *line, BOOL *stop))block;
@end
