/* SPDX-License-Identifier: MIT */
/* Numeric error values: .NET Accounts binding's ACErrorCode documentation:
 * https://learn.microsoft.com/en-us/dotnet/api/accounts.acerrorcode */
typedef NS_ENUM(NSInteger, ACAccountCredentialRenewResult) {
    ACAccountCredentialRenewResultRenewed = 0,
    ACAccountCredentialRenewResultRejected = 1,
    ACAccountCredentialRenewResultFailed = 2
};
enum { ACErrorAccountNotFound = 6, ACErrorPermissionDenied = 7 };
typedef void (^ACAccountStoreRequestAccessCompletionHandler)(BOOL, NSError *);
typedef void (^ACAccountStoreSaveCompletionHandler)(BOOL, NSError *);
typedef void (^ACAccountStoreRemoveCompletionHandler)(BOOL, NSError *);
typedef void (^ACAccountStoreCredentialRenewalHandler)(ACAccountCredentialRenewResult, NSError *);
