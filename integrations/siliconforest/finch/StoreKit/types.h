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
