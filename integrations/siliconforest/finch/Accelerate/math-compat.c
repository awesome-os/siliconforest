/* SPDX-License-Identifier: MIT */
/* Local missing libm functions, without changing PD libSystem. Reduce before
 * multiplication and preserve exact integer/half-integer values. */
#include <math.h>
#define LOCAL __attribute__((visibility("hidden")))
LOCAL double __sinpi(double x) {
    double r=remainder(x,2.0);
    if(r==0) return copysign(0.0,r);
    if(r==1 || r==-1) return copysign(0.0,-r);
    if(r==0.5) return 1;
    if(r==-0.5) return -1;
    return sin(M_PI*r);
}
LOCAL double __cospi(double x) {
    double r=remainder(x,2.0);
    if(r==0.5 || r==-0.5) return 0;
    if(r==0) return 1;
    if(r==1 || r==-1) return -1;
    return cos(M_PI*r);
}
LOCAL double __tanpi(double x) { return __sinpi(x)/__cospi(x); }
LOCAL float __sinpif(float x) { return (float)__sinpi((double)x); }
LOCAL float __cospif(float x) { return (float)__cospi((double)x); }
LOCAL float __tanpif(float x) { return (float)__tanpi((double)x); }
