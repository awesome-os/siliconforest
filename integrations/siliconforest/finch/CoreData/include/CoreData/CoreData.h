/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class NSAtomicStoreCacheNode, NSConstraintConflict, NSCoreDataCoreSpotlightDelegate, NSEntityDescription, NSEntityMapping, NSEntityMigrationPolicy, NSFetchIndexDescription, NSFetchIndexElementDescription, NSFetchRequestExpression, NSFetchedResultsController, NSIncrementalStoreNode, NSManagedObject, NSManagedObjectContext, NSManagedObjectID, NSManagedObjectModel, NSManagedObjectModelReference, NSMappingModel, NSMergeConflict, NSMergePolicy, NSMigrationManager, NSMigrationStage, NSPersistentCloudKitContainerEvent, NSPersistentCloudKitContainerOptions, NSPersistentContainer, NSPersistentHistoryChange, NSPersistentHistoryToken, NSPersistentHistoryTransaction, NSPersistentStore, NSPersistentStoreCoordinator, NSPersistentStoreDescription, NSPersistentStoreRequest, NSPersistentStoreResult, NSPropertyDescription, NSPropertyMapping, NSQueryGenerationToken, NSRelationshipDescription, NSSaveChangesRequest, NSStagedMigrationManager, NSAsynchronousFetchRequest, NSAtomicStore, NSAttributeDescription, NSBatchDeleteRequest, NSBatchDeleteResult, NSBatchInsertRequest, NSBatchInsertResult, NSBatchUpdateRequest, NSBatchUpdateResult, NSCompositeAttributeDescription, NSCustomMigrationStage, NSDerivedAttributeDescription, NSExpressionDescription, NSFetchRequest, NSFetchedPropertyDescription, NSIncrementalStore, NSLightweightMigrationStage, NSPersistentCloudKitContainer, NSPersistentCloudKitContainerEventRequest, NSPersistentCloudKitContainerEventResult, NSPersistentHistoryChangeRequest, NSPersistentHistoryResult, NSPersistentStoreAsynchronousResult, NSAsynchronousFetchResult;
@class NSAsynchronousFetchResult<__covariant ResultType>;
@class NSFetchRequest<__covariant ResultType>;
@class NSIndexPath,CSSearchableItemAttributeSet,CSSearchableIndex,CKRecord,CKRecordID,CKRecordZoneID,CKShare,CKContainer,CKShareMetadata;
/* SPDX-License-Identifier: MIT */
#import <CoreData/Numeric.h>
typedef NSUInteger NSAttributeType;
typedef NSUInteger NSBatchDeleteRequestResultType;
typedef NSUInteger NSBatchInsertRequestResultType;
typedef NSUInteger NSBatchUpdateRequestResultType;
typedef NSUInteger NSDeleteRule;
typedef NSUInteger NSEntityMappingType;
typedef NSUInteger NSFetchIndexElementType;
typedef NSUInteger NSFetchRequestResultType;
typedef NSUInteger NSManagedObjectContextConcurrencyType;
typedef NSUInteger NSMergePolicyType;
typedef NSInteger NSPersistentCloudKitContainerEventResultType;
typedef NSInteger NSPersistentCloudKitContainerEventType;
typedef NSUInteger NSPersistentCloudKitContainerSchemaInitializationOptions;
typedef NSInteger NSPersistentHistoryChangeType;
typedef NSInteger NSPersistentHistoryResultType;
typedef NSUInteger NSPersistentStoreRequestType;
typedef NSUInteger NSSnapshotEventType;
typedef NSUInteger NSFetchedResultsChangeType;
@protocol NSFetchRequestResult <NSObject>
@end
@protocol NSFetchedResultsSectionInfo <NSObject>
- (NSString *)name;
- (NSString *)indexTitle;
- (NSUInteger)numberOfObjects;
- (NSArray *)objects;
@end
@protocol NSFetchedResultsControllerDelegate <NSObject>
@optional
- (void)controllerWillChangeContent:(NSFetchedResultsController *)controller;
- (void)controllerDidChangeContent:(NSFetchedResultsController *)controller;
- (void)controller:(NSFetchedResultsController *)controller didChangeObject:(id)object atIndexPath:(NSIndexPath *)oldPath forChangeType:(NSFetchedResultsChangeType)type newIndexPath:(NSIndexPath *)newPath;
@end
extern id NSErrorMergePolicy,NSRollbackMergePolicy,NSOverwriteMergePolicy,NSMergeByPropertyObjectTrumpMergePolicy,NSMergeByPropertyStoreTrumpMergePolicy;

typedef NSInteger CKDatabaseScope;
typedef NSUInteger NSKeyValueSetMutationKind;
#ifndef NS_NOESCAPE
#define NS_NOESCAPE __attribute__((noescape))
#endif
#ifndef NS_SWIFT_SENDABLE
#define NS_SWIFT_SENDABLE
#endif
#import <Foundation/NSXMLDocument.h>
#import <Foundation/NSXMLElement.h>
@interface NSKeyedUnarchiver (SFFinchCoreDataSecureCoding)
@property BOOL requiresSecureCoding;
@end
#define NSKeyedArchiveRootObjectKey @"root"
/* Public static expression-type value (objc2-core-data0.3.2 fact). */
#define NSFetchRequestExpressionType ((NSExpressionType)50)

extern NSString * const NSInMemoryStoreType;
extern NSString * const NSBinaryStoreType;
extern NSString * const NSXMLStoreType;
extern NSString * const NSSQLiteStoreType;
extern NSString * const NSSQLiteErrorDomain;
extern NSString * const NSStoreTypeKey;
extern NSString * const NSStoreUUIDKey;
extern NSString * const NSStoreModelVersionHashesKey;
extern NSString * const NSStoreModelVersionIdentifiersKey;
extern NSString * const NSInsertedObjectsKey;
extern NSString * const NSUpdatedObjectsKey;
extern NSString * const NSUpdatedObjectIDsKey;
extern NSString * const NSDeletedObjectsKey;
extern NSString * const NSRefreshedObjectsKey;
extern NSString * const NSInvalidatedObjectsKey;
extern NSString * const NSInvalidatedAllObjectsKey;
extern NSString * const NSManagedObjectContextDidSaveNotification;
extern NSString * const NSManagedObjectContextDidSaveObjectIDsNotification;
extern NSString * const NSManagedObjectContextObjectsDidChangeNotification;
extern NSString * const NSManagedObjectContextWillSaveNotification;
extern NSString * const NSManagedObjectContextDidMergeChangesObjectIDsNotification;
extern NSString * const NSPersistentStoreCoordinatorStoresDidChangeNotification;
extern NSString * const NSPersistentStoreRemoteChangeNotification;
extern NSString * const NSPersistentHistoryTokenKey;
extern NSString * const NSPersistentHistoryTrackingKey;
extern NSString * const NSPersistentStoreFileProtectionKey;
extern NSString * const NSPersistentStoreRemoteChangeNotificationPostOptionKey;
extern NSString * const NSPersistentStoreStagedMigrationManagerOptionKey;
extern NSString * const NSSQLitePragmasOption;
extern NSString * const NSPersistentStoreTimeoutOption;
extern NSString * const NSSQLiteAnalyzeOption;
extern NSString * const NSSQLiteManualVacuumOption;
extern NSString * const NSReadOnlyPersistentStoreOption;
extern NSString * const NSMigratePersistentStoresAutomaticallyOption;
extern NSString * const NSInferMappingModelAutomaticallyOption;
extern NSString * const NSIgnorePersistentStoreVersioningOption;
extern NSString * const NSStoreModelVersionHashesVersionKey;
extern NSString * const NSCoreDataCoreSpotlightDelegateIndexDidUpdateNotification;

@interface NSAtomicStoreCacheNode : NSObject
- (instancetype)initWithObjectID:(NSManagedObjectID *)moid;
- (NSManagedObjectID *)objectID;
- (NSMutableDictionary *)propertyCache;
- (void)setPropertyCache:(NSMutableDictionary *)cache;
- (id)valueForKey:(NSString *)key;
- (void)setValue:(id)value forKey:(NSString *)key;
@end

@interface NSConstraintConflict : NSObject
- (instancetype)init;
- (instancetype)initWithConstraint:(NSArray *)constraint databaseObject:(NSManagedObject *)databaseObject databaseSnapshot:(NSDictionary *)databaseSnapshot conflictingObjects:(NSArray *)objects conflictingSnapshots:(NSArray *)snapshots;
- (NSArray *)constraint;
- (NSDictionary *)constraintValues;
- (NSManagedObject *)databaseObject;
- (NSDictionary *)databaseSnapshot;
- (NSArray *)conflictingObjects;
- (NSArray *)conflictingSnapshots;
@end

@interface NSCoreDataCoreSpotlightDelegate : NSObject
- (instancetype)initForStoreWithDescription:(NSPersistentStoreDescription *)description coordinator:(NSPersistentStoreCoordinator *)coordinator;
- (instancetype)initForStoreWithDescription:(NSPersistentStoreDescription *)description model:(NSManagedObjectModel *)model;
- (BOOL)isIndexingEnabled;
- (NSString *)domainIdentifier;
- (NSString *)indexName;
- (void)startSpotlightIndexing;
- (void)stopSpotlightIndexing;
- (void)deleteSpotlightIndexWithCompletionHandler:(void (^)(NSError *))completionHandler;
- (CSSearchableItemAttributeSet *)attributeSetForObject:(NSManagedObject *)object;
- (void)searchableIndex:(CSSearchableIndex *)index reindexAllSearchableItemsWithAcknowledgementHandler:(void (^)(void))handler;
- (void)searchableIndex:(CSSearchableIndex *)index reindexSearchableItemsWithIdentifiers:(NSArray *)identifiers acknowledgementHandler:(void (^)(void))handler;
@property(getter=isIndexingEnabled,readonly) BOOL indexingEnabled;
@end

@interface NSEntityDescription : NSObject
+ (NSEntityDescription *)entityForName:(NSString *)name inManagedObjectContext:(NSManagedObjectContext *)context;
+ (NSManagedObject *)insertNewObjectForEntityForName:(NSString *)name inManagedObjectContext:(NSManagedObjectContext *)context;
- (NSString *)name;
- (void)setName:(NSString *)value;
- (NSString *)managedObjectClassName;
- (void)setManagedObjectClassName:(NSString *)value;
- (NSManagedObjectModel *)managedObjectModel;
- (NSEntityDescription *)superentity;
- (void)setSuperentity:(NSEntityDescription *)value;
- (NSArray *)subentities;
- (void)setSubentities:(NSArray *)value;
- (NSArray *)properties;
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state objects:(id __unsafe_unretained [])buffer count:(NSUInteger)length;
- (void)setProperties:(NSArray *)value;
- (NSDictionary *)propertiesByName;
- (NSDictionary *)attributesByName;
- (NSDictionary *)relationshipsByName;
- (NSDictionary *)subentitiesByName;
- (NSArray *)relationshipsWithDestinationEntity:(NSEntityDescription *)entity;
- (BOOL)isAbstract;
- (void)setAbstract:(BOOL)value;
- (NSDictionary *)userInfo;
- (NSString *)description;
- (void)setUserInfo:(NSDictionary *)value;
- (NSString *)versionHashModifier;
- (void)setVersionHashModifier:(NSString *)value;
- (NSString *)renamingIdentifier;
- (void)setRenamingIdentifier:(NSString *)value;
- (NSArray *)indexes;
- (void)setIndexes:(NSArray *)value;
- (NSArray *)compoundIndexes;
- (void)setCompoundIndexes:(NSArray *)value;
- (NSArray *)uniquenessConstraints;
- (void)setUniquenessConstraints:(NSArray *)value;
- (NSExpression *)coreSpotlightDisplayNameExpression;
- (void)setCoreSpotlightDisplayNameExpression:(NSExpression *)value;
- (BOOL)isKindOfEntity:(NSEntityDescription *)entity;
- (NSData *)versionHash;
- (id)copyWithZone:(NSZone *)zone;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@property(getter=isAbstract) BOOL abstract;
@end

@interface NSEntityMapping : NSObject
- (NSString *)name;
- (NSString *)description;
- (void)setName:(NSString *)value;
- (NSEntityMappingType)mappingType;
- (void)setMappingType:(NSEntityMappingType)value;
- (NSString *)sourceEntityName;
- (void)setSourceEntityName:(NSString *)value;
- (NSData *)sourceEntityVersionHash;
- (void)setSourceEntityVersionHash:(NSData *)value;
- (NSString *)destinationEntityName;
- (void)setDestinationEntityName:(NSString *)value;
- (NSData *)destinationEntityVersionHash;
- (void)setDestinationEntityVersionHash:(NSData *)value;
- (NSArray *)attributeMappings;
- (void)setAttributeMappings:(NSArray *)value;
- (NSArray *)relationshipMappings;
- (void)setRelationshipMappings:(NSArray *)value;
- (NSExpression *)sourceExpression;
- (void)setSourceExpression:(NSExpression *)value;
- (NSDictionary *)userInfo;
- (void)setUserInfo:(NSDictionary *)value;
- (NSString *)entityMigrationPolicyClassName;
- (void)setEntityMigrationPolicyClassName:(NSString *)value;
@end

@interface NSEntityMigrationPolicy : NSObject
- (BOOL)beginEntityMapping:(NSEntityMapping *)mapping manager:(NSMigrationManager *)manager error:(NSError **)error;
- (BOOL)createDestinationInstancesForSourceInstance:(NSManagedObject *)sInstance entityMapping:(NSEntityMapping *)mapping manager:(NSMigrationManager *)manager error:(NSError **)error;
- (BOOL)endInstanceCreationForEntityMapping:(NSEntityMapping *)mapping manager:(NSMigrationManager *)manager error:(NSError **)error;
- (BOOL)createRelationshipsForDestinationInstance:(NSManagedObject *)dInstance entityMapping:(NSEntityMapping *)mapping manager:(NSMigrationManager *)manager error:(NSError **)error;
- (BOOL)endRelationshipCreationForEntityMapping:(NSEntityMapping *)mapping manager:(NSMigrationManager *)manager error:(NSError **)error;
- (BOOL)performCustomValidationForEntityMapping:(NSEntityMapping *)mapping manager:(NSMigrationManager *)manager error:(NSError **)error;
- (BOOL)endEntityMapping:(NSEntityMapping *)mapping manager:(NSMigrationManager *)manager error:(NSError **)error;
@end

@interface NSFetchIndexDescription : NSObject
- (instancetype)initWithName:(NSString *)name elements:(NSArray *)elements;
- (NSString *)name;
- (void)setName:(NSString *)value;
- (NSArray *)elements;
- (void)setElements:(NSArray *)value;
- (NSEntityDescription *)entity;
- (NSPredicate *)partialIndexPredicate;
- (void)setPartialIndexPredicate:(NSPredicate *)value;
- (id)copyWithZone:(NSZone *)zone;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@end

@interface NSFetchIndexElementDescription : NSObject
- (instancetype)initWithProperty:(NSPropertyDescription *)property collationType:(NSFetchIndexElementType)type;
- (NSPropertyDescription *)property;
- (NSString *)propertyName;
- (NSFetchIndexElementType)collationType;
- (void)setCollationType:(NSFetchIndexElementType)value;
- (BOOL)isAscending;
- (void)setAscending:(BOOL)value;
- (NSFetchIndexDescription *)indexDescription;
- (id)copyWithZone:(NSZone *)zone;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@property(getter=isAscending) BOOL ascending;
@end

@interface NSFetchRequestExpression : NSExpression
+ (BOOL)supportsSecureCoding;
+ (NSExpression *)expressionForFetch:(NSExpression *)fetch context:(NSExpression *)context countOnly:(BOOL)countFlag;
- (NSExpressionType)expressionType;
- (NSExpression *)requestExpression;
- (NSExpression *)contextExpression;
- (BOOL)isCountOnlyRequest;
- (NSString *)description;
- (NSString *)predicateFormat;
- (id)expressionValueWithObject:(id)object context:(NSMutableDictionary *)variables;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@property(getter=isCountOnlyRequest,readonly) BOOL countOnlyRequest;
@end

@interface NSFetchedResultsController : NSObject
- (instancetype)initWithFetchRequest:(NSFetchRequest *)request managedObjectContext:(NSManagedObjectContext *)context sectionNameKeyPath:(NSString *)sectionNameKeyPath cacheName:(NSString *)cacheName;
- (NSFetchRequest *)fetchRequest;
- (NSManagedObjectContext *)managedObjectContext;
- (NSString *)sectionNameKeyPath;
- (NSString *)cacheName;
- (id<NSFetchedResultsControllerDelegate>)delegate;
- (void)setDelegate:(id<NSFetchedResultsControllerDelegate>)value;
- (NSArray *)fetchedObjects;
- (NSArray *)sections;
- (NSArray *)sectionIndexTitles;
- (nullable NSString *)sectionIndexTitleForSectionName:(NSString *)sectionName;
- (NSInteger)sectionForSectionIndexTitle:(NSString *)title atIndex:(NSInteger)sectionIndex;
+ (void)deleteCacheWithName:(NSString *)name;
- (BOOL)performFetch:(NSError **)error;
- (id)objectAtIndexPath:(NSIndexPath *)indexPath;
- (NSIndexPath *)indexPathForObject:(id)object;
- (void)dealloc;
@end

@interface NSIncrementalStoreNode : NSObject
- (instancetype)initWithObjectID:(NSManagedObjectID *)objectID withValues:(NSDictionary *)values version:(uint64_t)version;
- (void)updateWithValues:(NSDictionary *)values version:(uint64_t)version;
- (NSManagedObjectID *)objectID;
- (uint64_t)version;
- (id)valueForPropertyDescription:(NSPropertyDescription *)property;
- (NSString *)description;
@end

@interface NSManagedObject : NSObject <NSFetchRequestResult>
+ (BOOL)contextShouldIgnoreUnmodeledPropertyChanges;
+ (NSEntityDescription *)entity;
+ (NSFetchRequest *)fetchRequest;
+ (BOOL)resolveInstanceMethod:(SEL)selector;
- (instancetype)initWithEntity:(NSEntityDescription *)entity insertIntoManagedObjectContext:(NSManagedObjectContext *)context;
- (instancetype)init;
- (instancetype)initWithContext:(NSManagedObjectContext *)context;
- (NSManagedObjectContext *)managedObjectContext;
- (NSEntityDescription *)entity;
- (NSManagedObjectID *)objectID;
- (BOOL)isInserted;
- (BOOL)isUpdated;
- (BOOL)isDeleted;
- (BOOL)hasChanges;
- (BOOL)hasPersistentChangedValues;
- (BOOL)isFault;
- (NSUInteger)faultingState;
- (BOOL)hasFaultForRelationshipNamed:(NSString *)key;
- (NSArray *)objectIDsForRelationshipNamed:(NSString *)key;
- (id)valueForKey:(NSString *)key;
- (void)setValue:(id)value forKey:(NSString *)key;
- (id)primitiveValueForKey:(NSString *)key;
- (void)setPrimitiveValue:(id)value forKey:(NSString *)key;
- (NSDictionary *)committedValuesForKeys:(NSArray *)keys;
- (NSDictionary *)changedValues;
- (NSDictionary *)changedValuesForCurrentEvent;
- (BOOL)validateValue:(id *)value forKey:(NSString *)key error:(NSError **)error;
- (BOOL)validateForInsert:(NSError **)error;
- (BOOL)validateForUpdate:(NSError **)error;
- (BOOL)validateForDelete:(NSError **)error;
- (void)willAccessValueForKey:(NSString *)key;
- (void)didAccessValueForKey:(NSString *)key;
- (void)willChangeValueForKey:(NSString *)key;
- (void)didChangeValueForKey:(NSString *)key;
- (void)willChangeValueForKey:(NSString *)key withSetMutation:(NSKeyValueSetMutationKind)kind usingObjects:(NSSet *)objects;
- (void)didChangeValueForKey:(NSString *)key withSetMutation:(NSKeyValueSetMutationKind)kind usingObjects:(NSSet *)objects;
- (void)awakeFromFetch;
- (void)awakeFromInsert;
- (void)awakeFromSnapshotEvents:(NSSnapshotEventType)flags;
- (void)prepareForDeletion;
- (void)willSave;
- (void)didSave;
- (void)willTurnIntoFault;
- (void)didTurnIntoFault;
- (void)setObservationInfo:(void *)info;
- (void *)observationInfo;
@property(getter=isInserted,readonly) BOOL inserted;
@property(getter=isUpdated,readonly) BOOL updated;
@property(getter=isDeleted,readonly) BOOL deleted;
@property(getter=isFault,readonly) BOOL fault;
@end

@interface NSManagedObjectContext : NSObject
- (instancetype)init;
+ (instancetype)new;
- (instancetype)initWithConcurrencyType:(NSManagedObjectContextConcurrencyType)type;
- (NSPersistentStoreCoordinator *)persistentStoreCoordinator;
- (void)setPersistentStoreCoordinator:(NSPersistentStoreCoordinator *)coordinator;
- (NSManagedObjectContext *)parentContext;
- (void)setParentContext:(NSManagedObjectContext *)context;
- (NSString *)name;
- (void)setName:(NSString *)value;
- (NSUndoManager *)undoManager;
- (void)setUndoManager:(NSUndoManager *)value;
- (NSManagedObjectContextConcurrencyType)concurrencyType;
- (NSMutableDictionary *)userInfo;
- (BOOL)hasChanges;
- (NSSet *)insertedObjects;
- (NSSet *)updatedObjects;
- (NSSet *)deletedObjects;
- (NSSet *)registeredObjects;
- (BOOL)propagatesDeletesAtEndOfEvent;
- (void)setPropagatesDeletesAtEndOfEvent:(BOOL)value;
- (BOOL)retainsRegisteredObjects;
- (void)setRetainsRegisteredObjects:(BOOL)value;
- (BOOL)shouldDeleteInaccessibleFaults;
- (void)setShouldDeleteInaccessibleFaults:(BOOL)value;
- (NSTimeInterval)stalenessInterval;
- (void)setStalenessInterval:(NSTimeInterval)value;
- (id)mergePolicy;
- (void)setMergePolicy:(id)value;
- (NSQueryGenerationToken *)queryGenerationToken;
- (BOOL)setQueryGenerationFromToken:(NSQueryGenerationToken *)token error:(NSError **)error;
- (void)performBlock:(void (^)(void))block;
- (void)performBlockAndWait:(void (^NS_NOESCAPE NS_SWIFT_SENDABLE)(void))block;
- (void)lock;
- (void)unlock;
- (BOOL)tryLock;
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context;
- (void)undo;
- (void)redo;
- (BOOL)shouldHandleInaccessibleFault:(NSManagedObject *)fault forObjectID:(NSManagedObjectID *)objectID triggeredByProperty:(NSPropertyDescription *)property;
- (void)insertObject:(NSManagedObject *)object;
- (void)deleteObject:(NSManagedObject *)object;
- (NSManagedObject *)objectRegisteredForID:(NSManagedObjectID *)objectID;
- (NSManagedObject *)objectWithID:(NSManagedObjectID *)objectID;
- (NSManagedObject *)existingObjectWithID:(NSManagedObjectID *)objectID error:(NSError **)error;
- (NSArray *)executeFetchRequest:(NSFetchRequest *)request error:(NSError **)error;
- (NSUInteger)countForFetchRequest:(NSFetchRequest *)request error:(NSError **)error;
- (NSPersistentStoreResult *)executeRequest:(NSPersistentStoreRequest *)request error:(NSError **)error;
- (BOOL)save:(NSError **)error;
- (void)rollback;
- (void)reset;
- (void)refreshObject:(NSManagedObject *)object mergeChanges:(BOOL)flag;
- (void)refreshAllObjects;
- (void)detectConflictsForObject:(NSManagedObject *)object;
- (void)assignObject:(id)object toPersistentStore:(NSPersistentStore *)store;
- (void)processPendingChanges;
- (void)mergeChangesFromContextDidSaveNotification:(NSNotification *)notification;
+ (void)mergeChangesFromRemoteContextSave:(NSDictionary *)data intoContexts:(NSArray *)contexts;
- (BOOL)obtainPermanentIDsForObjects:(NSArray *)objects error:(NSError **)error;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (void)dealloc;
@end

@interface NSManagedObjectID : NSObject <NSFetchRequestResult,NSCopying>
- (instancetype)init;
- (NSEntityDescription *)entity;
- (NSPersistentStore *)persistentStore;
- (BOOL)isTemporaryID;
- (NSURL *)URIRepresentation;
- (NSString *)description;
- (NSUInteger)hash;
- (BOOL)isEqual:(id)object;
- (id)copyWithZone:(NSZone *)zone;
- (instancetype)cd_init;
@property(getter=isTemporaryID,readonly) BOOL temporaryID;
@end

@interface NSManagedObjectModel : NSObject
- (instancetype)init;
- (instancetype)initWithContentsOfURL:(NSURL *)url;
- (NSArray *)entities;
- (void)setEntities:(NSArray *)entities;
- (NSDictionary *)entitiesByName;
- (NSArray *)configurations;
- (NSArray *)entitiesForConfiguration:(NSString *)configuration;
- (void)setEntities:(NSArray *)entities forConfiguration:(NSString *)configuration;
- (NSDictionary *)fetchRequestTemplatesByName;
- (NSString *)description;
- (void)setFetchRequestTemplate:(NSFetchRequest *)request forName:(NSString *)name;
- (NSFetchRequest *)fetchRequestTemplateForName:(NSString *)name;
- (NSFetchRequest *)fetchRequestFromTemplateWithName:(NSString *)name substitutionVariables:(NSDictionary *)variables;
- (NSDictionary *)localizationDictionary;
- (void)setLocalizationDictionary:(NSDictionary *)value;
- (NSSet *)versionIdentifiers;
- (void)setVersionIdentifiers:(NSSet *)value;
- (NSDictionary *)entityVersionHashesByName;
- (NSData *)versionHash;
- (NSString *)versionChecksum;
- (BOOL)isConfiguration:(NSString *)configuration compatibleWithStoreMetadata:(NSDictionary *)metadata;
- (id)copyWithZone:(NSZone *)zone;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state objects:(id *)buffer count:(NSUInteger)len;
+ (NSManagedObjectModel *)modelByMergingModels:(NSArray *)models;
+ (NSManagedObjectModel *)mergedModelFromBundles:(NSArray *)bundles;
+ (NSManagedObjectModel *)mergedModelFromBundles:(NSArray *)bundles forStoreMetadata:(NSDictionary *)metadata;
+ (NSManagedObjectModel *)modelByMergingModels:(NSArray *)models forStoreMetadata:(NSDictionary *)metadata;
+ (NSDictionary *)checksumsForVersionedModelAtURL:(NSURL *)url error:(NSError **)error;
@end

@interface NSManagedObjectModelReference : NSObject
- (instancetype)initWithModel:(NSManagedObjectModel *)model versionChecksum:(NSString *)checksum;
- (instancetype)initWithFileURL:(NSURL *)url versionChecksum:(NSString *)checksum;
- (instancetype)initWithEntityVersionHashes:(NSDictionary *)hashes inBundle:(NSBundle *)bundle versionChecksum:(NSString *)checksum;
- (instancetype)initWithName:(NSString *)name inBundle:(NSBundle *)bundle versionChecksum:(NSString *)checksum;
- (NSManagedObjectModel *)resolvedModel;
- (NSString *)versionChecksum;
@end

@interface NSMappingModel : NSObject
- (NSString *)description;
+ (NSMappingModel *)mappingModelFromBundles:(NSArray *)bundles forSourceModel:(NSManagedObjectModel *)source destinationModel:(NSManagedObjectModel *)destination;
+ (NSMappingModel *)inferredMappingModelForSourceModel:(NSManagedObjectModel *)source destinationModel:(NSManagedObjectModel *)destination error:(NSError **)error;
- (instancetype)initWithContentsOfURL:(NSURL *)url;
- (NSArray *)entityMappings;
- (void)setEntityMappings:(NSArray *)value;
- (NSDictionary *)entityMappingsByName;
@end

@interface NSMergeConflict : NSObject
- (instancetype)initWithSource:(NSManagedObject *)source newVersion:(NSUInteger)newVersion oldVersion:(NSUInteger)oldVersion cachedSnapshot:(NSDictionary *)cached persistedSnapshot:(NSDictionary *)persisted;
- (NSManagedObject *)sourceObject;
- (NSDictionary *)objectSnapshot;
- (NSDictionary *)cachedSnapshot;
- (NSDictionary *)persistedSnapshot;
- (NSUInteger)newVersionNumber;
- (NSUInteger)oldVersionNumber;
@end

@interface NSMergePolicy : NSObject
+ (void)initialize;
+ (NSMergePolicy *)errorMergePolicy;
+ (NSMergePolicy *)rollbackMergePolicy;
+ (NSMergePolicy *)overwriteMergePolicy;
+ (NSMergePolicy *)mergeByPropertyObjectTrumpMergePolicy;
+ (NSMergePolicy *)mergeByPropertyStoreTrumpMergePolicy;
- (instancetype)initWithMergeType:(NSMergePolicyType)type;
- (NSMergePolicyType)mergeType;
- (BOOL)resolveConflicts:(NSArray *)conflicts error:(NSError **)error;
- (BOOL)resolveOptimisticLockingVersionConflicts:(NSArray<NSMergeConflict *> *)conflicts error:(NSError **)error;
- (BOOL)resolveConstraintConflicts:(NSArray<NSConstraintConflict *> *)conflicts error:(NSError **)error;
@end

@interface NSMigrationManager : NSObject
- (instancetype)initWithSourceModel:(NSManagedObjectModel *)sourceModel destinationModel:(NSManagedObjectModel *)destinationModel;
- (BOOL)migrateStoreFromURL:(NSURL *)sourceURL type:(NSString *)sourceType options:(NSDictionary *)sourceOptions withMappingModel:(NSMappingModel *)mapping toDestinationURL:(NSURL *)destinationURL destinationType:(NSString *)destinationType destinationOptions:(NSDictionary *)destinationOptions error:(NSError **)error;
- (BOOL)usesStoreSpecificMigrationManager;
- (void)setUsesStoreSpecificMigrationManager:(BOOL)value;
- (void)reset;
- (NSMappingModel *)mappingModel;
- (NSManagedObjectModel *)sourceModel;
- (NSManagedObjectModel *)destinationModel;
- (NSManagedObjectContext *)sourceContext;
- (NSManagedObjectContext *)destinationContext;
- (NSEntityDescription *)sourceEntityForEntityMapping:(NSEntityMapping *)mapping;
- (NSEntityDescription *)destinationEntityForEntityMapping:(NSEntityMapping *)mapping;
- (void)associateSourceInstance:(NSManagedObject *)source withDestinationInstance:(NSManagedObject *)destination forEntityMapping:(NSEntityMapping *)mapping;
- (NSArray *)destinationInstancesForSourceRelationshipNamed:(NSString *)name sourceInstances:(id)sources;
- (NSFetchRequest *)fetchRequestForSourceEntityNamed:(NSString *)entityName predicateString:(NSString *)predicateString;
- (NSFetchRequest *)fetchRequestForSourceEntityNamed:(NSString *)entityName predicateString:(NSString *)predicateString includesSubentities:(BOOL)subentities;
- (NSArray *)destinationInstancesForEntityMappingNamed:(NSString *)name sourceInstances:(NSArray *)sources;
- (NSArray *)sourceInstancesForEntityMappingNamed:(NSString *)name destinationInstances:(NSArray *)destinations;
- (NSEntityMapping *)currentEntityMapping;
- (float)migrationProgress;
- (NSDictionary *)userInfo;
- (void)setUserInfo:(NSDictionary *)value;
- (void)cancelMigrationWithError:(NSError *)error;
@end

@interface NSMigrationStage : NSObject
- (NSString *)label;
- (void)setLabel:(NSString *)value;
@end

@interface NSPersistentCloudKitContainerEvent : NSObject
- (id)copyWithZone:(NSZone *)zone;
- (NSUUID *)identifier;
- (NSString *)storeIdentifier;
- (NSPersistentCloudKitContainerEventType)type;
- (NSDate *)startDate;
- (NSDate *)endDate;
- (BOOL)succeeded;
- (NSError *)error;
@end

@interface NSPersistentCloudKitContainerOptions : NSObject
- (instancetype)initWithContainerIdentifier:(NSString *)containerIdentifier;
- (NSString *)containerIdentifier;
- (CKDatabaseScope)databaseScope;
- (void)setDatabaseScope:(CKDatabaseScope)scope;
@end

@interface NSPersistentContainer : NSObject
- (instancetype)init;
+ (NSURL *)defaultDirectoryURL;
+ (instancetype)persistentContainerWithName:(NSString *)name;
+ (instancetype)persistentContainerWithName:(NSString *)name managedObjectModel:(NSManagedObjectModel *)model;
- (instancetype)initWithName:(NSString *)name;
- (instancetype)initWithName:(NSString *)name managedObjectModel:(NSManagedObjectModel *)model;
- (NSString *)name;
- (NSManagedObjectModel *)managedObjectModel;
- (NSPersistentStoreCoordinator *)persistentStoreCoordinator;
- (NSManagedObjectContext *)viewContext;
- (NSArray *)persistentStoreDescriptions;
- (void)setPersistentStoreDescriptions:(NSArray *)descriptions;
- (void)loadPersistentStoresWithCompletionHandler:(void (^)(NSPersistentStoreDescription *, NSError *))block;
- (NSManagedObjectContext *)newBackgroundContext;
- (void)performBackgroundTask:(void (^)(NSManagedObjectContext *))block;
@end

@interface NSPersistentHistoryChange : NSObject
+ (NSEntityDescription *)entityDescriptionWithContext:(NSManagedObjectContext *)context;
+ (NSEntityDescription *)entityDescription;
+ (NSFetchRequest *)fetchRequest;
- (int64_t)changeID;
- (NSManagedObjectID *)changedObjectID;
- (NSPersistentHistoryChangeType)changeType;
- (NSDictionary *)tombstone;
- (NSPersistentHistoryTransaction *)transaction;
- (NSSet *)updatedProperties;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface NSPersistentHistoryToken : NSObject
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@end

@interface NSPersistentHistoryTransaction : NSObject
+ (NSEntityDescription *)entityDescriptionWithContext:(NSManagedObjectContext *)context;
+ (NSEntityDescription *)entityDescription;
+ (NSFetchRequest *)fetchRequest;
- (NSDate *)timestamp;
- (NSArray *)changes;
- (int64_t)transactionNumber;
- (NSString *)storeID;
- (NSString *)bundleID;
- (NSString *)processID;
- (NSString *)contextName;
- (NSString *)author;
- (NSPersistentHistoryToken *)token;
- (NSNotification *)objectIDNotification;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface NSPersistentStore : NSObject
+ (NSDictionary *)metadataForPersistentStoreWithURL:(NSURL *)url error:(NSError **)error;
+ (BOOL)setMetadata:(NSDictionary *)metadata forPersistentStoreWithURL:(NSURL *)url error:(NSError **)error;
+ (Class)migrationManagerClass;
- (instancetype)initWithPersistentStoreCoordinator:(NSPersistentStoreCoordinator *)coordinator configurationName:(NSString *)name URL:(NSURL *)url options:(NSDictionary *)options;
- (BOOL)loadMetadata:(NSError **)error;
- (NSPersistentStoreCoordinator *)persistentStoreCoordinator;
- (NSString *)configurationName;
- (NSDictionary *)options;
- (NSURL *)URL;
- (void)setURL:(NSURL *)url;
- (NSString *)identifier;
- (void)setIdentifier:(NSString *)identifier;
- (NSString *)type;
- (BOOL)isReadOnly;
- (void)setReadOnly:(BOOL)value;
- (instancetype)init;
- (NSDictionary *)metadata;
- (void)setMetadata:(NSDictionary *)metadata;
- (NSCoreDataCoreSpotlightDelegate *)coreSpotlightExporter;
- (void)didAddToPersistentStoreCoordinator:(NSPersistentStoreCoordinator *)coordinator;
- (void)willRemoveFromPersistentStoreCoordinator:(NSPersistentStoreCoordinator *)coordinator;
- (void)dealloc;
@property(getter=isReadOnly) BOOL readOnly;
@end

@interface NSPersistentStoreCoordinator : NSObject
- (instancetype)init;
- (instancetype)initWithManagedObjectModel:(NSManagedObjectModel *)model;
- (NSManagedObjectModel *)managedObjectModel;
- (NSArray *)persistentStores;
- (NSString *)name;
- (void)setName:(NSString *)value;
- (void)lock;
- (void)unlock;
- (NSPersistentStore *)persistentStoreForURL:(NSURL *)url;
- (NSURL *)URLForPersistentStore:(NSPersistentStore *)store;
- (BOOL)setURL:(NSURL *)url forPersistentStore:(NSPersistentStore *)store;
- (NSPersistentStore *)addPersistentStoreWithType:(NSString *)type configuration:(NSString *)configuration URL:(NSURL *)url options:(NSDictionary *)options error:(NSError **)error;
- (void)addPersistentStoreWithDescription:(NSPersistentStoreDescription *)description completionHandler:(void (^)(NSPersistentStoreDescription *, NSError *))block;
- (BOOL)removePersistentStore:(NSPersistentStore *)store error:(NSError **)error;
- (void)setMetadata:(NSDictionary *)metadata forPersistentStore:(NSPersistentStore *)store;
- (NSDictionary *)metadataForPersistentStore:(NSPersistentStore *)store;
- (NSManagedObjectID *)managedObjectIDForURIRepresentation:(NSURL *)url;
- (id)executeRequest:(NSPersistentStoreRequest *)request withContext:(NSManagedObjectContext *)context error:(NSError **)error;
+ (NSDictionary *)registeredStoreTypes;
+ (void)registerStoreClass:(Class)storeClass forStoreType:(NSString *)type;
+ (NSDictionary *)metadataForPersistentStoreOfType:(NSString *)type URL:(NSURL *)url options:(NSDictionary *)options error:(NSError **)error;
+ (BOOL)setMetadata:(NSDictionary *)metadata forPersistentStoreOfType:(NSString *)type URL:(NSURL *)url options:(NSDictionary *)options error:(NSError **)error;
+ (NSDictionary *)metadataForPersistentStoreOfType:(NSString *)type URL:(NSURL *)url error:(NSError **)error;
+ (BOOL)setMetadata:(NSDictionary *)metadata forPersistentStoreOfType:(NSString *)type URL:(NSURL *)url error:(NSError **)error;
- (BOOL)tryLock;
- (NSManagedObjectID *)managedObjectIDFromUTF8String:(const char *)utf8string length:(NSUInteger)length;
+ (NSDictionary *)metadataForPersistentStoreWithURL:(NSURL *)url error:(NSError **)error;
+ (NSManagedObjectModel *)cachedModelForPersistentStoreAtURL:(NSURL *)url options:(NSDictionary *)options error:(NSError **)error;
+ (NSDictionary *)elementsDerivedFromExternalRecordURL:(NSURL *)url;
- (NSPersistentStore *)importStoreWithIdentifier:(NSString *)identifier fromExternalRecordsDirectory:(NSURL *)directory toURL:(NSURL *)url options:(NSDictionary *)options withType:(NSString *)type error:(NSError **)error;
+ (BOOL)removeUbiquitousContentAndPersistentStoreAtURL:(NSURL *)url options:(NSDictionary *)options error:(NSError **)error;
- (NSPersistentStore *)migratePersistentStore:(NSPersistentStore *)store toURL:(NSURL *)url options:(NSDictionary *)options withType:(NSString *)type error:(NSError **)error;
- (BOOL)destroyPersistentStoreAtURL:(NSURL *)url withType:(NSString *)type options:(NSDictionary *)options error:(NSError **)error;
- (BOOL)replacePersistentStoreAtURL:(NSURL *)destinationURL destinationOptions:(NSDictionary *)destinationOptions withPersistentStoreFromURL:(NSURL *)sourceURL sourceOptions:(NSDictionary *)sourceOptions storeType:(NSString *)type error:(NSError **)error;
- (void)performBlock:(void (^)(void))block;
- (void)performBlockAndWait:(void (^NS_NOESCAPE NS_SWIFT_SENDABLE)(void))block;
- (NSPersistentHistoryToken *)currentPersistentHistoryTokenFromStores:(NSArray *)stores;
- (BOOL)finishDeferredLightweightMigration:(NSError **)error;
- (BOOL)finishDeferredLightweightMigrationTask:(NSError **)error;
@end

@interface NSPersistentStoreDescription : NSObject
+ (instancetype)persistentStoreDescriptionWithURL:(NSURL *)url;
- (NSPersistentCloudKitContainerOptions *)cloudKitContainerOptions;
- (void)setCloudKitContainerOptions:(NSPersistentCloudKitContainerOptions *)options;
- (instancetype)init;
- (NSString *)description;
- (instancetype)initWithURL:(NSURL *)url;
- (NSString *)type;
- (void)setType:(NSString *)value;
- (NSString *)configuration;
- (void)setConfiguration:(NSString *)value;
- (NSURL *)URL;
- (void)setURL:(NSURL *)value;
- (NSDictionary *)options;
- (void)setOption:(NSObject *)option forKey:(NSString *)key;
- (BOOL)isReadOnly;
- (void)setReadOnly:(BOOL)value;
- (NSTimeInterval)timeout;
- (void)setTimeout:(NSTimeInterval)value;
- (NSDictionary *)sqlitePragmas;
- (void)setValue:(NSObject *)value forPragmaNamed:(NSString *)name;
- (BOOL)shouldAddStoreAsynchronously;
- (void)setShouldAddStoreAsynchronously:(BOOL)value;
- (BOOL)shouldMigrateStoreAutomatically;
- (void)setShouldMigrateStoreAutomatically:(BOOL)value;
- (BOOL)shouldInferMappingModelAutomatically;
- (void)setShouldInferMappingModelAutomatically:(BOOL)value;
- (id)copyWithZone:(NSZone *)zone;
@property(getter=isReadOnly) BOOL readOnly;
@end

@interface NSPersistentStoreRequest : NSObject
- (NSArray *)affectedStores;
- (void)setAffectedStores:(NSArray *)stores;
- (NSPersistentStoreRequestType)requestType;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface NSPersistentStoreResult : NSObject

@end

@interface NSPropertyDescription : NSObject
- (NSString *)name;
- (void)setName:(NSString *)name;
- (NSEntityDescription *)entity;
- (BOOL)isOptional;
- (void)setOptional:(BOOL)value;
- (BOOL)isTransient;
- (void)setTransient:(BOOL)value;
- (NSDictionary *)userInfo;
- (void)setUserInfo:(NSDictionary *)value;
- (BOOL)isIndexed;
- (void)setIndexed:(BOOL)value;
- (BOOL)isIndexedBySpotlight;
- (void)setIndexedBySpotlight:(BOOL)value;
- (BOOL)isStoredInExternalRecord;
- (void)setStoredInExternalRecord:(BOOL)value;
- (NSString *)versionHashModifier;
- (void)setVersionHashModifier:(NSString *)value;
- (NSString *)renamingIdentifier;
- (void)setRenamingIdentifier:(NSString *)value;
- (NSArray *)validationPredicates;
- (NSArray *)validationWarnings;
- (void)setValidationPredicates:(NSArray *)predicates withValidationWarnings:(NSArray *)warnings;
- (NSData *)versionHash;
- (NSString *)description;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (id)copyWithZone:(NSZone *)zone;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@property(getter=isOptional) BOOL optional;
@property(getter=isTransient) BOOL transient;
@property(getter=isIndexed) BOOL indexed;
@property(getter=isIndexedBySpotlight) BOOL indexedBySpotlight;
@property(getter=isStoredInExternalRecord) BOOL storedInExternalRecord;
@end

@interface NSPropertyMapping : NSObject
- (NSString *)description;
- (NSString *)name;
- (void)setName:(NSString *)value;
- (NSExpression *)valueExpression;
- (void)setValueExpression:(NSExpression *)value;
- (NSDictionary *)userInfo;
- (void)setUserInfo:(NSDictionary *)value;
@end

@interface NSQueryGenerationToken : NSObject
+ (NSQueryGenerationToken *)currentQueryGenerationToken;
- (id)copyWithZone:(NSZone *)zone;
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@end

@interface NSRelationshipDescription : NSPropertyDescription
- (NSEntityDescription *)destinationEntity;
- (void)setDestinationEntity:(NSEntityDescription *)value;
- (NSRelationshipDescription *)inverseRelationship;
- (void)setInverseRelationship:(NSRelationshipDescription *)value;
- (NSUInteger)maxCount;
- (void)setMaxCount:(NSUInteger)value;
- (NSUInteger)minCount;
- (void)setMinCount:(NSUInteger)value;
- (NSDeleteRule)deleteRule;
- (NSString *)description;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (void)setDeleteRule:(NSDeleteRule)value;
- (BOOL)isToMany;
- (BOOL)isOrdered;
- (void)setOrdered:(BOOL)value;
- (NSData *)versionHash;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@property(getter=isToMany,readonly) BOOL toMany;
@property(getter=isOrdered) BOOL ordered;
@end

@interface NSSaveChangesRequest : NSPersistentStoreRequest
- (instancetype)initWithInsertedObjects:(NSSet *)insertedObjects updatedObjects:(NSSet *)updatedObjects deletedObjects:(NSSet *)deletedObjects lockedObjects:(NSSet *)lockedObjects;
- (NSPersistentStoreRequestType)requestType;
- (NSSet *)insertedObjects;
- (NSSet *)updatedObjects;
- (NSSet *)deletedObjects;
- (NSSet *)lockedObjects;
- (NSString *)description;
@end

@interface NSStagedMigrationManager : NSObject
- (instancetype)initWithMigrationStages:(NSArray *)stages;
- (NSArray *)stages;
- (NSPersistentContainer *)container;
@end

@interface NSAsynchronousFetchRequest : NSPersistentStoreRequest
- (instancetype)initWithFetchRequest:(NSFetchRequest<id<NSFetchRequestResult>> *)request completionBlock:(void (^)(NSAsynchronousFetchResult<id<NSFetchRequestResult>> *))block;
- (NSFetchRequest *)fetchRequest;
- (void (^)(NSAsynchronousFetchResult *))completionBlock;
- (NSInteger)estimatedResultCount;
- (void)setEstimatedResultCount:(NSInteger)value;
@end

@interface NSAtomicStore : NSPersistentStore
- (NSString *)type;
- (void)setMetadata:(NSDictionary *)metadata;
- (instancetype)initWithPersistentStoreCoordinator:(NSPersistentStoreCoordinator *)coordinator configurationName:(NSString *)configurationName URL:(NSURL *)url options:(NSDictionary *)options;
- (BOOL)load:(NSError **)error;
- (BOOL)save:(NSError **)error;
- (NSAtomicStoreCacheNode *)newCacheNodeForManagedObject:(NSManagedObject *)managedObject;
- (void)updateCacheNode:(NSAtomicStoreCacheNode *)node fromManagedObject:(NSManagedObject *)managedObject;
- (NSSet *)cacheNodes;
- (void)addCacheNodes:(NSSet *)cacheNodes;
- (void)willRemoveCacheNodes:(NSSet *)cacheNodes;
- (NSAtomicStoreCacheNode *)cacheNodeForObjectID:(NSManagedObjectID *)objectID;
- (NSManagedObjectID *)objectIDForEntity:(NSEntityDescription *)entity referenceObject:(id)data;
- (id)newReferenceObjectForManagedObject:(NSManagedObject *)managedObject;
- (id)referenceObjectForObjectID:(NSManagedObjectID *)objectID;
@end

@interface NSAttributeDescription : NSPropertyDescription
- (NSAttributeType)attributeType;
- (void)setAttributeType:(NSAttributeType)value;
- (NSString *)attributeValueClassName;
- (NSString *)description;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (id)copyWithZone:(NSZone *)zone;
- (void)setAttributeValueClassName:(NSString *)value;
- (id)defaultValue;
- (void)setDefaultValue:(id)value;
- (NSString *)valueTransformerName;
- (void)setValueTransformerName:(NSString *)value;
- (BOOL)allowsExternalBinaryDataStorage;
- (void)setAllowsExternalBinaryDataStorage:(BOOL)value;
- (BOOL)preservesValueInHistoryOnDeletion;
- (void)setPreservesValueInHistoryOnDeletion:(BOOL)value;
- (BOOL)allowsCloudEncryption;
- (void)setAllowsCloudEncryption:(BOOL)value;
- (NSData *)versionHash;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@end

@interface NSBatchDeleteRequest : NSPersistentStoreRequest
- (instancetype)init;
- (instancetype)initWithFetchRequest:(NSFetchRequest *)fetch;
- (instancetype)initWithObjectIDs:(NSArray *)objects;
- (NSFetchRequest *)fetchRequest;
- (NSBatchDeleteRequestResultType)resultType;
- (void)setResultType:(NSBatchDeleteRequestResultType)value;
- (NSPersistentStoreRequestType)requestType;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface NSBatchDeleteResult : NSPersistentStoreResult
- (id)result;
- (NSBatchDeleteRequestResultType)resultType;
@end

@interface NSBatchInsertRequest : NSPersistentStoreRequest
+ (instancetype)batchInsertRequestWithEntityName:(NSString *)entityName objects:(NSArray *)dictionaries;
+ (instancetype)batchInsertRequestWithEntityName:(NSString *)entityName dictionaryHandler:(BOOL (^)(NSMutableDictionary<NSString *, id> *))handler;
+ (instancetype)batchInsertRequestWithEntityName:(NSString *)entityName managedObjectHandler:(BOOL (^)(NSManagedObject *))handler;
- (instancetype)init;
- (instancetype)initWithEntityName:(NSString *)entityName objects:(NSArray *)dictionaries;
- (instancetype)initWithEntity:(NSEntityDescription *)entity objects:(NSArray *)dictionaries;
- (instancetype)initWithEntity:(NSEntityDescription *)entity dictionaryHandler:(BOOL (^)(NSMutableDictionary<NSString *, id> *))handler;
- (instancetype)initWithEntity:(NSEntityDescription *)entity managedObjectHandler:(BOOL (^)(NSManagedObject *))handler;
- (instancetype)initWithEntityName:(NSString *)entityName dictionaryHandler:(BOOL (^)(NSMutableDictionary<NSString *, id> *))handler;
- (instancetype)initWithEntityName:(NSString *)entityName managedObjectHandler:(BOOL (^)(NSManagedObject *))handler;
- (NSPersistentStoreRequestType)requestType;
- (NSString *)entityName;
- (NSEntityDescription *)entity;
- (NSArray *)objectsToInsert;
- (void)setObjectsToInsert:(NSArray *)objects;
- (BOOL (^)(NSMutableDictionary<NSString *, id> *))dictionaryHandler;
- (void)setDictionaryHandler:(BOOL (^)(NSMutableDictionary<NSString *, id> *))handler;
- (BOOL (^)(NSManagedObject *))managedObjectHandler;
- (void)setManagedObjectHandler:(BOOL (^)(NSManagedObject *))handler;
- (NSBatchInsertRequestResultType)resultType;
- (void)setResultType:(NSBatchInsertRequestResultType)type;
- (NSString *)description;
@end

@interface NSBatchInsertResult : NSPersistentStoreResult
- (id)result;
- (NSBatchInsertRequestResultType)resultType;
@end

@interface NSBatchUpdateRequest : NSPersistentStoreRequest
+ (instancetype)batchUpdateRequestWithEntityName:(NSString *)name;
- (instancetype)init;
- (instancetype)initWithEntityName:(NSString *)name;
- (instancetype)initWithEntity:(NSEntityDescription *)entity;
- (NSString *)entityName;
- (NSEntityDescription *)entity;
- (NSPredicate *)predicate;
- (void)setPredicate:(NSPredicate *)value;
- (BOOL)includesSubentities;
- (void)setIncludesSubentities:(BOOL)value;
- (NSBatchUpdateRequestResultType)resultType;
- (void)setResultType:(NSBatchUpdateRequestResultType)value;
- (NSDictionary *)propertiesToUpdate;
- (void)setPropertiesToUpdate:(NSDictionary *)value;
- (NSPersistentStoreRequestType)requestType;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface NSBatchUpdateResult : NSPersistentStoreResult
- (id)result;
- (NSBatchUpdateRequestResultType)resultType;
@end

@interface NSCompositeAttributeDescription : NSAttributeDescription
- (instancetype)init;
- (NSAttributeType)attributeType;
- (void)setAttributeType:(NSAttributeType)type;
- (NSString *)attributeValueClassName;
- (NSArray *)elements;
- (NSString *)description;
- (BOOL)isEqual:(id)other;
- (NSData *)versionHash;
- (NSUInteger)hash;
- (void)setElements:(NSArray *)elements;
- (id)copyWithZone:(NSZone *)zone;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@end

@interface NSCustomMigrationStage : NSMigrationStage
- (instancetype)initWithCurrentModelReference:(NSManagedObjectModelReference *)current nextModelReference:(NSManagedObjectModelReference *)next;
- (NSManagedObjectModelReference *)currentModel;
- (NSManagedObjectModelReference *)nextModel;
- (BOOL (^)(NSStagedMigrationManager *, NSCustomMigrationStage *, NSError **))willMigrateHandler;
- (void)setWillMigrateHandler:(BOOL (^)(NSStagedMigrationManager *, NSCustomMigrationStage *, NSError **))block;
- (BOOL (^)(NSStagedMigrationManager *, NSCustomMigrationStage *, NSError **))didMigrateHandler;
- (void)setDidMigrateHandler:(BOOL (^)(NSStagedMigrationManager *, NSCustomMigrationStage *, NSError **))block;
@end

@interface NSDerivedAttributeDescription : NSAttributeDescription
- (NSExpression *)derivationExpression;
- (NSString *)description;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (void)setDerivationExpression:(NSExpression *)expression;
- (NSData *)versionHash;
- (id)copyWithZone:(NSZone *)zone;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@end

@interface NSExpressionDescription : NSPropertyDescription
- (NSExpression *)expression;
- (void)setExpression:(NSExpression *)value;
- (NSAttributeType)expressionResultType;
- (void)setExpressionResultType:(NSAttributeType)value;
@end

@interface NSFetchRequest<__covariant ResultType> : NSPersistentStoreRequest
+ (instancetype)fetchRequestWithEntityName:(NSString *)name;
- (instancetype)init;
- (instancetype)initWithEntityName:(NSString *)name;
- (NSEntityDescription *)entity;
- (void)setEntity:(NSEntityDescription *)entity;
- (NSString *)entityName;
- (NSPredicate *)predicate;
- (void)setPredicate:(NSPredicate *)value;
- (NSArray *)sortDescriptors;
- (void)setSortDescriptors:(NSArray *)value;
- (NSUInteger)fetchLimit;
- (void)setFetchLimit:(NSUInteger)value;
- (NSFetchRequestResultType)resultType;
- (void)setResultType:(NSFetchRequestResultType)value;
- (NSString *)description;
- (BOOL)includesSubentities;
- (void)setIncludesSubentities:(BOOL)value;
- (BOOL)includesPropertyValues;
- (void)setIncludesPropertyValues:(BOOL)value;
- (BOOL)returnsObjectsAsFaults;
- (void)setReturnsObjectsAsFaults:(BOOL)value;
- (NSArray *)relationshipKeyPathsForPrefetching;
- (void)setRelationshipKeyPathsForPrefetching:(NSArray *)value;
- (BOOL)includesPendingChanges;
- (void)setIncludesPendingChanges:(BOOL)value;
- (BOOL)returnsDistinctResults;
- (void)setReturnsDistinctResults:(BOOL)value;
- (NSArray *)propertiesToFetch;
- (void)setPropertiesToFetch:(NSArray *)value;
- (NSUInteger)fetchOffset;
- (void)setFetchOffset:(NSUInteger)value;
- (NSUInteger)fetchBatchSize;
- (void)setFetchBatchSize:(NSUInteger)value;
- (BOOL)shouldRefreshRefetchedObjects;
- (void)setShouldRefreshRefetchedObjects:(BOOL)value;
- (NSArray *)propertiesToGroupBy;
- (void)setPropertiesToGroupBy:(NSArray *)value;
- (NSPredicate *)havingPredicate;
- (void)setHavingPredicate:(NSPredicate *)value;
- (NSArray *)affectedStores;
- (NSPersistentStoreRequestType)requestType;
- (NSArray *)execute:(NSError **)error;
- (id)copyWithZone:(NSZone *)zone;
- (id)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
@end

@interface NSFetchedPropertyDescription : NSPropertyDescription
- (NSFetchRequest *)fetchRequest;
- (void)setFetchRequest:(NSFetchRequest *)value;
@end

@interface NSIncrementalStore : NSPersistentStore
- (NSString *)type;
- (void)setMetadata:(NSDictionary *)metadata;
+ (id)identifierForNewStoreAtURL:(NSURL *)storeURL;
- (BOOL)loadMetadata:(NSError **)error;
- (id)executeRequest:(NSPersistentStoreRequest *)request withContext:(NSManagedObjectContext *)context error:(NSError **)error;
- (NSIncrementalStoreNode *)newValuesForObjectWithID:(NSManagedObjectID *)objectID withContext:(NSManagedObjectContext *)context error:(NSError **)error;
- (id)newValueForRelationship:(NSRelationshipDescription *)relationship forObjectWithID:(NSManagedObjectID *)objectID withContext:(NSManagedObjectContext *)context error:(NSError **)error;
- (NSArray *)obtainPermanentIDsForObjects:(NSArray *)array error:(NSError **)error;
- (void)managedObjectContextDidRegisterObjectsWithIDs:(NSArray *)objectIDs;
- (void)managedObjectContextDidUnregisterObjectsWithIDs:(NSArray *)objectIDs;
- (NSManagedObjectID *)newObjectIDForEntity:(NSEntityDescription *)entity referenceObject:(id)data;
- (id)referenceObjectForObjectID:(NSManagedObjectID *)objectID;
@end

@interface NSLightweightMigrationStage : NSMigrationStage
- (instancetype)initWithVersionChecksums:(NSArray *)checksums;
- (NSArray *)versionChecksums;
@end

@interface NSPersistentCloudKitContainer : NSPersistentContainer
- (BOOL)initializeCloudKitSchemaWithOptions:(NSPersistentCloudKitContainerSchemaInitializationOptions)options error:(NSError **)error;
- (CKRecord *)recordForManagedObjectID:(NSManagedObjectID *)managedObjectID;
- (NSDictionary *)recordsForManagedObjectIDs:(NSArray *)managedObjectIDs;
- (CKRecordID *)recordIDForManagedObjectID:(NSManagedObjectID *)managedObjectID;
- (NSDictionary *)recordIDsForManagedObjectIDs:(NSArray *)managedObjectIDs;
- (BOOL)canUpdateRecordForManagedObjectWithID:(NSManagedObjectID *)objectID;
- (BOOL)canDeleteRecordForManagedObjectWithID:(NSManagedObjectID *)objectID;
- (BOOL)canModifyManagedObjectsInStore:(NSPersistentStore *)store;
- (void)acceptShareInvitationsFromMetadata:(NSArray *)metadata intoPersistentStore:(NSPersistentStore *)persistentStore completion:(void (^)(NSArray *, NSError *))completion;
- (void)purgeObjectsAndRecordsInZoneWithID:(CKRecordZoneID *)zoneID inPersistentStore:(NSPersistentStore *)persistentStore completion:(void (^)(CKRecordZoneID *, NSError *))completion;
- (void)persistUpdatedShare:(CKShare *)share inPersistentStore:(NSPersistentStore *)persistentStore completion:(void (^)(CKShare *, NSError *))completion;
- (void)fetchParticipantsMatchingLookupInfos:(NSArray *)lookupInfos intoPersistentStore:(NSPersistentStore *)persistentStore completion:(void (^)(NSArray *, NSError *))completion;
- (NSDictionary *)fetchSharesMatchingObjectIDs:(NSArray *)objectIDs error:(NSError **)error;
- (NSArray *)fetchSharesInPersistentStore:(NSPersistentStore *)persistentStore error:(NSError **)error;
- (void)shareManagedObjects:(NSArray *)managedObjects toShare:(CKShare *)share completion:(void (^)(NSSet *, CKShare *, CKContainer *, NSError *))completion;
@end

@interface NSPersistentCloudKitContainerEventRequest : NSPersistentStoreRequest
+ (instancetype)fetchEventsAfterDate:(NSDate *)date;
+ (instancetype)fetchEventsAfterEvent:(NSPersistentCloudKitContainerEvent *)event;
+ (instancetype)fetchEventsMatchingFetchRequest:(NSFetchRequest *)fetchRequest;
+ (NSFetchRequest *)fetchRequestForEvents;
- (NSPersistentStoreRequestType)requestType;
- (NSPersistentCloudKitContainerEventResultType)resultType;
- (void)setResultType:(NSPersistentCloudKitContainerEventResultType)type;
@end

@interface NSPersistentCloudKitContainerEventResult : NSPersistentStoreResult
- (NSPersistentCloudKitContainerEventResultType)resultType;
- (id)result;
@end

@interface NSPersistentHistoryChangeRequest : NSPersistentStoreRequest
+ (instancetype)requestWithOperation:(NSString *)operation value:(id)value;
+ (instancetype)fetchHistoryAfterDate:(NSDate *)date;
+ (instancetype)fetchHistoryAfterToken:(NSPersistentHistoryToken *)token;
+ (instancetype)fetchHistoryAfterTransaction:(NSPersistentHistoryTransaction *)transaction;
+ (instancetype)fetchHistoryWithFetchRequest:(NSFetchRequest *)request;
+ (instancetype)deleteHistoryBeforeDate:(NSDate *)date;
+ (instancetype)deleteHistoryBeforeToken:(NSPersistentHistoryToken *)token;
+ (instancetype)deleteHistoryBeforeTransaction:(NSPersistentHistoryTransaction *)transaction;
- (NSPersistentHistoryResultType)resultType;
- (void)setResultType:(NSPersistentHistoryResultType)value;
- (NSPersistentHistoryToken *)token;
- (NSFetchRequest *)fetchRequest;
- (void)setFetchRequest:(NSFetchRequest *)request;
- (NSPersistentStoreRequestType)requestType;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface NSPersistentHistoryResult : NSPersistentStoreResult
- (id)result;
- (NSPersistentHistoryResultType)resultType;
@end

@interface NSPersistentStoreAsynchronousResult : NSPersistentStoreResult
- (NSManagedObjectContext *)managedObjectContext;
- (NSError *)operationError;
- (NSProgress *)progress;
- (void)cancel;
@end

@interface NSAsynchronousFetchResult<__covariant ResultType> : NSPersistentStoreAsynchronousResult
- (NSAsynchronousFetchRequest *)fetchRequest;
- (NSArray *)finalResult;
@end

extern NSString *const NSAddedPersistentStoresKey;

extern NSString *const NSAffectedObjectsErrorKey;

extern NSString *const NSAffectedStoresErrorKey;

extern NSString *const NSBinaryExternalRecordType;

extern NSString *const NSBinaryStoreInsecureDecodingCompatibilityOption;

extern NSString *const NSBinaryStoreSecureDecodingClasses;

extern NSString *const NSCoreDataCoreSpotlightExporter;

extern NSString *const NSDeletedObjectIDsKey;

extern NSString *const NSDetailedErrorsKey;

extern NSString *const NSEntityNameInPathKey;

extern NSString *const NSExternalRecordExtensionOption;

extern NSString *const NSExternalRecordsDirectoryOption;

extern NSString *const NSExternalRecordsFileFormatOption;

extern NSString *const NSInsertedObjectIDsKey;

extern NSString *const NSInvalidatedObjectIDsKey;

extern NSString *const NSManagedObjectContextQueryGenerationKey;

extern NSString *const NSMigrationDestinationObjectKey;

extern NSString *const NSMigrationEntityMappingKey;

extern NSString *const NSMigrationEntityPolicyKey;

extern NSString *const NSMigrationManagerKey;

extern NSString *const NSMigrationPropertyMappingKey;

extern NSString *const NSMigrationSourceObjectKey;

extern NSString *const NSModelPathKey;

extern NSString *const NSObjectURIKey;

extern NSString *const NSPersistentCloudKitContainerEventChangedNotification;

extern NSString *const NSPersistentCloudKitContainerEventUserInfoKey;

extern NSString *const NSPersistentStoreConnectionPoolMaxSizeKey;

extern NSString *const NSPersistentStoreCoordinatorStoresWillChangeNotification;

extern NSString *const NSPersistentStoreCoordinatorWillRemoveStoreNotification;

extern NSString *const NSPersistentStoreDeferredLightweightMigrationOptionKey;

extern NSString *const NSPersistentStoreDidImportUbiquitousContentChangesNotification;

extern NSString *const NSPersistentStoreForceDestroyOption;

extern NSString *const NSPersistentStoreModelVersionChecksumKey;

extern NSString *const NSPersistentStoreOSCompatibility;

extern NSString *const NSPersistentStoreRebuildFromUbiquitousContentOption;

extern NSString *const NSPersistentStoreRemoveUbiquitousMetadataOption;

extern NSString *const NSPersistentStoreSaveConflictsErrorKey;

extern NSString *const NSPersistentStoreURLKey;

extern NSString *const NSPersistentStoreUbiquitousContainerIdentifierKey;

extern NSString *const NSPersistentStoreUbiquitousContentNameKey;

extern NSString *const NSPersistentStoreUbiquitousContentURLKey;

extern NSString *const NSPersistentStoreUbiquitousPeerTokenOption;

extern NSString *const NSPersistentStoreUbiquitousTransitionTypeKey;

extern NSString *const NSRefreshedObjectIDsKey;

extern NSString *const NSRemovedPersistentStoresKey;

extern NSString *const NSStorePathKey;

extern NSString *const NSStoreUUIDInPathKey;

extern NSString *const NSUUIDChangedPersistentStoresKey;

extern NSString *const NSValidateXMLStoreOption;

extern NSString *const NSValidationKeyErrorKey;

extern NSString *const NSValidationObjectErrorKey;

extern NSString *const NSValidationPredicateErrorKey;

extern NSString *const NSValidationValueErrorKey;

extern NSString *const NSXMLExternalRecordType;
