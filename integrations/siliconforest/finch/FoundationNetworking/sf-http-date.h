/* SPDX-License-Identifier: MIT */
/* Retained CF/ICU date parser; the installed NSDateFormatter lacks timeZone. */
#include <CoreFoundation/CFDateFormatter.h>
#include <CoreFoundation/CFTimeZone.h>
static NSDate *sfHTTPDate(NSString *text,NSArray<NSString *> *patterns){
    text=[text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if(!text.length)return nil;
    CFLocaleRef locale=CFLocaleCreate(NULL,CFSTR("en_US_POSIX"));
    CFTimeZoneRef zone=CFTimeZoneCreateWithTimeIntervalFromGMT(NULL,0);
    NSDate *result=nil;
    for(NSString *pattern in patterns){
        CFDateFormatterRef formatter=CFDateFormatterCreate(NULL,locale,kCFDateFormatterNoStyle,kCFDateFormatterNoStyle);
        if(!formatter)continue;
        CFDateFormatterSetProperty(formatter,kCFDateFormatterTimeZone,zone);
        CFDateFormatterSetFormat(formatter,(CFStringRef)pattern);
        CFRange range=CFRangeMake(0,CFStringGetLength((CFStringRef)text));
        CFDateRef date=CFDateFormatterCreateDateFromString(NULL,formatter,(CFStringRef)text,&range);
        if(date&&range.length==CFStringGetLength((CFStringRef)text))result=[NSDate dateWithTimeIntervalSinceReferenceDate:CFDateGetAbsoluteTime(date)];
        if(date)CFRelease(date);CFRelease(formatter);
        if(result)break;
    }
    if(zone)CFRelease(zone);if(locale)CFRelease(locale);return result;
}
