/* SPDX-License-Identifier: MIT */
/* Own declarations; enum facts checked against the public Go/Rust bindings. */
#pragma once
#include <Accelerate/CBLAS.h>
#include <complex.h>
#include <stdbool.h>
typedef unsigned long sparse_dimension;
typedef long sparse_index, sparse_stride;
typedef int sparse_status, sparse_matrix_property;
typedef enum { SPARSE_NORM_ONE=171, SPARSE_NORM_TWO=173,
               SPARSE_NORM_INF=175, SPARSE_NORM_R1=179 } sparse_norm;
enum { SPARSE_SUCCESS=0, SPARSE_ILLEGAL_PARAMETER=-1000,
       SPARSE_CANNOT_SET_PROPERTY=-1001, SPARSE_SYSTEM_ERROR=-1002,
       SPARSE_UPPER_TRIANGULAR=1, SPARSE_LOWER_TRIANGULAR=2,
       SPARSE_UPPER_SYMMETRIC=4, SPARSE_LOWER_SYMMETRIC=8 };
typedef struct sparse_m_float *sparse_matrix_float;
typedef struct sparse_m_double *sparse_matrix_double;
typedef struct sparse_m_float_complex *sparse_matrix_float_complex;
typedef struct sparse_m_double_complex *sparse_matrix_double_complex;
#ifndef SF_ACCELERATE_PREPROCESS
#include <Accelerate/SparseBLAS-generated.h>
#endif
