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
