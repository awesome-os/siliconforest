/* SPDX-License-Identifier: MIT */
/* Own declarations for Finch's Objective-C CloudKit implementation. */
@class CLLocation, NSPersonNameComponents;
typedef NSString *CKRecordType, *CKRecordFieldKey, *CKSubscriptionID, *CKOperationID;
typedef NSInteger CKAccountStatus, CKApplicationPermissionStatus, CKDatabaseScope, CKSubscriptionType;
typedef NSInteger CKNotificationType, CKShareParticipantPermission, CKShareParticipantRole;
typedef NSInteger CKShareParticipantAcceptanceStatus, CKRecordSavePolicy, CKSyncEnginePendingDatabaseChangeType;
typedef NSInteger CKSyncEnginePendingRecordZoneChangeType;
typedef NSUInteger CKReferenceAction, CKApplicationPermissions, CKQuerySubscriptionOptions;
typedef NSUInteger CKRecordZoneCapabilities, CKSharingParticipantAccessOption, CKSharingParticipantPermissionOption;
typedef void (^CKApplicationPermissionBlock)(CKApplicationPermissionStatus, NSError *);
enum { CKQuerySubscriptionOptionsFiresOnRecordUpdate=1UL<<1, CKAccountStatusCouldNotDetermine=0, CKAccountStatusNoAccount=3, CKDatabaseScopePublic=1, CKDatabaseScopePrivate=2, CKDatabaseScopeShared=3,
 CKErrorNetworkUnavailable=3, CKErrorNotAuthenticated=9, CKNotificationTypeQuery=1, CKNotificationTypeRecordZone=2,
 CKReferenceActionNone=0, CKReferenceActionDeleteSelf=1, CKSubscriptionTypeQuery=1,
 CKSubscriptionTypeRecordZone=2, CKSubscriptionTypeDatabase=3, CKShareParticipantRoleOwner=1,
 CKShareParticipantRolePrivateUser=3, CKShareParticipantPermissionNone=1, CKShareParticipantPermissionReadOnly=2,
 CKShareParticipantPermissionReadWrite=3, CKShareParticipantAcceptanceStatusPending=1,
 CKShareParticipantAcceptanceStatusAccepted=2, CKSharingParticipantAccessOptionAny=3,
 CKSharingParticipantPermissionOptionAny=3, CKApplicationPermissionStatusInitialState=0,
 CKSyncEnginePendingDatabaseChangeTypeSaveZone=0, CKSyncEnginePendingDatabaseChangeTypeDeleteZone=1,
 CKSyncEnginePendingRecordZoneChangeTypeSaveRecord=0, CKSyncEnginePendingRecordZoneChangeTypeDeleteRecord=1 };
@protocol CKSyncEngineDelegate <NSObject>
@end
@interface NSCoder (SFFinchCloudKitCoding)
- (id)decodeObjectOfClass:(Class)cls forKey:(NSString *)key;
- (id)decodeObjectOfClasses:(NSSet *)classes forKey:(NSString *)key;
@end
@interface NSKeyedArchiver (SFFinchCloudKitArchive)
+ (NSData *)archivedDataWithRootObject:(id)object requiringSecureCoding:(BOOL)secure error:(NSError **)error;
@end
@interface NSKeyedUnarchiver (SFFinchCloudKitArchive)
+ (id)unarchivedObjectOfClasses:(NSSet *)classes fromData:(NSData *)data error:(NSError **)error;
+ (id)unarchivedObjectOfClass:(Class)cls fromData:(NSData *)data error:(NSError **)error;
@end

extern const NSUInteger CKQueryOperationMaximumResults;
