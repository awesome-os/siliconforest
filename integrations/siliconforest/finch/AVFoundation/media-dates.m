/* SPDX-License-Identifier: MIT */
#import <SFAVFMediaDates.h>
#import <CoreFoundation/CoreFoundation.h>

@implementation SFAVFMediaDates
static CFDateFormatterRef formatter(BOOL dateOnly)
{
    CFLocaleRef locale = CFLocaleCreate(NULL, CFSTR("en_US_POSIX"));
    CFDateFormatterRef f = CFDateFormatterCreate(NULL, locale, kCFDateFormatterNoStyle, kCFDateFormatterNoStyle);
    CFRelease(locale);
    if (!f) return NULL;
    CFTimeZoneRef zone = CFTimeZoneCreateWithTimeIntervalFromGMT(NULL, 0);
    CFDateFormatterSetProperty(f, kCFDateFormatterTimeZone, zone);
    CFDateFormatterSetProperty(f, kCFDateFormatterIsLenient, kCFBooleanFalse);
    CFDateFormatterSetFormat(f, dateOnly ? CFSTR("yyyy-MM-dd") : CFSTR("yyyy-MM-dd'T'HH:mm:ssXXXXX"));
    CFRelease(zone);
    return f;
}
- (NSString *)stringFromDate:(NSDate *)date
{
    if (!date) return nil;
    CFDateFormatterRef f = formatter(NO);
    if (!f) return nil;
    CFStringRef text = CFDateFormatterCreateStringWithAbsoluteTime(NULL, f, [date timeIntervalSince1970] - 978307200.0);
    CFRelease(f);
    return CFBridgingRelease(text);
}
- (NSDate *)dateFromString:(NSString *)text
{
    if (!text) return nil;
    CFDateFormatterRef f = formatter(self.dateOnly);
    if (!f) return nil;
    CFRange range = CFRangeMake(0, (CFIndex)text.length);
    CFDateRef date = CFDateFormatterCreateDateFromString(NULL, f, (__bridge CFStringRef)text, &range);
    CFRelease(f);
    NSDate *result = date && range.length == (CFIndex)text.length ?
        [NSDate dateWithTimeIntervalSince1970:CFDateGetAbsoluteTime(date) + 978307200.0] : nil;
    if (date) CFRelease(date);
    return result;
}
@end
