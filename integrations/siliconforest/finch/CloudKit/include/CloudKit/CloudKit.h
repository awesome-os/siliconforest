/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class CKAllowedSharingOptions, CKAsset, CKContainer, CKDatabase, CKFetchRecordZoneChangesConfiguration, CKFetchRecordZoneChangesOptions, CKLocationSortDescriptor, CKNotification, CKNotificationID, CKNotificationInfo, CKOperation, CKOperationConfiguration, CKOperationGroup, CKQuery, CKQueryCursor, CKQueryNotification, CKRecord, CKRecordID, CKRecordZone, CKRecordZoneID, CKRecordZoneNotification, CKReference, CKServerChangeToken, CKShare, CKShareMetadata, CKShareParticipant, CKSubscription, CKSyncEngine, CKSyncEngineConfiguration, CKSyncEngineFetchChangesOptions, CKSyncEngineFetchChangesScope, CKSyncEnginePendingDatabaseChange, CKSyncEnginePendingRecordZoneChange, CKSyncEnginePendingZoneDelete, CKSyncEnginePendingZoneSave, CKSyncEngineSendChangesOptions, CKSyncEngineSendChangesScope, CKSyncEngineState, CKSyncEngineStateSerialization, CKUserIdentity, CKUserIdentityLookupInfo, CKDatabaseOperation, CKDatabaseSubscription, CKDiscoverAllUserIdentitiesOperation, CKDiscoverUserIdentitiesOperation, CKFetchDatabaseChangesOperation, CKFetchRecordChangesOperation, CKFetchRecordZoneChangesOperation, CKFetchRecordZonesOperation, CKFetchRecordsOperation, CKFetchShareMetadataOperation, CKFetchShareParticipantsOperation, CKFetchSubscriptionsOperation, CKFetchWebAuthTokenOperation, CKModifyRecordZonesOperation, CKModifyRecordsOperation, CKModifySubscriptionsOperation, CKQueryOperation, CKQuerySubscription, CKRecordZoneSubscription, CKShareRequestAccessOperation, CKAcceptSharesOperation;
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

extern NSString * const CKAccountChangedNotification;
extern NSString * const CKCurrentUserDefaultName;
extern NSString * const CKErrorDomain;
extern NSString * const CKErrorRetryAfterKey;
extern NSString * const CKErrorUserDidResetEncryptedDataKey;
extern NSString * const CKOwnerDefaultName;
extern NSString * const CKPartialErrorsByItemIDKey;
extern NSString * const CKRecordChangedErrorAncestorRecordKey;
extern NSString * const CKRecordChangedErrorClientRecordKey;
extern NSString * const CKRecordChangedErrorServerRecordKey;
extern NSString * const CKRecordCreationDateKey;
extern NSString * const CKRecordCreatorUserRecordIDKey;
extern NSString * const CKRecordLastModifiedUserRecordIDKey;
extern NSString * const CKRecordModificationDateKey;
extern NSString * const CKRecordNameZoneWideShare;
extern NSString * const CKRecordParentKey;
extern NSString * const CKRecordRecordIDKey;
extern NSString * const CKRecordShareKey;
extern NSString * const CKRecordTypeShare;
extern NSString * const CKRecordTypeUserRecord;
extern NSString * const CKRecordZoneDefaultName;
extern NSString * const CKShareThumbnailImageDataKey;
extern NSString * const CKShareTitleKey;
extern NSString * const CKShareTypeKey;

@interface CKAllowedSharingOptions : NSObject
@property(assign) CKSharingParticipantAccessOption allowedParticipantAccessOptions;
@property(assign) CKSharingParticipantPermissionOption allowedParticipantPermissionOptions;
- (instancetype)initWithAllowedParticipantPermissionOptions:(CKSharingParticipantPermissionOption)permissionOptions allowedParticipantAccessOptions:(CKSharingParticipantAccessOption)accessOptions;
+ (instancetype)standardOptions;
- (id)copyWithZone:(NSZone *)zone;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
+ (BOOL)supportsSecureCoding;
@end

@interface CKAsset : NSObject
@property(copy) NSURL * fileURL;
- (instancetype)initWithFileURL:(NSURL *)fileURL;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
+ (BOOL)supportsSecureCoding;
@end

@interface CKContainer : NSObject
@property(copy) NSString * containerIdentifier;
+ (instancetype)defaultContainer;
+ (instancetype)containerWithIdentifier:(NSString *)containerIdentifier;
- (CKDatabase *)_databaseWithScope:(CKDatabaseScope)scope;
- (CKDatabase *)privateCloudDatabase;
- (CKDatabase *)publicCloudDatabase;
- (CKDatabase *)sharedCloudDatabase;
- (CKDatabase *)databaseWithDatabaseScope:(CKDatabaseScope)scope;
- (void)addOperation:(CKOperation *)operation;
- (void)accountStatusWithCompletionHandler:(void (^)(CKAccountStatus, NSError *))completionHandler;
- (void)fetchUserRecordIDWithCompletionHandler:(void (^)(CKRecordID *, NSError *))completionHandler;
- (void)discoverAllIdentitiesWithCompletionHandler:(void (^)(NSArray<CKUserIdentity *> *, NSError *))completionHandler;
- (void)discoverUserIdentityWithEmailAddress:(NSString *)email completionHandler:(void (^)(CKUserIdentity *, NSError *))completionHandler;
- (void)discoverUserIdentityWithPhoneNumber:(NSString *)phoneNumber completionHandler:(void (^)(CKUserIdentity *, NSError *))completionHandler;
- (void)discoverUserIdentityWithUserRecordID:(CKRecordID *)userRecordID completionHandler:(void (^)(CKUserIdentity *, NSError *))completionHandler;
- (void)fetchShareParticipantWithEmailAddress:(NSString *)emailAddress completionHandler:(void (^)(CKShareParticipant *, NSError *))completionHandler;
- (void)fetchShareParticipantWithPhoneNumber:(NSString *)phoneNumber completionHandler:(void (^)(CKShareParticipant *, NSError *))completionHandler;
- (void)fetchShareParticipantWithUserRecordID:(CKRecordID *)userRecordID completionHandler:(void (^)(CKShareParticipant *, NSError *))completionHandler;
- (void)fetchShareMetadataWithURL:(NSURL *)url completionHandler:(void (^)(CKShareMetadata *, NSError *))completionHandler;
- (void)acceptShareMetadata:(CKShareMetadata *)metadata completionHandler:(void (^)(CKShare *, NSError *))completionHandler;
- (void)fetchAllLongLivedOperationIDsWithCompletionHandler:(void (^)(NSArray<CKOperationID> *, NSError *))completionHandler;
- (void)fetchLongLivedOperationWithID:(CKOperationID)operationID completionHandler:(void (^)(CKOperation *, NSError *))completionHandler;
- (void)statusForApplicationPermission:(CKApplicationPermissions)applicationPermission completionHandler:(CKApplicationPermissionBlock)completionHandler;
- (void)requestApplicationPermission:(CKApplicationPermissions)applicationPermission completionHandler:(CKApplicationPermissionBlock)completionHandler;
@end

@interface CKDatabase : NSObject
@property(assign) NSInteger databaseScope;
- (void)addOperation:(CKDatabaseOperation *)operation;
- (NSError *)_unavailableError;
- (void)fetchRecordWithID:(CKRecordID *)recordID completionHandler:(void (^)(CKRecord *, NSError *))completionHandler;
- (void)saveRecord:(CKRecord *)record completionHandler:(void (^)(CKRecord *, NSError *))completionHandler;
- (void)deleteRecordWithID:(CKRecordID *)recordID completionHandler:(void (^)(CKRecordID *, NSError *))completionHandler;
- (void)performQuery:(CKQuery *)query inZoneWithID:(CKRecordZoneID *)zoneID completionHandler:(void (^)(NSArray<CKRecord *> *, NSError *))completionHandler;
- (void)fetchAllRecordZonesWithCompletionHandler:(void (^)(NSArray<CKRecordZone *> *, NSError *))completionHandler;
- (void)fetchRecordZoneWithID:(CKRecordZoneID *)zoneID completionHandler:(void (^)(CKRecordZone *, NSError *))completionHandler;
- (void)saveRecordZone:(CKRecordZone *)zone completionHandler:(void (^)(CKRecordZone *, NSError *))completionHandler;
- (void)deleteRecordZoneWithID:(CKRecordZoneID *)zoneID completionHandler:(void (^)(CKRecordZoneID *, NSError *))completionHandler;
- (void)fetchSubscriptionWithID:(CKSubscriptionID)subscriptionID completionHandler:(void (^)(CKSubscription *, NSError *))completionHandler;
- (void)fetchAllSubscriptionsWithCompletionHandler:(void (^)(NSArray<CKSubscription *> *, NSError *))completionHandler;
- (void)saveSubscription:(CKSubscription *)subscription completionHandler:(void (^)(CKSubscription *, NSError *))completionHandler;
- (void)deleteSubscriptionWithID:(CKSubscriptionID)subscriptionID completionHandler:(void (^)(CKSubscriptionID, NSError *))completionHandler;
@end

@interface CKFetchRecordZoneChangesConfiguration : NSObject


@end

@interface CKFetchRecordZoneChangesOptions : NSObject


@end

@interface CKLocationSortDescriptor : NSSortDescriptor
@property(retain) CLLocation * relativeLocation;
- (instancetype)initWithKey:(NSString *)key relativeLocation:(CLLocation *)relativeLocation;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
+ (BOOL)supportsSecureCoding;
@end

@interface CKNotification : NSObject
@property(copy) NSString * containerIdentifier;
@property(retain) CKNotificationID * notificationID;
@property(assign) CKNotificationType notificationType;
@property(assign) CKSubscriptionID subscriptionID;
+ (instancetype)notificationFromRemoteNotificationDictionary:(NSDictionary *)notificationDictionary;
@end

@interface CKNotificationID : NSObject

- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)object;
- (NSUInteger)hash;
+ (BOOL)supportsSecureCoding;
@end

@interface CKNotificationInfo : NSObject
@property(assign) BOOL shouldBadge;
@property(assign) BOOL shouldSendContentAvailable;
@property(assign) BOOL shouldSendMutableContent;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
@end

@interface CKOperation : NSOperation
@property(copy) NSString * operationID;
- (instancetype)init;
- (CKOperationConfiguration *)configuration;
- (void)setConfiguration:(CKOperationConfiguration *)configuration;
- (void)main;
@end

@interface CKOperationConfiguration : NSObject
@property(assign) BOOL allowsCellularAccess;
@property(retain) CKContainer * container;
@property(assign,getter=isLongLived) BOOL longLived;
@property(assign) NSQualityOfService qualityOfService;
@property(assign) NSTimeInterval timeoutIntervalForRequest;
@property(assign) NSTimeInterval timeoutIntervalForResource;
- (instancetype)init;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface CKOperationGroup : NSObject
@property(retain) CKOperationConfiguration * defaultConfiguration;
@property(assign) NSInteger expectedReceiveSize;
@property(assign) NSInteger expectedSendSize;
@property(copy) NSString * name;
@property(copy) NSString * operationGroupID;
@property(assign) NSUInteger quantity;
- (instancetype)init;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
- (CKOperationConfiguration *)defaultConfiguration;
- (void)setDefaultConfiguration:(CKOperationConfiguration *)configuration;
+ (BOOL)supportsSecureCoding;
@end

@interface CKQuery : NSObject
@property(retain) NSPredicate * predicate;
@property(copy) NSString * recordType;
@property(copy) NSArray * sortDescriptors;
- (instancetype)initWithRecordType:(CKRecordType)recordType predicate:(NSPredicate *)predicate;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
@end

@interface CKQueryCursor : NSObject


@end

@interface CKQueryNotification : CKNotification
@property(assign) CKDatabaseScope databaseScope;
@property(assign) NSInteger queryNotificationReason;
@property(retain) CKRecordID * recordID;

@end

@interface CKRecord : NSObject
@property(copy) NSDate * creationDate;
@property(retain) CKRecordID * creatorUserRecordID;
@property(retain) CKRecordID * lastModifiedUserRecordID;
@property(copy) NSDate * modificationDate;
@property(retain) CKReference * parent;
@property(copy) NSString * recordChangeTag;
@property(retain) CKRecordID * recordID;
@property(copy) NSString * recordType;
@property(retain) CKReference * share;
- (instancetype)initWithRecordType:(CKRecordType)recordType;
- (instancetype)initWithRecordType:(CKRecordType)recordType recordID:(CKRecordID *)recordID;
- (instancetype)initWithRecordType:(CKRecordType)recordType zoneID:(CKRecordZoneID *)zoneID;
- (NSDictionary *)_fieldValues;
- (id)objectForKey:(CKRecordFieldKey)key;
- (void)setObject:(id)object forKey:(CKRecordFieldKey)key;
- (NSArray *)allKeys;
- (NSArray *)allTokens;
- (id)objectForKeyedSubscript:(CKRecordFieldKey)key;
- (void)setObject:(id)object forKeyedSubscript:(CKRecordFieldKey)key;
- (NSArray *)changedKeys;
- (void)encodeSystemFieldsWithCoder:(NSCoder *)coder;
- (void)_setSystemValue:(id)value forKey:(NSString *)key;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
- (void)setParentReferenceFromRecord:(CKRecord *)parentRecord;
- (void)setParentReferenceFromRecordID:(CKRecordID *)parentRecordID;
@end

@interface CKRecordID : NSObject
@property(copy) NSString * recordName;
@property(retain) CKRecordZoneID * zoneID;
- (instancetype)initWithRecordName:(NSString *)recordName;
- (instancetype)initWithRecordName:(NSString *)recordName zoneID:(CKRecordZoneID *)zoneID;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)object;
- (NSUInteger)hash;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
+ (BOOL)supportsSecureCoding;
@end

@interface CKRecordZone : NSObject
@property(assign) NSUInteger capabilities;
@property(assign) NSInteger encryptionScope;
@property(retain) CKReference * share;
@property(retain) CKRecordZoneID * zoneID;
+ (CKRecordZone *)defaultRecordZone;
- (instancetype)initWithZoneName:(NSString *)zoneName;
- (instancetype)initWithZoneID:(CKRecordZoneID *)zoneID;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
+ (BOOL)supportsSecureCoding;
@end

@interface CKRecordZoneID : NSObject
@property(copy) NSString * ownerName;
@property(copy) NSString * zoneName;
- (instancetype)initWithZoneName:(NSString *)zoneName ownerName:(NSString *)ownerName;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)object;
- (NSUInteger)hash;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
+ (BOOL)supportsSecureCoding;
@end

@interface CKRecordZoneNotification : CKNotification
@property(assign) CKDatabaseScope databaseScope;
@property(retain) CKRecordZoneID * recordZoneID;

@end

@interface CKReference : NSObject
@property(retain) CKRecordID * recordID;
@property(assign) NSInteger referenceAction;
- (instancetype)initWithRecordID:(CKRecordID *)recordID action:(CKReferenceAction)action;
- (instancetype)initWithRecord:(CKRecord *)record action:(CKReferenceAction)action;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)object;
- (NSUInteger)hash;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
+ (BOOL)supportsSecureCoding;
@end

@interface CKServerChangeToken : NSObject


@end

@interface CKShare : CKRecord
@property(copy) NSURL * URL;
@property(retain) id allKeys;
@property(retain) CKShareParticipant * currentUserParticipant;
@property(retain) CKRecordID * hierarchicalRootRecordID;
@property(retain) CKShareParticipant * owner;
@property(copy) NSArray * participants;
@property(assign) NSInteger publicPermission;
- (instancetype)initWithRecordZoneID:(CKRecordZoneID *)recordZoneID;
- (instancetype)initWithRootRecord:(CKRecord *)rootRecord;
- (instancetype)initWithRootRecord:(CKRecord *)rootRecord shareID:(CKRecordID *)shareID;
- (void)addParticipant:(CKShareParticipant *)participant;
- (void)removeParticipant:(CKShareParticipant *)participant;
- (NSURL *)oneTimeURLForParticipantID:(NSString *)participantID;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
@end

@interface CKShareMetadata : NSObject


@end

@interface CKShareParticipant : NSObject
@property(assign) NSInteger acceptanceStatus;
@property(copy) NSString * participantID;
@property(assign) NSInteger permission;
@property(assign) NSInteger role;
@property(retain) CKUserIdentity * userIdentity;
+ (instancetype)oneTimeURLParticipant;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
@end

@interface CKSubscription : NSObject
@property(retain) CKNotificationInfo * notificationInfo;
@property(copy) NSString * subscriptionID;
@property(assign) NSInteger subscriptionType;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
@end

@interface CKSyncEngine : NSObject
@property(retain) CKDatabase * database;
@property(retain) CKSyncEngineState * state;
- (instancetype)initWithConfiguration:(CKSyncEngineConfiguration *)configuration;
- (void)fetchChangesWithCompletionHandler:(void (^)(NSError *))completionHandler;
- (void)fetchChangesWithOptions:(CKSyncEngineFetchChangesOptions *)options completionHandler:(void (^)(NSError *))completionHandler;
- (void)sendChangesWithCompletionHandler:(void (^)(NSError *))completionHandler;
- (void)sendChangesWithOptions:(CKSyncEngineSendChangesOptions *)options completionHandler:(void (^)(NSError *))completionHandler;
- (void)cancelOperationsWithCompletionHandler:(void (^)(void))completionHandler;
@end

@interface CKSyncEngineConfiguration : NSObject
@property(assign) BOOL automaticallySync;
@property(retain) CKDatabase * database;
@property(weak) id<CKSyncEngineDelegate> delegate;
@property(retain) CKSyncEngineStateSerialization * stateSerialization;
- (instancetype)initWithDatabase:(CKDatabase *)database stateSerialization:(CKSyncEngineStateSerialization *)stateSerialization delegate:(id<CKSyncEngineDelegate>)delegate;
@end

@interface CKSyncEngineFetchChangesOptions : NSObject
@property(retain) id operationGroup;
@property(retain) id prioritizedZoneIDs;
@property(retain) CKSyncEngineFetchChangesScope * scope;
- (instancetype)initWithScope:(CKSyncEngineFetchChangesScope *)scope;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface CKSyncEngineFetchChangesScope : NSObject
@property(retain) id excludedZoneIDs;
@property(copy) NSArray * zoneIDs;
- (instancetype)initWithZoneIDs:(NSSet<CKRecordZoneID *> *)zoneIDs;
- (instancetype)initWithExcludedZoneIDs:(NSSet<CKRecordZoneID *> *)zoneIDs;
- (BOOL)containsZoneID:(CKRecordZoneID *)zoneID;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface CKSyncEnginePendingDatabaseChange : NSObject
@property(assign) CKSyncEnginePendingDatabaseChangeType type;
@property(retain) CKRecordZoneID * zoneID;

@end

@interface CKSyncEnginePendingRecordZoneChange : NSObject
@property(retain) CKRecordID * recordID;
@property(assign) NSInteger type;
- (instancetype)initWithRecordID:(CKRecordID *)recordID type:(CKSyncEnginePendingRecordZoneChangeType)type;
@end

@interface CKSyncEnginePendingZoneDelete : CKSyncEnginePendingDatabaseChange

- (instancetype)initWithZoneID:(CKRecordZoneID *)zoneID;
@end

@interface CKSyncEnginePendingZoneSave : CKSyncEnginePendingDatabaseChange
@property(retain) id zone;
- (instancetype)initWithZone:(CKRecordZone *)zone;
@end

@interface CKSyncEngineSendChangesOptions : NSObject
@property(retain) id operationGroup;
@property(retain) CKSyncEngineSendChangesScope * scope;
- (instancetype)initWithScope:(CKSyncEngineSendChangesScope *)scope;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface CKSyncEngineSendChangesScope : NSObject
@property(retain) id excludedZoneIDs;
@property(copy) NSArray * recordIDs;
@property(copy) NSArray * zoneIDs;
- (instancetype)initWithZoneIDs:(NSSet<CKRecordZoneID *> *)zoneIDs;
- (instancetype)initWithExcludedZoneIDs:(NSSet<CKRecordZoneID *> *)zoneIDs;
- (instancetype)initWithRecordIDs:(NSSet<CKRecordID *> *)recordIDs;
- (BOOL)containsRecordID:(CKRecordID *)recordID;
- (BOOL)containsPendingRecordZoneChange:(CKSyncEnginePendingRecordZoneChange *)change;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface CKSyncEngineState : NSObject
@property(assign) BOOL hasPendingUntrackedChanges;
@property(copy) NSArray * pendingDatabaseChanges;
@property(copy) NSArray * pendingRecordZoneChanges;
@property(retain) id zoneIDsWithUnfetchedServerChanges;
- (instancetype)_initWithSerialization:(CKSyncEngineStateSerialization *)serialization;
- (void)addPendingRecordZoneChanges:(NSArray<CKSyncEnginePendingRecordZoneChange *> *)changes;
- (void)removePendingRecordZoneChanges:(NSArray<CKSyncEnginePendingRecordZoneChange *> *)changes;
- (void)addPendingDatabaseChanges:(NSArray<CKSyncEnginePendingDatabaseChange *> *)changes;
- (void)removePendingDatabaseChanges:(NSArray<CKSyncEnginePendingDatabaseChange *> *)changes;
- (CKSyncEngineStateSerialization *)_serialization;
@end

@interface CKSyncEngineStateSerialization : NSObject

- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
@end

@interface CKUserIdentity : NSObject
@property(copy) NSArray * contactIdentifiers;
@property(assign) BOOL hasiCloudAccount;
@property(retain) CKUserIdentityLookupInfo * lookupInfo;
@property(retain) NSPersonNameComponents * nameComponents;
@property(retain) CKRecordID * userRecordID;

@end

@interface CKUserIdentityLookupInfo : NSObject
@property(copy) NSString * emailAddress;
@property(copy) NSString * phoneNumber;
@property(retain) id userRecordID;
- (instancetype)initWithEmailAddress:(NSString *)emailAddress;
- (instancetype)initWithPhoneNumber:(NSString *)phoneNumber;
- (instancetype)initWithUserRecordID:(CKRecordID *)userRecordID;
+ (NSArray<CKUserIdentityLookupInfo *> *)lookupInfosWithEmails:(NSArray<NSString *> *)emails;
+ (NSArray<CKUserIdentityLookupInfo *> *)lookupInfosWithPhoneNumbers:(NSArray<NSString *> *)phoneNumbers;
+ (NSArray<CKUserIdentityLookupInfo *> *)lookupInfosWithRecordIDs:(NSArray<CKRecordID *> *)recordIDs;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
@end

@interface CKDatabaseOperation : CKOperation
@property(retain) CKDatabase * database;

@end

@interface CKDatabaseSubscription : CKSubscription
@property(copy) NSString * recordType;
- (instancetype)init;
- (instancetype)initWithSubscriptionID:(CKSubscriptionID)subscriptionID;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
@end

@interface CKDiscoverAllUserIdentitiesOperation : CKDatabaseOperation
@property(copy) void (^discoverAllUserIdentitiesCompletionBlock)(NSError *);

@end

@interface CKDiscoverUserIdentitiesOperation : CKDatabaseOperation
@property(copy) void (^discoverUserIdentitiesCompletionBlock)(NSError *);
@property(copy) NSArray * userIdentityLookupInfos;
- (instancetype)initWithUserIdentityLookupInfos:(NSArray<CKUserIdentityLookupInfo *> *)lookupInfos;
@end

@interface CKFetchDatabaseChangesOperation : CKDatabaseOperation
@property(copy) void (^fetchDatabaseChangesCompletionBlock)(CKServerChangeToken *, BOOL, NSError *);
@property(retain) CKServerChangeToken * previousServerChangeToken;
- (instancetype)initWithPreviousServerChangeToken:(CKServerChangeToken *)token;
@end

@interface CKFetchRecordChangesOperation : CKDatabaseOperation
@property(retain) CKServerChangeToken * previousServerChangeToken;
@property(retain) id recordZoneID;
- (instancetype)initWithRecordZoneID:(CKRecordZoneID *)zoneID previousServerChangeToken:(CKServerChangeToken *)token;
@end

@interface CKFetchRecordZoneChangesOperation : CKDatabaseOperation
@property(copy) NSDictionary * configurationsByRecordZoneID;
@property(copy) void (^fetchRecordZoneChangesCompletionBlock)(NSError *);
@property(retain) id optionsByRecordZoneID;
@property(copy) NSArray * recordZoneIDs;
- (instancetype)initWithRecordZoneIDs:(NSArray<CKRecordZoneID *> *)zoneIDs configurationsByRecordZoneID:(NSDictionary<CKRecordZoneID *,CKFetchRecordZoneChangesConfiguration *> *)configurations;
- (instancetype)initWithRecordZoneIDs:(NSArray<CKRecordZoneID *> *)zoneIDs optionsByRecordZoneID:(NSDictionary<CKRecordZoneID *,CKFetchRecordZoneChangesOptions *> *)options;
@end

@interface CKFetchRecordZonesOperation : CKDatabaseOperation
@property(copy) void (^fetchRecordZonesCompletionBlock)(NSDictionary *, NSError *);
@property(copy) void (^perRecordZoneCompletionBlock)(CKRecordZoneID *, CKRecordZone *, NSError *);
@property(copy) NSArray * recordZoneIDs;
+ (instancetype)fetchAllRecordZonesOperation;
- (instancetype)initWithRecordZoneIDs:(NSArray<CKRecordZoneID *> *)recordZoneIDs;
@end

@interface CKFetchRecordsOperation : CKDatabaseOperation
@property(copy) void (^fetchRecordsCompletionBlock)(NSDictionary *, NSError *);
@property(copy) void (^perRecordCompletionBlock)(CKRecord *, CKRecordID *, NSError *);
@property(copy) NSArray * recordIDs;
- (instancetype)initWithRecordIDs:(NSArray<CKRecordID *> *)recordIDs;
+ (instancetype)fetchCurrentUserRecordOperation;
@end

@interface CKFetchShareMetadataOperation : CKDatabaseOperation
@property(copy) void (^fetchShareMetadataCompletionBlock)(NSError *);
@property(copy) void (^perShareMetadataBlock)(NSURL *, CKShareMetadata *, NSError *);
@property(copy) NSArray * shareURLs;
- (instancetype)initWithShareURLs:(NSArray<NSURL *> *)shareURLs;
@end

@interface CKFetchShareParticipantsOperation : CKDatabaseOperation
@property(copy) void (^fetchShareParticipantsCompletionBlock)(NSError *);
@property(copy) void (^perShareParticipantCompletionBlock)(CKUserIdentityLookupInfo *, CKShareParticipant *, NSError *);
@property(copy) NSArray * userIdentityLookupInfos;
- (instancetype)initWithUserIdentityLookupInfos:(NSArray<CKUserIdentityLookupInfo *> *)lookupInfos;
@end

@interface CKFetchSubscriptionsOperation : CKDatabaseOperation
@property(copy) void (^fetchSubscriptionCompletionBlock)(NSDictionary *, NSError *);
@property(copy) void (^perSubscriptionCompletionBlock)(CKSubscriptionID, CKSubscription *, NSError *);
@property(copy) NSArray * subscriptionIDs;
+ (instancetype)fetchAllSubscriptionsOperation;
- (instancetype)initWithSubscriptionIDs:(NSArray<CKSubscriptionID> *)subscriptionIDs;
@end

@interface CKFetchWebAuthTokenOperation : CKDatabaseOperation
@property(retain) id APIToken;
@property(copy) void (^fetchWebAuthTokenCompletionBlock)(NSString *, NSError *);
- (instancetype)initWithAPIToken:(NSString *)APIToken;
@end

@interface CKModifyRecordZonesOperation : CKDatabaseOperation
@property(copy) void (^modifyRecordZonesCompletionBlock)(NSArray *, NSArray *, NSError *);
@property(copy) void (^perRecordZoneDeleteBlock)(CKRecordZoneID *, NSError *);
@property(copy) void (^perRecordZoneSaveBlock)(CKRecordZoneID *, CKRecordZone *, NSError *);
@property(copy) NSArray * recordZoneIDsToDelete;
@property(copy) NSArray * recordZonesToSave;
- (instancetype)initWithRecordZonesToSave:(NSArray<CKRecordZone *> *)zones recordZoneIDsToDelete:(NSArray<CKRecordZoneID *> *)zoneIDs;
@end

@interface CKModifyRecordsOperation : CKDatabaseOperation
@property(copy) void (^modifyRecordsCompletionBlock)(NSArray *, NSArray *, NSError *);
@property(copy) void (^perRecordDeleteBlock)(CKRecordID *, NSError *);
@property(copy) void (^perRecordSaveBlock)(CKRecordID *, CKRecord *, NSError *);
@property(copy) NSArray * recordIDsToDelete;
@property(copy) NSArray * recordsToSave;
- (instancetype)initWithRecordsToSave:(NSArray<CKRecord *> *)records recordIDsToDelete:(NSArray<CKRecordID *> *)recordIDs;
@end

@interface CKModifySubscriptionsOperation : CKDatabaseOperation
@property(copy) void (^modifySubscriptionsCompletionBlock)(NSArray *, NSArray *, NSError *);
@property(copy) void (^perSubscriptionDeleteBlock)(CKSubscriptionID, NSError *);
@property(copy) void (^perSubscriptionSaveBlock)(CKSubscriptionID, CKSubscription *, NSError *);
@property(copy) NSArray * subscriptionIDsToDelete;
@property(copy) NSArray * subscriptionsToSave;
- (instancetype)initWithSubscriptionsToSave:(NSArray<CKSubscription *> *)subscriptions subscriptionIDsToDelete:(NSArray<CKSubscriptionID> *)subscriptionIDs;
@end

@interface CKQueryOperation : CKDatabaseOperation
@property(retain) CKQueryCursor * cursor;
@property(retain) CKQuery * query;
@property(copy) void (^queryCompletionBlock)(CKQueryCursor *, NSError *);
- (instancetype)initWithQuery:(CKQuery *)query;
- (instancetype)initWithCursor:(CKQueryCursor *)cursor;
@end

@interface CKQuerySubscription : CKSubscription
@property(retain) NSPredicate * predicate;
@property(assign) NSInteger querySubscriptionOptions;
@property(copy) NSString * recordType;
@property(retain) CKRecordZoneID * zoneID;
- (instancetype)initWithRecordType:(CKRecordType)recordType predicate:(NSPredicate *)predicate options:(CKQuerySubscriptionOptions)options;
- (instancetype)initWithRecordType:(CKRecordType)recordType predicate:(NSPredicate *)predicate subscriptionID:(CKSubscriptionID)subscriptionID options:(CKQuerySubscriptionOptions)options;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
@end

@interface CKRecordZoneSubscription : CKSubscription
@property(copy) NSString * recordType;
@property(retain) CKRecordZoneID * zoneID;
- (instancetype)initWithZoneID:(CKRecordZoneID *)zoneID;
- (instancetype)initWithZoneID:(CKRecordZoneID *)zoneID subscriptionID:(CKSubscriptionID)subscriptionID;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
@end

@interface CKShareRequestAccessOperation : CKDatabaseOperation
@property(copy) NSArray * shareURLs;
- (instancetype)initWithShareURLs:(NSArray<NSURL *> *)shareURLs;
@end

@interface CKAcceptSharesOperation : CKDatabaseOperation
@property(copy) void (^acceptSharesCompletionBlock)(NSError *);
@property(copy) void (^perShareCompletionBlock)(CKShareMetadata *, CKShare *, NSError *);
@property(copy) NSArray * shareMetadatas;
- (instancetype)initWithShareMetadatas:(NSArray<CKShareMetadata *> *)shareMetadatas;
@end

