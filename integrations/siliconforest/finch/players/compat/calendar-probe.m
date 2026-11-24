/* SPDX-License-Identifier: MIT */
/* Source-owned UTC calendar vectors; no persistent preferences or GUI. */
#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
@interface NSCalendar(SFCalendarProbe)
- (NSInteger)ordinalityOfUnit:(NSUInteger)small inUnit:(NSUInteger)large forDate:(NSDate *)date;
- (void)setTimeZone:(NSTimeZone *)zone;
@end
int main(void){@autoreleasepool {
 CFTimeZoneRef utc=CFTimeZoneCreateWithTimeIntervalFromGMT(NULL,0);CFTimeZoneSetDefault(utc);CFRelease(utc);
 NSCalendar *calendar=[[NSCalendar alloc] initWithCalendarIdentifier:@"gregorian"];
 if(![calendar respondsToSelector:@selector(ordinalityOfUnit:inUnit:forDate:)])return 2;
 if([calendar respondsToSelector:@selector(setTimeZone:)])[calendar setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
 const double dates[]={0,946684800,951782400,951868800,1703980800,1704067200,-2203977600};
 const NSUInteger pairs[][2]={{1<<4,1<<3},{1<<4,1<<2},{1<<4,1<<1},{1<<3,1<<2},{1<<2,1<<1},{1<<2,1<<4}};
 for(unsigned i=0;i<sizeof(dates)/sizeof(dates[0]);i++){
  NSDate *date=[NSDate dateWithTimeIntervalSince1970:dates[i]];
  for(unsigned j=0;j<sizeof(pairs)/sizeof(pairs[0]);j++){
   NSInteger value=[calendar ordinalityOfUnit:pairs[j][0] inUnit:pairs[j][1] forDate:date];
   printf("CALENDAR date=%u pair=%u ordinal=%lld\n",i,j,(long long)value);
  }
 }
 [calendar release];return 0;
}}
