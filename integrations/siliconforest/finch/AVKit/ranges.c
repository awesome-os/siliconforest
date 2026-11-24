/* SPDX-License-Identifier: MIT */
/* Own rational-range operations; interface observed in QuickTime and behavior
 * compared by our Linux-built math-reference on the controller Mac. */
#include <CoreMedia/CoreMedia.h>
#include <math.h>
#include <stdint.h>
static int numeric(CMTime t){return (t.flags&1)&&!(t.flags&0x1c)&&t.timescale>0&&t.epoch==0;}
static CMTime scaled(CMTime t,double factor){
 if(factor==trunc(factor)&&factor<=INT32_MAX)return CMTimeMultiply(t,(int32_t)factor);
 t=CMTimeConvertScale(t,1000000000,kCMTimeRoundingMethod_Default);
 CMTime result=CMTimeMultiplyByFloat64(t,factor);
 if((double)t.value*factor!=trunc((double)t.value*factor))result.flags|=2;
 return result;
}
static int valid(CMTimeRange r,CMTime duration){
 return numeric(r.start)&&numeric(r.duration)&&numeric(duration)&&r.duration.value>=0&&duration.value>0;
}
CMTimeRange AVAssetSlowMotionTimeRangeFromNormalizedSlowMotionTimeRange(CMTimeRange range,CMTime duration){
 if(!valid(range,duration))return kCMTimeRangeInvalid;
 double scale=CMTimeGetSeconds(duration);
 range.start=range.start.value?scaled(range.start,scale):kCMTimeZero;
 range.duration=scaled(range.duration,scale);
 return range;
}
CMTimeRange AVNormalizedSlowMotionTimeRangeFromAssetSlowMotionTimeRange(CMTimeRange range,CMTime duration){
 if(!valid(range,duration))return kCMTimeRangeInvalid;
 double scale=1.0/CMTimeGetSeconds(duration);
 range.start=range.start.value?scaled(range.start,scale):kCMTimeZero;
 range.duration=scaled(range.duration,scale);
 return range;
}
