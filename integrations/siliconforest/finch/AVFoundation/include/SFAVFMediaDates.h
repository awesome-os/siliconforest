/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
/* Private adapter while retained Foundation lacks NSISO8601DateFormatter. */
@interface SFAVFMediaDates : NSObject
@property BOOL dateOnly;
- (NSString *)stringFromDate:(NSDate *)date;
- (NSDate *)dateFromString:(NSString *)text;
@end
