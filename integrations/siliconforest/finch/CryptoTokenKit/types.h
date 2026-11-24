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
