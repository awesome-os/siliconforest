/* SPDX-License-Identifier: MIT */
#import <StoreKit/StoreKit.h>
#import <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %s\n",#x);exit(1);} }while(0)
#define WAIT(x) do { CFAbsoluteTime deadline=CFAbsoluteTimeGetCurrent()+5; \
    while(!(x) && CFAbsoluteTimeGetCurrent()<deadline) \
        CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.02,true); CHECK(x); }while(0)
@interface Observer : NSObject <SKPaymentTransactionObserver,SKProductsRequestDelegate>
@property BOOL requestFailed,updated,removed,restoreFailed;
@end
@implementation Observer
- (void)request:(SKRequest *)r didFailWithError:(NSError *)e {
    CHECK([e.domain isEqualToString:SKErrorDomain]);CHECK(e.code==SKErrorCloudServiceNetworkConnectionFailed);
    _requestFailed=YES;
}
- (void)paymentQueue:(SKPaymentQueue *)q updatedTransactions:(NSArray *)transactions {
    CHECK(transactions.count==1);SKPaymentTransaction *t=transactions[0];
    CHECK(t.transactionState==SKPaymentTransactionStateFailed);
    CHECK(t.error.code==SKErrorPaymentNotAllowed);_updated=YES;
}
- (void)paymentQueue:(SKPaymentQueue *)q removedTransactions:(NSArray *)transactions {CHECK(transactions.count==1);_removed=YES;}
- (void)paymentQueue:(SKPaymentQueue *)q restoreCompletedTransactionsFailedWithError:(NSError *)e {CHECK(e.code==SKErrorCloudServiceNetworkConnectionFailed);_restoreFailed=YES;}
@end
int main(void) { @autoreleasepool {
    CHECK(![SKPaymentQueue canMakePayments]);
    SKProduct *product=[SKProduct new];CHECK(product.price!=nil);CHECK(product.price.doubleValue==0);
    SKMutablePayment *payment=(SKMutablePayment *)[SKMutablePayment paymentWithProduct:product];
    payment.productIdentifier=@"org.siliconforest.probe";payment.quantity=3;payment.applicationUsername=@"probe";
    SKPayment *copy=[payment copy];CHECK(copy.quantity==3);CHECK([copy.productIdentifier isEqualToString:payment.productIdentifier]);
    SKPaymentQueue *queue=[SKPaymentQueue defaultQueue];CHECK(queue.transactions.count==0);
    __weak Observer *weak;
    @autoreleasepool {
    Observer *observer=[Observer new];weak=observer;[queue addTransactionObserver:observer];CHECK(queue.transactionObservers.count==1);
    SKProductsRequest *request=[[SKProductsRequest alloc] initWithProductIdentifiers:[NSSet setWithObject:@"probe"]];
    request.delegate=observer;[request start];
    WAIT(observer.requestFailed);
    [queue addPayment:payment];WAIT(observer.updated);CHECK(queue.transactions.count==1);
    [queue finishTransaction:queue.transactions[0]];WAIT(observer.removed);CHECK(queue.transactions.count==0);
    [queue restoreCompletedTransactions];WAIT(observer.restoreFailed);
    [queue removeTransactionObserver:observer];CHECK(queue.transactionObservers.count==0);
    observer=nil;request.delegate=nil;
    }
    CHECK(weak==nil);
    puts("PASS StoreKit values/copy/weak observer/offline request/purchase refusal/finish/restore refusal");
    return 0;
} }
