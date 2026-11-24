/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class ODSession, ODNode, ODRecord, ODQuery;
/* SPDX-License-Identifier: MIT */
#import <CFOpenDirectory/CFOpenDirectory.h>
@protocol ODQueryDelegate <NSObject>
- (void)query:(ODQuery *)query foundResults:(NSArray *)results error:(NSError *)error;
@end
extern NSString *const ODFrameworkErrorDomain;
extern NSString *const ODSessionProxyAddress;
extern NSString *const ODSessionProxyPassword;
extern NSString *const ODSessionProxyPort;
extern NSString *const ODSessionProxyUsername;
extern NSString *const ODTrustTypeAnonymous;
extern NSString *const ODTrustTypeJoined;
extern NSString *const ODTrustTypeUsingCredentials;



@interface ODSession : NSObject { id _internal; }
+ (ODSession *)defaultSession;
+ (instancetype)sessionWithOptions:(NSDictionary *)options error:(NSError **)outError;
- (instancetype)initWithOptions:(NSDictionary *)options error:(NSError **)outError;
- (NSArray *)nodeNamesAndReturnError:(NSError **)outError;
- (NSArray *)configurationTemplateNames;
- (NSArray *)mappingTemplateNames;
- (void)dealloc;
@end

@interface ODNode : NSObject { id _internal; }
+ (instancetype)nodeWithSession:(ODSession *)session type:(ODNodeType)type error:(NSError **)outError;
+ (instancetype)nodeWithSession:(ODSession *)session name:(NSString *)name error:(NSError **)outError;
- (instancetype)initWithSession:(ODSession *)session type:(ODNodeType)type error:(NSError **)outError;
- (instancetype)initWithSession:(ODSession *)session name:(NSString *)name error:(NSError **)outError;
- (NSString *)nodeName;
- (NSArray *)subnodeNamesAndReturnError:(NSError **)error;
- (NSArray *)unreachableSubnodeNamesAndReturnError:(NSError **)error;
- (NSArray *)supportedRecordTypesAndReturnError:(NSError **)error;
- (NSArray *)supportedAttributesForRecordType:(ODRecordType)type error:(NSError **)error;
- (NSDictionary *)nodeDetailsForKeys:(NSArray *)keys error:(NSError **)error;
- (ODRecord *)recordWithRecordType:(ODRecordType)type name:(NSString *)name attributes:(id)attributes error:(NSError **)outError;
- (BOOL)setCredentialsWithRecordType:(ODRecordType)type recordName:(NSString *)name password:(NSString *)password error:(NSError **)outError;
- (void)dealloc;
@end

@interface ODRecord : NSObject
- (NSString *)recordName;
- (NSString *)recordType;
- (NSArray *)valuesForAttribute:(ODAttributeType)attribute error:(NSError **)outError;
- (NSDictionary *)recordDetailsForAttributes:(NSArray *)attributes error:(NSError **)outError;
- (BOOL)verifyPassword:(NSString *)password error:(NSError **)outError;
- (BOOL)synchronizeAndReturnError:(NSError **)outError;
- (void)dealloc;
@end

@interface ODQuery : NSObject
+ (ODQuery *)queryWithNode:(ODNode *)node forRecordTypes:(id)types attribute:(ODAttributeType)attribute matchType:(ODMatchType)matchType queryValues:(id)values returnAttributes:(id)attributes maximumResults:(NSInteger)maximum error:(NSError **)error;
- (instancetype)initWithNode:(ODNode *)node forRecordTypes:(id)types attribute:(ODAttributeType)attribute matchType:(ODMatchType)matchType queryValues:(id)values returnAttributes:(id)attributes maximumResults:(NSInteger)maximum error:(NSError **)error;
- (NSArray *)resultsAllowingPartial:(BOOL)partial error:(NSError **)error;
- (id<ODQueryDelegate>)delegate;
- (void)setDelegate:(id<ODQueryDelegate>)delegate;
- (NSOperationQueue *)operationQueue;
- (void)setOperationQueue:(NSOperationQueue *)queue;
- (void)scheduleInRunLoop:(NSRunLoop *)runLoop forMode:(NSString *)mode;
- (void)removeFromRunLoop:(NSRunLoop *)runLoop forMode:(NSString *)mode;
- (void)synchronize;
- (id)copyWithZone:(NSZone *)zone;
- (void)dealloc;
@end
