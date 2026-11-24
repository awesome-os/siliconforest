/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class SKRequest, SKProductsRequest, SKReceiptRefreshRequest, SKProductsResponse, SKProduct, SKProductSubscriptionPeriod, SKProductDiscount, SKPaymentDiscount, SKPayment, SKMutablePayment, SKPaymentTransaction, SKDownload, SKPaymentQueue, SKStorefront, SKStoreReviewController;
/* SPDX-License-Identifier: MIT */
typedef NSInteger SKErrorCode, SKProductPeriodUnit, SKProductDiscountPaymentMode;
typedef NSInteger SKProductDiscountType, SKPaymentTransactionState, SKDownloadState;
enum { SKErrorPaymentNotAllowed = 4, SKErrorCloudServiceNetworkConnectionFailed = 7,
       SKProductPeriodUnitDay = 0, SKProductDiscountPaymentModePayAsYouGo = 0,
       SKProductDiscountTypeIntroductory = 0, SKPaymentTransactionStateFailed = 2,
       SKDownloadStateFailed = 4 };
@protocol SKRequestDelegate <NSObject>
@optional
- (void)request:(SKRequest *)request didFailWithError:(NSError *)error;
@end
@protocol SKProductsRequestDelegate <SKRequestDelegate>
@end
@protocol SKPaymentQueueDelegate <NSObject>
@end
@protocol SKPaymentTransactionObserver <NSObject>
@optional
- (void)paymentQueue:(SKPaymentQueue *)queue updatedTransactions:(NSArray *)transactions;
- (void)paymentQueue:(SKPaymentQueue *)queue removedTransactions:(NSArray *)transactions;
- (void)paymentQueue:(SKPaymentQueue *)queue restoreCompletedTransactionsFailedWithError:(NSError *)error;
@end
/* Declarations missing from the PD headers. The implementation is a Foundation
 * prerequisite; these are declarations only, with no fabricated substitute. */
@interface NSDecimalNumber : NSNumber
+ (NSDecimalNumber *)zero;
@end
@interface NSHashTable (SFFinchStoreKitDeclarations)
+ (NSHashTable *)weakObjectsHashTable;
- (NSArray *)allObjects;
- (void)addObject:(id)object;
- (void)removeObject:(id)object;
@end

extern NSString *const SKCloudServiceCapabilitiesDidChangeNotification;
extern const double SKDownloadTimeRemainingUnknown;
extern NSString *const SKErrorDomain;
extern NSString *const SKReceiptPropertyIsExpired;
extern NSString *const SKReceiptPropertyIsRevoked;
extern NSString *const SKReceiptPropertyIsVolumePurchase;
extern NSString *const SKStoreProductParameterAdvertisingPartnerToken;
extern NSString *const SKStoreProductParameterAffiliateToken;
extern NSString *const SKStoreProductParameterCampaignToken;
extern NSString *const SKStoreProductParameterCustomProductPageIdentifier;
extern NSString *const SKStoreProductParameterITunesItemIdentifier;
extern NSString *const SKStoreProductParameterProductIdentifier;
extern NSString *const SKStoreProductParameterProviderToken;
extern NSString *const SKStorefrontCountryCodeDidChangeNotification;
extern NSString *const SKStorefrontIdentifierDidChangeNotification;
extern NSString *const SKUIServiceNavigationOptionBackButtonHidden;
extern NSString *const SKUIServiceNavigationOptionBackButtonTitle;
extern NSString *const SKUIServiceNavigationOptionRightButtonEnabled;
extern NSString *const SKUIServiceNavigationOptionRightButtonStyle;
extern NSString *const SKUIServiceNavigationOptionRightButtonTitle;
extern NSString *const SKUIServiceNavigationOptionTitle;
extern NSString *const SKUIServiceReviewSandboxMode;

@interface SKRequest : NSObject
- (id<SKRequestDelegate>)delegate;
- (void)setDelegate:(id<SKRequestDelegate>)delegate;
- (void)cancel;
- (void)start;
@end

@interface SKProductsRequest : SKRequest
- (instancetype)initWithProductIdentifiers:(NSSet<NSString *> *)productIdentifiers;
- (id<SKProductsRequestDelegate>)delegate;
- (void)setDelegate:(id<SKProductsRequestDelegate>)delegate;
@end

@interface SKReceiptRefreshRequest : SKRequest
- (instancetype)initWithReceiptProperties:(NSDictionary<NSString *, id> *)properties;
- (NSDictionary *)receiptProperties;
@end

@interface SKProductsResponse : NSObject
- (NSArray<SKProduct *> *)products;
- (NSArray<NSString *> *)invalidProductIdentifiers;
@end

@interface SKProduct : NSObject
- (NSString *)localizedDescription;
- (NSString *)localizedTitle;
- (NSDecimalNumber *)price;
- (NSLocale *)priceLocale;
- (NSString *)productIdentifier;
- (BOOL)isDownloadable;
- (BOOL)downloadable;
- (BOOL)isFamilyShareable;
- (NSArray<NSNumber *> *)contentLengths;
- (NSArray<NSNumber *> *)downloadContentLengths;
- (NSString *)contentVersion;
- (NSString *)downloadContentVersion;
- (SKProductSubscriptionPeriod *)subscriptionPeriod;
- (SKProductDiscount *)introductoryPrice;
- (NSString *)subscriptionGroupIdentifier;
- (NSArray<SKProductDiscount *> *)discounts;
@end

@interface SKProductSubscriptionPeriod : NSObject
- (NSUInteger)numberOfUnits;
- (SKProductPeriodUnit)unit;
@end

@interface SKProductDiscount : NSObject
- (NSDecimalNumber *)price;
- (NSLocale *)priceLocale;
- (NSString *)identifier;
- (SKProductSubscriptionPeriod *)subscriptionPeriod;
- (NSUInteger)numberOfPeriods;
- (SKProductDiscountPaymentMode)paymentMode;
- (SKProductDiscountType)type;
@end

@interface SKPaymentDiscount : NSObject
- (instancetype)initWithIdentifier:(NSString *)identifier keyIdentifier:(NSString *)keyIdentifier nonce:(NSUUID *)nonce signature:(NSString *)signature timestamp:(NSNumber *)timestamp;
- (NSString *)identifier;
- (NSString *)keyIdentifier;
- (NSUUID *)nonce;
- (NSString *)signature;
- (NSNumber *)timestamp;
@end

@interface SKPayment : NSObject
+ (instancetype)paymentWithProduct:(SKProduct *)product;
+ (id)paymentWithProductIdentifier:(NSString *)identifier;
- (NSString *)productIdentifier;
- (NSData *)requestData;
- (NSInteger)quantity;
- (NSString *)applicationUsername;
- (BOOL)simulatesAskToBuyInSandbox;
- (SKPaymentDiscount *)paymentDiscount;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;
@end

@interface SKMutablePayment : SKPayment
- (void)setProductIdentifier:(NSString *)v;
- (void)setRequestData:(NSData *)v;
- (void)setQuantity:(NSInteger)v;
- (void)setApplicationUsername:(NSString *)v;
- (void)setSimulatesAskToBuyInSandbox:(BOOL)v;
- (void)setPaymentDiscount:(SKPaymentDiscount *)v;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface SKPaymentTransaction : NSObject
+ (instancetype)_finchFailedTransactionFor:(SKPayment *)payment;
- (SKPayment *)payment;
- (NSError *)error;
- (SKPaymentTransactionState)transactionState;
- (NSString *)transactionIdentifier;
- (NSDate *)transactionDate;
- (SKPaymentTransaction *)originalTransaction;
- (NSData *)transactionReceipt;
- (NSArray<SKDownload *> *)downloads;
@end

@interface SKDownload : NSObject
- (SKDownloadState)state;
- (SKDownloadState)downloadState;
- (NSNumber *)contentLength;
- (long long)expectedContentLength;
- (NSString *)contentIdentifier;
- (NSURL *)contentURL;
- (NSString *)contentVersion;
- (NSError *)error;
- (float)progress;
- (NSTimeInterval)timeRemaining;
- (SKPaymentTransaction *)transaction;
+ (NSURL *)contentURLForProductID:(NSString *)productID;
+ (void)deleteContentForProductID:(NSString *)productID;
@end

@interface SKPaymentQueue : NSObject
+ (BOOL)canMakePayments;
+ (instancetype)defaultQueue;
- (instancetype)init;
- (id<SKPaymentQueueDelegate>)delegate;
- (void)setDelegate:(id<SKPaymentQueueDelegate>)delegate;
- (SKStorefront *)storefront;
- (NSArray<SKPaymentTransaction *> *)transactions;
- (NSArray *)transactionObservers;
- (void)addTransactionObserver:(id<SKPaymentTransactionObserver>)observer;
- (void)removeTransactionObserver:(id<SKPaymentTransactionObserver>)observer;
- (void)addPayment:(SKPayment *)payment;
- (void)finishTransaction:(SKPaymentTransaction *)transaction;
- (void)restoreCompletedTransactions;
- (void)restoreCompletedTransactionsWithApplicationUsername:(NSString *)username;
- (void)startDownloads:(NSArray<SKDownload *> *)downloads;
- (void)pauseDownloads:(NSArray<SKDownload *> *)downloads;
- (void)resumeDownloads:(NSArray<SKDownload *> *)downloads;
- (void)cancelDownloads:(NSArray<SKDownload *> *)downloads;
- (void)presentCodeRedemptionSheet;
- (void)showPriceConsentIfNeeded;
@end

@interface SKStorefront : NSObject
- (NSString *)countryCode;
- (NSString *)identifier;
@end

@interface SKStoreReviewController : NSObject
+ (void)requestReview;
+ (void)requestReviewInScene:(id)windowScene;
@end
