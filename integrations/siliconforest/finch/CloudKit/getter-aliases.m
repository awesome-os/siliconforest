/* SPDX-License-Identifier: MIT */
/* Preserve clients compiled with our initial declarations while publishing the
 * real isLongLived getter in the corrected property catalogue. */
#import <CloudKit/CloudKit.h>
@implementation CKOperationConfiguration (SFDeclarationAliases)
- (BOOL)longLived { return [self isLongLived]; }
@end
