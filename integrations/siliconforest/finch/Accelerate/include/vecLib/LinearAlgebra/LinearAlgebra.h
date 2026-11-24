/* SPDX-License-Identifier: MIT */
/* Our declarations. Scalar/norm/error values measured by our Linux-built
 * runtime probe on macOS 26.5.2; public type widths checked in Apple API docs. */
#pragma once
#include <stddef.h>
typedef unsigned long la_count_t,la_hint_t,la_attribute_t,la_norm_t;
typedef long la_index_t,la_status_t;
typedef unsigned int la_scalar_type_t;
typedef void (*la_deallocator_t)(void *);
#ifdef __OBJC__
@class OS_la_object;
typedef OS_la_object *la_object_t;
#define SF_LA_RETAINED __attribute__((ns_returns_retained))
#else
typedef struct la_s *la_object_t;
#define SF_LA_RETAINED
#endif
enum { LA_SUCCESS=0, LA_INTERNAL_ERROR=-1000, LA_INVALID_PARAMETER_ERROR=-1001,
       LA_DIMENSION_MISMATCH_ERROR=-1002, LA_PRECISION_MISMATCH_ERROR=-1003,
       LA_SINGULAR_ERROR=-1004, LA_SLICE_OUT_OF_BOUNDS_ERROR=-1005,
       LA_SCALAR_TYPE_DOUBLE=16384, LA_SCALAR_TYPE_FLOAT=32768,
       LA_L1_NORM=1, LA_L2_NORM=2, LA_LINF_NORM=3,
       LA_DEFAULT_ATTRIBUTES=0, LA_ATTRIBUTE_ENABLE_LOGGING=1, LA_NO_HINT=0,
       LA_SHAPE_DIAGONAL=1, LA_SHAPE_LOWER_TRIANGULAR=2, LA_SHAPE_UPPER_TRIANGULAR=4,
       LA_FEATURE_SYMMETRIC=1<<16, LA_FEATURE_POSITIVE_DEFINITE=1<<17,
       LA_FEATURE_DIAGONALLY_DOMINANT=1<<18 };
/* Finch ignores hint flags; their numerical optimization is not implemented. */
#ifndef SF_ACCELERATE_PREPROCESS
#include <Accelerate/LinearAlgebra-generated.h>
#endif

/* These public convenience interfaces are macros on Apple's library too. */
#define la_vector_from_double_buffer(p,n,s,a) la_matrix_from_double_buffer(p,n,1,s,LA_NO_HINT,a)
#define la_vector_from_float_buffer(p,n,s,a) la_matrix_from_float_buffer(p,n,1,s,LA_NO_HINT,a)
#define la_vector_from_double_buffer_nocopy(p,n,s,d,a) la_matrix_from_double_buffer_nocopy(p,n,1,s,LA_NO_HINT,d,a)
#define la_vector_from_float_buffer_nocopy(p,n,s,d,a) la_matrix_from_float_buffer_nocopy(p,n,1,s,LA_NO_HINT,d,a)
#define la_vector_reverse(v) la_vector_slice(v,(la_index_t)la_vector_length(v)-1,-1,la_vector_length(v))
#if !defined(SF_ACCELERATE_BUILD) && !defined(__OBJC__)
void *sf_la_objc_retain(void *) __asm__("_objc_retain");
void sf_la_objc_release(void *) __asm__("_objc_release");
#define la_retain(o) ((la_object_t)sf_la_objc_retain(o))
#define la_release(o) sf_la_objc_release(o)
#endif
