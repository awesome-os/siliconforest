/* SPDX-License-Identifier: MIT */
#import <Security/SecBase.h>
#import <Security/SecIdentity.h>
typedef NSInteger CWEventType;
typedef NSInteger CWSecurity;
typedef NSInteger CWPHYMode;
typedef NSInteger CWChannelWidth;
typedef NSInteger CWChannelBand;
typedef NSInteger CWKeychainDomain;
enum { kCWChannelWidthUnknown = 0, kCWChannelBandUnknown = 0, kCWNotSupportedErr = -3903 };

/* Public selector absent from PD's header; runtime remains subject to conformance checks. */
@interface NSCoder (SFFinchCoreWLANDeclarations)
- (id)decodeObjectOfClass:(Class)cls forKey:(NSString *)key;
@end
