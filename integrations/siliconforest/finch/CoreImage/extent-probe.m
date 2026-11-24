/* SPDX-License-Identifier: MIT */
/* Public image extents plus geometry operations, no rendering/GPU/device. */
#import <CoreImage/CoreImage.h>
#include <float.h>
#include <math.h>
#include <dlfcn.h>
#include <stdio.h>
static void inspect(const char *name,CGRect r) {
    CGRect finite=CGRectMake(-2,-3,5,7);
    CGRect intersection=CGRectIntersection(r,finite),united=CGRectUnion(r,finite);
    printf("RECT %s %a %a %a %a\n",name,r.origin.x,r.origin.y,r.size.width,r.size.height);
    printf("FLAGS %s infinite=%d null=%d empty=%d canonical=%d finite=%d%d%d%d contains=%d%d%d%d%d intersection=%d union=%d\n",
        name,CGRectIsInfinite(r),CGRectIsNull(r),CGRectIsEmpty(r),CGRectEqualToRect(r,CGRectInfinite),
        isfinite(r.origin.x),isfinite(r.origin.y),isfinite(r.size.width),isfinite(r.size.height),
        CGRectContainsPoint(r,CGPointMake(0,0)),CGRectContainsPoint(r,CGPointMake(1e20,-1e20)),
        CGRectContainsPoint(r,CGPointMake(DBL_MAX*.49,DBL_MAX*.49)),
        CGRectContainsPoint(r,CGPointMake(DBL_MAX*.75,DBL_MAX*.75)),
        CGRectContainsRect(r,finite),CGRectEqualToRect(intersection,finite),CGRectEqualToRect(united,r));
    typedef bool(*Predicate)(CGRect);
    Predicate infinite=(Predicate)dlsym(RTLD_DEFAULT,"CGRectIsInfinite");
    Predicate null=(Predicate)dlsym(RTLD_DEFAULT,"CGRectIsNull");
    Predicate empty=(Predicate)dlsym(RTLD_DEFAULT,"CGRectIsEmpty");
    printf("RUNTIME %s infinite=%d null=%d empty=%d\n",name,infinite?infinite(r):-1,null?null(r):-1,empty?empty(r):-1);
}
int main(void) { @autoreleasepool {
    inspect("canonical",CGRectInfinite);inspect("null",CGRectNull);inspect("zero",CGRectZero);
    inspect("zero-width",CGRectMake(2,3,0,7));inspect("zero-height",CGRectMake(2,3,5,0));
    inspect("finite",CGRectMake(-2,-3,5,7));
    CIImage *constant=[CIImage imageWithColor:[CIColor colorWithRed:.25 green:.5 blue:.75 alpha:1]];
    if(!constant)return 1;
    inspect("constant",constant.extent);
    CIImage *cropped=[constant imageByCroppingToRect:CGRectMake(-2,-3,5,7)];
    if(!cropped)return 1;
    inspect("crop",cropped.extent);inspect("clamped",[cropped imageByClampingToExtent].extent);
    CIImage *transformed=[constant imageByApplyingTransform:CGAffineTransformMakeRotation(.25)];
    if(!transformed)return 1;
    inspect("transformed-constant",transformed.extent);
    puts("PASS: image extents and geometry observations");
}return 0;}
