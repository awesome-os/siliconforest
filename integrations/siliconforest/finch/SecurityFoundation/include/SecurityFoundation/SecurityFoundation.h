/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class SFAuthorization;
/* SPDX-License-Identifier: MIT */
#import <Security/Security.h>
@interface NSCoder (SFFinchSecurityFoundationDeclarations)
- (id)decodeObjectOfClass:(Class)cls forKey:(NSString *)key;
@end
@interface NSError (SFFinchSecurityFoundationDeclarations)
+ (instancetype)errorWithDomain:(NSString *)domain code:(NSInteger)code userInfo:(NSDictionary *)info;
@end
@class NSPortCoder;
typedef const char *AuthorizationString;
CFDateRef SecCertificateCopyNotValidAfterDate(SecCertificateRef certificate);



@interface SFAuthorization : NSObject <NSSecureCoding> { AuthorizationRef _authorization; }
+ (BOOL)supportsSecureCoding;
+ (id)authorization;
+ (id)authorizationWithFlags:(AuthorizationFlags)flags rights:(const AuthorizationRights *)rights environment:(const AuthorizationEnvironment *)environment;
- (id)initWithFlags:(AuthorizationFlags)flags rights:(const AuthorizationRights *)rights environment:(const AuthorizationEnvironment *)environment;
- (id)init;
- (void)dealloc;
- (void)encodeWithCoder:(NSCoder *)coder;
- (id)initWithCoder:(NSCoder *)coder;
- (id)replacementObjectForPortCoder:(NSPortCoder *)coder;
- (AuthorizationRef)authorizationRef;
- (OSStatus)permitWithRights:(const AuthorizationRights *)rights flags:(AuthorizationFlags)flags environment:(const AuthorizationEnvironment *)environment authorizedRights:(AuthorizationRights *)authorizedRights;
- (OSStatus)permitWithRight:(AuthorizationString)rightName flags:(AuthorizationFlags)flags;
- (BOOL)obtainWithRights:(const AuthorizationRights *)rights flags:(AuthorizationFlags)flags environment:(const AuthorizationEnvironment *)environment authorizedRights:(AuthorizationRights **)authorizedRights error:(NSError **)error;
- (BOOL)obtainWithRight:(AuthorizationString)rightName flags:(AuthorizationFlags)flags error:(NSError **)error;
- (void)invalidateCredentials:(BOOL)destroy;
- (void)invalidateCredentials;
@end
