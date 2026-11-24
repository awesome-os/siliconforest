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
