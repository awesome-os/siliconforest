/* SPDX-License-Identifier: MIT */
/* Calendar ordinality through the existing ICU-backed CoreFoundation engine.
 * No NSCalendar layout assumptions or replacement of existing methods. */
#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>
#include <math.h>
@interface NSCalendar(SFCalendarDeclarations)
- (NSString *)calendarIdentifier;
- (NSTimeZone *)timeZone;
- (NSUInteger)firstWeekday;
- (NSUInteger)minimumDaysInFirstWeek;
- (NSInteger)sfOrdinalityOfUnit:(NSUInteger)small inUnit:(NSUInteger)large forDate:(NSDate *)date;
@end
@implementation NSCalendar(SFOrdinality)
+ (void)load {
 SEL target=@selector(ordinalityOfUnit:inUnit:forDate:);
 if(!class_getInstanceMethod(self,target)){
  Method source=class_getInstanceMethod(self,@selector(sfOrdinalityOfUnit:inUnit:forDate:));
  class_addMethod(self,target,method_getImplementation(source),method_getTypeEncoding(source));
 }
}
- (NSInteger)sfOrdinalityOfUnit:(NSUInteger)small inUnit:(NSUInteger)large forDate:(NSDate *)date {
 if(!date || ![date isKindOfClass:[NSDate class]])return NSNotFound;
 CFAbsoluteTime at=[date timeIntervalSinceReferenceDate];if(!isfinite(at))return NSNotFound;
 NSString *identifier=[self respondsToSelector:@selector(calendarIdentifier)]?[self calendarIdentifier]:@"gregorian";
 CFCalendarRef calendar=CFCalendarCreateWithIdentifier(NULL,(CFStringRef)identifier);
 if(!calendar)return NSNotFound;
 if([self respondsToSelector:@selector(timeZone)]){
  NSString *name=[[self timeZone] name];
  CFTimeZoneRef zone=name?CFTimeZoneCreateWithName(NULL,(CFStringRef)name,true):NULL;
  if(zone){CFCalendarSetTimeZone(calendar,zone);CFRelease(zone);}
 }
 if([self respondsToSelector:@selector(firstWeekday)])CFCalendarSetFirstWeekday(calendar,[self firstWeekday]);
 if([self respondsToSelector:@selector(minimumDaysInFirstWeek)])CFCalendarSetMinimumDaysInFirstWeek(calendar,[self minimumDaysInFirstWeek]);
 CFIndex result=CFCalendarGetOrdinalityOfUnit(calendar,(CFCalendarUnit)small,(CFCalendarUnit)large,at);
 CFRelease(calendar);return result==kCFNotFound?NSNotFound:(NSInteger)result;
}
@end
