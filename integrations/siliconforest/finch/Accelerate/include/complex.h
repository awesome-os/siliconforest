/* SPDX-License-Identifier: MIT */
/* Standard C complex declarations for arm64; PD's old libm header omits arm64. */
#pragma once
#define complex _Complex
#define _Complex_I (__extension__ 1.0fi)
#define I _Complex_I
#define CMPLX(x,y) __builtin_complex((double)(x),(double)(y))
#define CMPLXF(x,y) __builtin_complex((float)(x),(float)(y))
#define CMPLXL(x,y) __builtin_complex((long double)(x),(long double)(y))
#define SF_COMPLEX_FUNCS(T,S) \
T complex cacos##S(T complex); T complex casin##S(T complex); \
T complex catan##S(T complex); T complex ccos##S(T complex); \
T complex csin##S(T complex); T complex ctan##S(T complex); \
T complex cacosh##S(T complex); T complex casinh##S(T complex); \
T complex catanh##S(T complex); T complex ccosh##S(T complex); \
T complex csinh##S(T complex); T complex ctanh##S(T complex); \
T complex cexp##S(T complex); T complex clog##S(T complex); \
T complex cpow##S(T complex,T complex); T complex csqrt##S(T complex); \
T complex conj##S(T complex); T complex cproj##S(T complex); \
T cabs##S(T complex); T carg##S(T complex); T creal##S(T complex); T cimag##S(T complex);
SF_COMPLEX_FUNCS(float,f)
SF_COMPLEX_FUNCS(double,)
SF_COMPLEX_FUNCS(long double,l)
#undef SF_COMPLEX_FUNCS
typedef float complex __float_complex_t;
typedef double complex __double_complex_t;
