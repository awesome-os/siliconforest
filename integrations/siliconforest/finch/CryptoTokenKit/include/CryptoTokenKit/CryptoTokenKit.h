/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class TKTLVRecord, TKBERTLVRecord, TKSimpleTLVRecord, TKCompactTLVRecord, TKTokenWatcher, TKSmartCardPINFormat, TKTokenKeychainItem, TKTokenKeychainCertificate, TKTokenKeychainKey, TKTokenKeychainContents, TKTokenConfiguration, TKTokenDriverConfiguration, TKTokenKeyAlgorithm, TKTokenKeyExchangeParameters, TKTokenSession, TKToken, TKTokenDriver, TKSmartCardSlotManager, TKTokenAuthOperation, TKTokenPasswordAuthOperation;
/* SPDX-License-Identifier: MIT */
#import <Security/Security.h>
#include <stdint.h>
@class TKTokenWatcherTokenInfo, TKSmartCardSlot;
@protocol TKTokenDelegate <NSObject>
@end
@protocol TKTokenSessionDelegate <NSObject>
@end
@protocol TKTokenDriverDelegate <NSObject>
@end
typedef uint64_t TKTLVTag;
typedef id<NSCopying, NSSecureCoding> TKTokenObjectID;
typedef NSInteger TKSmartCardPINCharset;
typedef NSInteger TKSmartCardPINEncoding;
typedef NSInteger TKSmartCardPINJustification;
/* Numeric facts from Microsoft's CryptoTokenKit bindings (learn.microsoft.com):
 * tksmartcardpinencoding, tksmartcardpinjustification, tkerrorcode. */
enum { TKSmartCardPINCharsetNumeric = 0, TKSmartCardPINCharsetAlphanumeric = 1,
       TKSmartCardPINEncodingASCII = 1, TKSmartCardPINEncodingBCD = 2,
       TKSmartCardPINJustificationLeft = 0, TKSmartCardPINJustificationRight = 1,
       TKErrorCodeBadParameter = -8, TKErrorCodeObjectNotFound = -6,
       TKTokenOperationReadData = 1 };

extern NSString * const TKErrorDomain;

@interface TKTLVRecord : NSObject
- (TKTLVTag)tag;
- (NSData *)value;
- (NSData *)data;
+ (instancetype)recordFromData:(NSData *)data;
+ (NSArray<TKTLVRecord *> *)sequenceOfRecordsFromData:(NSData *)data;
@end

@interface TKBERTLVRecord : TKTLVRecord
+ (NSData *)dataForTag:(TKTLVTag)tag;
- (instancetype)initWithTag:(TKTLVTag)tag value:(NSData *)value;
- (instancetype)initWithTag:(TKTLVTag)tag records:(NSArray<TKTLVRecord *> *)records;
@end

@interface TKSimpleTLVRecord : TKTLVRecord
- (instancetype)initWithTag:(UInt8)tag value:(NSData *)value;
@end

@interface TKCompactTLVRecord : TKTLVRecord
- (instancetype)initWithTag:(UInt8)tag value:(NSData *)value;
@end

@interface TKTokenWatcher : NSObject
- (instancetype)init;
- (instancetype)initWithInsertionHandler:(void (^)(NSString *))insertionHandler;
- (NSArray<NSString *> *)tokenIDs;
- (void)setInsertionHandler:(void (^)(NSString *))insertionHandler;
- (NSMutableDictionary *)_removalHandlers;
- (void)addRemovalHandler:(void (^)(NSString *))handler forTokenID:(NSString *)tokenID;
- (TKTokenWatcherTokenInfo *)tokenInfoForTokenID:(NSString *)tokenID;
@end

@interface TKSmartCardPINFormat : NSObject
- (TKSmartCardPINCharset)charset;
- (void)setCharset:(TKSmartCardPINCharset)value;
- (TKSmartCardPINEncoding)encoding;
- (void)setEncoding:(TKSmartCardPINEncoding)value;
- (NSInteger)minPINLength;
- (void)setMinPINLength:(NSInteger)value;
- (NSInteger)maxPINLength;
- (void)setMaxPINLength:(NSInteger)value;
- (NSInteger)PINBlockByteLength;
- (void)setPINBlockByteLength:(NSInteger)value;
- (TKSmartCardPINJustification)PINJustification;
- (void)setPINJustification:(TKSmartCardPINJustification)value;
- (NSInteger)PINBitOffset;
- (void)setPINBitOffset:(NSInteger)value;
- (NSInteger)PINLengthBitOffset;
- (void)setPINLengthBitOffset:(NSInteger)value;
- (NSInteger)PINLengthBitSize;
- (void)setPINLengthBitSize:(NSInteger)value;
@end

@interface TKTokenKeychainItem : NSObject
- (instancetype)initWithObjectID:(TKTokenObjectID)objectID;
- (id)objectID;
- (NSString *)label;
- (void)setLabel:(NSString *)value;
- (NSDictionary *)constraints;
- (void)setConstraints:(NSDictionary *)value;
@end

@interface TKTokenKeychainCertificate : TKTokenKeychainItem
- (instancetype)initWithCertificate:(SecCertificateRef)certificateRef objectID:(TKTokenObjectID)objectID;
- (NSData *)data;
@end

@interface TKTokenKeychainKey : TKTokenKeychainItem
- (instancetype)initWithCertificate:(SecCertificateRef)certificateRef objectID:(TKTokenObjectID)objectID;
- (NSString *)keyType;
- (void)setKeyType:(NSString *)value;
- (NSData *)applicationTag;
- (void)setApplicationTag:(NSData *)value;
- (NSInteger)keySizeInBits;
- (void)setKeySizeInBits:(NSInteger)value;
- (NSData *)publicKeyData;
- (void)setPublicKeyData:(NSData *)value;
- (NSData *)publicKeyHash;
- (void)setPublicKeyHash:(NSData *)value;
- (BOOL)canDecrypt;
- (void)setCanDecrypt:(BOOL)value;
- (BOOL)canSign;
- (void)setCanSign:(BOOL)value;
- (BOOL)canPerformKeyExchange;
- (void)setCanPerformKeyExchange:(BOOL)value;
- (BOOL)isSuitableForLogin;
- (void)setSuitableForLogin:(BOOL)value;
@end

@interface TKTokenKeychainContents : NSObject
- (void)fillWithItems:(NSArray<TKTokenKeychainItem *> *)items;
- (NSArray *)items;
- (TKTokenKeychainKey *)keyForObjectID:(id)objectID error:(NSError **)error;
- (TKTokenKeychainCertificate *)certificateForObjectID:(id)objectID error:(NSError **)error;
@end

@interface TKTokenConfiguration : NSObject
- (NSString *)instanceID;
- (NSData *)configurationData;
- (void)setConfigurationData:(NSData *)value;
- (NSArray *)keychainItems;
- (void)setKeychainItems:(NSArray *)value;
- (TKTokenKeychainKey *)keyForObjectID:(id)objectID error:(NSError **)error;
- (TKTokenKeychainCertificate *)certificateForObjectID:(id)objectID error:(NSError **)error;
@end

@interface TKTokenDriverConfiguration : NSObject
+ (NSDictionary *)driverConfigurations;
- (NSString *)classID;
- (NSDictionary *)tokenConfigurations;
- (TKTokenConfiguration *)addTokenConfigurationForTokenInstanceID:(NSString *)instanceID;
- (void)removeTokenConfigurationForTokenInstanceID:(NSString *)instanceID;
@end

@interface TKTokenKeyAlgorithm : NSObject
- (BOOL)isAlgorithm:(SecKeyAlgorithm)algorithm;
- (BOOL)supportsAlgorithm:(SecKeyAlgorithm)algorithm;
@end

@interface TKTokenKeyExchangeParameters : NSObject
- (NSInteger)requestedSize;
- (NSData *)sharedInfo;
@end

@interface TKTokenSession : NSObject
- (instancetype)initWithToken:(TKToken *)token;
- (TKToken *)token;
- (id<TKTokenSessionDelegate>)delegate;
- (void)setDelegate:(id<TKTokenSessionDelegate>)delegate;
@end

@interface TKToken : NSObject
- (instancetype)initWithTokenDriver:(TKTokenDriver *)tokenDriver instanceID:(NSString *)instanceID;
- (TKTokenDriver *)tokenDriver;
- (id<TKTokenDelegate>)delegate;
- (void)setDelegate:(id<TKTokenDelegate>)delegate;
- (TKTokenConfiguration *)configuration;
- (TKTokenKeychainContents *)keychainContents;
@end

@interface TKTokenDriver : NSObject
- (id<TKTokenDriverDelegate>)delegate;
- (void)setDelegate:(id<TKTokenDriverDelegate>)delegate;
@end

@interface TKSmartCardSlotManager : NSObject
+ (TKSmartCardSlotManager *)defaultManager;
- (NSArray<NSString *> *)slotNames;
- (TKSmartCardSlot *)slotNamed:(NSString *)name;
- (void)getSlotWithName:(NSString *)name reply:(void (^)(TKSmartCardSlot *))reply;
- (BOOL)isNFCSupported;
@end

@interface TKTokenAuthOperation : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (BOOL)finishWithError:(NSError **)error;
@end

@interface TKTokenPasswordAuthOperation : TKTokenAuthOperation
- (NSString *)password;
- (void)setPassword:(NSString *)value;
@end
