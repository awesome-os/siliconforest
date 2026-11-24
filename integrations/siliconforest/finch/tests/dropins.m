/* SPDX-License-Identifier: MIT */
#import <Accounts/Accounts.h>
#import <LocalAuthentication/LocalAuthentication.h>
#include <assert.h>
#include <stdio.h>
int main(void)
{
    @autoreleasepool {
        ACAccountStore *store = [ACAccountStore new];
        assert(store && store.accounts.count == 0);
        ACAccountType *type = [store accountTypeWithAccountTypeIdentifier:ACAccountTypeIdentifierTwitter];
        assert(type && [type.identifier isEqualToString:ACAccountTypeIdentifierTwitter]);
        assert(!type.accessGranted);
        ACAccount *account = [[ACAccount alloc] initWithAccountType:type];
        account.username = @"finch-conformance";
        assert([account.username isEqualToString:@"finch-conformance"]);
        ACAccountCredential *credential = [[ACAccountCredential alloc] initWithOAuthToken:@"dummy" tokenSecret:@"dummy-secret"];
        assert([credential.oauthToken isEqualToString:@"dummy"]);
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block BOOL denied = NO;
        [store requestAccessToAccountsWithType:type options:nil completion:^(BOOL granted, NSError *error) {
            denied = !granted && [error.domain isEqualToString:ACErrorDomain] && error.code == ACErrorPermissionDenied;
            dispatch_semaphore_signal(semaphore);
        }];
        assert(!dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 5*NSEC_PER_SEC)) && denied);
        LAContext *context = [LAContext new];
        NSError *error = nil;
        assert(![context canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics error:&error]);
        assert(error.code == LAErrorBiometryNotAvailable && [error.domain isEqualToString:LAErrorDomain]);
        context.localizedCancelTitle = @"Cancel check";
        assert([context.localizedCancelTitle isEqualToString:@"Cancel check"]);
        context.touchIDAuthenticationAllowableReuseDuration = 1000;
        assert(context.touchIDAuthenticationAllowableReuseDuration == 300);
        puts("drop-ins: PASS (Accounts values/access refusal, LocalAuthentication unavailable/error/settings)");
    }
    return 0;
}
